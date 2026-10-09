// SPDX-License-Identifier: GPL-3.0-or-later
// An edited chart for the pptx writer (pptx_write_shapes.h; M67, D-076).
// The chart part is never re-derived from the model: the SOURCE part is
// copied and only what the edit touched is rewritten in it —
//   - each series' name, categories and values: the series formulas (c:f)
//     recomputed for the data's new extent where the first series' formulas
//     placed it, and the caches PowerPoint draws from (c:strCache /
//     c:numCache) refilled; added series are copies of the last one (their
//     accent advanced), removed ones are dropped, per-point overrides past
//     the new last point go;
//   - a kind change rebuilds the first plot as the new kind (series keep
//     their name, colour and data) with the axes that kind needs;
//   - the embedded workbook (c:externalData) is rewritten through calc_io
//     (chart_workbook.h) so PowerPoint's Edit Data shows the same numbers.
// Titles, legends, text and number formats, chart styles and colours (the
// chartStyle / chartColorStyle parts) stay as they were.

#include "pptx_write_shapes.h"

#include "chart_workbook.h"
#include "pptx_read_shapes.h"

#include "ooxml_namespaces.h"

#include <QLocale>

#include <cmath>

namespace QindaOffice::Show::Pptx {

namespace {

using Ooxml::XmlChild;
using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;

const QString kChartType = QStringLiteral("application/vnd.openxmlformats-officedocument.drawingml.chart+xml");
const QString kXlsxType = QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet");

XmlElementPtr make(const QString &name)
{
    return std::make_shared<XmlElement>(nsC(), name);
}

XmlElementPtr valued(const QString &name, const QString &value)
{
    XmlElementPtr e = make(name);
    e->setAttribute(QString(), QStringLiteral("val"), value);
    return e;
}

QString formulaOf(const XmlElement *holder)
{
    const XmlElement *f = holder != nullptr ? holder->descendant(nsC(), u"f") : nullptr;
    return f != nullptr ? f->text().trimmed() : QString();
}

QString number(qreal value)
{
    return QString::number(value, 'g', QLocale::FloatingPointShortest);
}

XmlElementPtr cacheOf(const char *kind, const QStringList &points, const QString &formatCode)
{
    XmlElementPtr cache = make(QLatin1String(kind));
    if (!formatCode.isNull()) {
        cache->appendElement(nsC(), QStringLiteral("formatCode")).appendText(formatCode);
    }
    cache->appendElement(valued(QStringLiteral("ptCount"), QString::number(points.size())));
    for (int i = 0; i < points.size(); ++i) {
        if (points.at(i).isNull()) {
            continue; // an empty cell has no point
        }
        XmlElement &pt = cache->appendElement(nsC(), QStringLiteral("pt"));
        pt.setAttribute(QString(), QStringLiteral("idx"), QString::number(i));
        pt.appendElement(nsC(), QStringLiteral("v")).appendText(Ooxml::xmlSafeText(points.at(i)));
    }
    return cache;
}

// c:strRef / c:numRef holding `formula` and its cached points.
XmlElementPtr reference(bool numeric, const QString &formula, const QStringList &points, const QString &formatCode)
{
    XmlElementPtr ref = make(numeric ? QStringLiteral("numRef") : QStringLiteral("strRef"));
    ref->appendElement(nsC(), QStringLiteral("f")).appendText(formula);
    ref->appendElement(numeric ? cacheOf("numCache", points, formatCode.isNull() ? QStringLiteral("General") : formatCode)
                               : cacheOf("strCache", points, QString()));
    return ref;
}

QStringList numberPoints(const QList<qreal> &values, int count)
{
    QStringList points;
    for (int i = 0; i < count; ++i) {
        const qreal v = values.value(i, std::nan(""));
        points.append(std::isnan(v) ? QString() : number(v));
    }
    return points;
}

// Replaces the first child named `name` (keeping its place), or inserts.
void replaceChild(XmlElement &parent, const QString &name, const XmlElementPtr &with, const QList<QStringView> &before)
{
    for (XmlChild &c : parent.children) {
        if (c.element && c.element->is(nsC(), name)) {
            c.element = with;
            return;
        }
    }
    parent.insertElementBefore(with, before);
}

// Drops per-point overrides (c:dPt, c:dLbl) at or past `count`.
void dropPointsFrom(XmlElement &holder, int count)
{
    holder.children.removeIf([count](const XmlChild &c) {
        if (!c.element || (c.element->name != u"dPt" && c.element->name != u"dLbl")) {
            return false;
        }
        const XmlElement *idx = c.element->child(nsC(), u"idx");
        return idx != nullptr && idx->attr(u"val").toInt() >= count;
    });
    if (XmlElement *labels = holder.child(nsC(), u"dLbls")) {
        dropPointsFrom(*labels, count);
    }
}

// Every theme accent in a copied series moves on to the next series' one.
void advanceAccent(XmlElement &e, int index)
{
    if (e.name == u"schemeClr" && e.attr(u"val").startsWith(QLatin1String("accent"))) {
        e.setAttribute(QString(), QStringLiteral("val"), QStringLiteral("accent%1").arg(index % 6 + 1));
    }
    for (XmlChild &c : e.children) {
        if (c.element) {
            advanceAccent(*c.element, index);
        }
    }
}

// An added series is drawn in the model's colour — or, with none, in the
// chart's own palette (no fill of its own) — whatever the copied series'
// look said. Line-drawn series carry the colour on their line.
void recolor(XmlElement &ser, const QColor &color)
{
    XmlElement *spPr = ser.child(nsC(), u"spPr");
    if (spPr == nullptr) {
        if (!color.isValid()) {
            return;
        }
        spPr = &ser.insertElementBefore(make(QStringLiteral("spPr")), {u"invertIfNegative", u"pictureOptions", u"marker",
                                                                       u"explosion", u"dPt", u"dLbls", u"trendline", u"errBars",
                                                                       u"cat", u"val", u"xVal", u"yVal", u"extLst"});
    }
    static const QList<QStringView> kFills{u"noFill", u"solidFill", u"gradFill", u"blipFill", u"pattFill", u"grpFill"};
    XmlElement *line = spPr->child(nsA(), u"ln");
    const bool lineDrawn = line != nullptr && line->child(nsA(), u"solidFill") != nullptr && spPr->child(nsA(), u"solidFill") == nullptr
        && spPr->child(nsA(), u"gradFill") == nullptr;
    XmlElement &target = lineDrawn ? *line : *spPr;
    for (const QStringView fill : kFills) {
        target.removeChildren(nsA(), fill);
    }
    if (color.isValid()) {
        XmlElementPtr solid = std::make_shared<XmlElement>(nsA(), QStringLiteral("solidFill"));
        solid->appendElement(colorElement(color));
        target.insertElementBefore(solid, lineDrawn ? QList<QStringView>{u"prstDash", u"custDash", u"round", u"bevel", u"miter",
                                                                          u"headEnd", u"tailEnd", u"extLst"}
                                                    : QList<QStringView>{u"ln", u"effectLst", u"effectDag", u"scene3d",
                                                                          u"sp3d", u"extLst"});
    }
}

struct DataShape {
    ChartWorkbook::Layout layout;
    QString categoryFormat; // formatCode of numeric categories (null = strings)
    QString valueFormat;
};

void patchSeries(XmlElement &ser, int index, const ChartData &chart, const DataShape &shape, bool scatterLike)
{
    const ChartSeries &series = chart.series.at(index);
    const int count = int(qMax(series.values.size(), series.categories.size()));
    const ChartWorkbook::Layout &layout = shape.layout;
    XmlElementPtr tx = make(QStringLiteral("tx"));
    const ChartWorkbook::Cell nameCell = layout.nameCell(index);
    tx->appendElement(reference(false, ChartWorkbook::reference(layout.sheet, nameCell, nameCell), {series.name}, QString()));
    replaceChild(ser, QStringLiteral("tx"), tx, {u"spPr", u"invertIfNegative", u"pictureOptions", u"marker", u"explosion", u"dPt",
                                                 u"dLbls", u"trendline", u"errBars", u"cat", u"val", u"xVal", u"yVal", u"shape",
                                                 u"smooth", u"bubbleSize", u"bubble3D", u"extLst"});
    const auto range = [&](auto cellAt) {
        return ChartWorkbook::reference(layout.sheet, cellAt(0), cellAt(qMax(0, count - 1)));
    };
    QStringList categories = series.categories;
    while (categories.size() < count) {
        categories.append(QString());
    }
    // An empty category label is a point without a cached value.
    for (QString &category : categories) {
        if (category.isEmpty()) {
            category = QString();
        }
    }
    // Numbers where the file had numbers (and x values), if they all are.
    bool allNumbers = true;
    for (const QString &category : std::as_const(categories)) {
        bool ok = category.isEmpty();
        if (!ok) {
            (void)category.toDouble(&ok);
        }
        allNumbers = allNumbers && ok;
    }
    const bool numericCategories = (scatterLike || !shape.categoryFormat.isNull()) && allNumbers;
    XmlElementPtr cat = make(scatterLike ? QStringLiteral("xVal") : QStringLiteral("cat"));
    cat->appendElement(reference(numericCategories, range([&](int k) { return layout.categoryCell(k); }), categories,
                                 shape.categoryFormat));
    XmlElementPtr val = make(scatterLike ? QStringLiteral("yVal") : QStringLiteral("val"));
    val->appendElement(reference(true, range([&](int k) { return layout.valueCell(index, k); }),
                                 numberPoints(series.values, count), shape.valueFormat));
    replaceChild(ser, cat->name, cat, {u"val", u"yVal", u"shape", u"smooth", u"bubbleSize", u"bubble3D", u"extLst"});
    replaceChild(ser, val->name, val, {u"shape", u"smooth", u"bubbleSize", u"bubble3D", u"extLst"});
    dropPointsFrom(ser, count);
}

// The series of the first plot, as the data now has them.
void patchAllSeries(XmlElement &plot, const ChartData &chart, const DataShape &shape)
{
    QList<XmlElement *> series;
    for (XmlChild &c : plot.children) {
        if (c.element && c.element->is(nsC(), u"ser")) {
            series.append(c.element.get());
        }
    }
    const bool scatterLike = plot.name == u"scatterChart" || plot.name == u"bubbleChart";
    int nextIdx = 0;
    for (const XmlElement *ser : std::as_const(series)) {
        if (const XmlElement *idx = ser->child(nsC(), u"idx")) {
            nextIdx = qMax(nextIdx, idx->attr(u"val").toInt() + 1);
        }
    }
    // Removed series go; added ones copy the last one.
    while (series.size() > chart.series.size() && !series.isEmpty()) {
        XmlElement *last = series.takeLast();
        plot.children.removeIf([last](const XmlChild &c) { return c.element.get() == last; });
    }
    if (series.isEmpty() && !chart.series.isEmpty()) {
        return; // nothing to copy: the caller writes a picture
    }
    while (series.size() < chart.series.size()) {
        XmlElementPtr copy = series.last()->clone();
        const int index = int(series.size());
        copy->children.removeIf([](const XmlChild &c) { return c.element && c.element->name == u"dPt"; });
        if (XmlElement *labels = copy->child(nsC(), u"dLbls")) {
            labels->children.removeIf([](const XmlChild &c) { return c.element && c.element->name == u"dLbl"; });
        }
        replaceChild(*copy, QStringLiteral("idx"), valued(QStringLiteral("idx"), QString::number(nextIdx)), {u"order"});
        replaceChild(*copy, QStringLiteral("order"), valued(QStringLiteral("order"), QString::number(nextIdx)), {u"tx"});
        ++nextIdx;
        if (XmlElement *spPr = copy->child(nsC(), u"spPr")) {
            advanceAccent(*spPr, index);
        }
        if (XmlElement *marker = copy->child(nsC(), u"marker")) {
            advanceAccent(*marker, index);
        }
        recolor(*copy, chart.series.at(index).color);
        // After the last series, wherever it sits among the plot's children.
        const XmlElement *after = series.last();
        for (qsizetype i = 0; i < plot.children.size(); ++i) {
            if (plot.children.at(i).element.get() == after) {
                plot.children.insert(i + 1, {copy, QString()});
                break;
            }
        }
        series.append(copy.get());
    }
    for (int i = 0; i < series.size(); ++i) {
        patchSeries(*series.at(i), i, chart, shape, scatterLike);
    }
}

// The c:chart reference of a chart's graphic frame.
XmlElement *chartReference(XmlElement &frame)
{
    XmlElement *graphic = frame.child(nsA(), u"graphic");
    XmlElement *data = graphic != nullptr ? graphic->child(nsA(), u"graphicData") : nullptr;
    return data != nullptr ? data->child(nsC(), u"chart") : nullptr;
}

QString sourceChartPart(const PartWriter &out, const QString &part, const QString &relationshipId)
{
    const auto rel = out.session.source ? out.session.source->relationship(part, relationshipId) : std::nullopt;
    return rel && !rel->external ? out.session.source->targetPart(part, *rel) : QString();
}

// Whether two charts hold the same data in the same kind (colours aside).
bool sameData(const ChartData &a, const ChartData &b)
{
    if (a.type != b.type || a.grouping != b.grouping || a.series.size() != b.series.size()) {
        return false;
    }
    for (qsizetype i = 0; i < a.series.size(); ++i) {
        ChartSeries x = a.series.at(i);
        ChartSeries y = b.series.at(i);
        x.color = y.color = QColor();
        x.pointColors.clear();
        y.pointColors.clear();
        if (!(x == y)) {
            return false;
        }
    }
    return true;
}

// The data a chart part caches (colours resolved against the Office theme:
// only the data is compared).
ChartData cachedChart(const std::shared_ptr<const Ooxml::Package> &package, const QString &part)
{
    static const ThemeContext theme = [] {
        ThemeContext office;
        office.theme = Ooxml::defaultOfficeTheme();
        return office;
    }();
    PartContext ctx;
    ctx.package = package;
    ctx.part = part;
    ctx.theme = &theme;
    return readChart(ctx, part);
}

} // namespace

DetachedChart detachChart(const PartWriter &out, XmlElement &frame, const QString &part)
{
    DetachedChart edit;
    edit.reference = chartReference(frame);
    if (edit.reference != nullptr) {
        edit.sourcePart = sourceChartPart(out, part, edit.reference->attribute(nsR(), u"id"));
        edit.reference->removeAttribute(nsR(), u"id");
    }
    return edit;
}

// Writes the edited chart part and names it in the frame; false when the
// source chart part could not be used.
bool attachChart(PartWriter &out, const DetachedChart &edit, const ChartData &chart, const ChartData &previous)
{
    const QString rel = edit.sourcePart.isEmpty() ? QString() : writeEditedChart(out, edit.sourcePart, chart, previous);
    if (rel.isEmpty()) {
        return false;
    }
    edit.reference->setAttribute(nsR(), QStringLiteral("id"), rel);
    return true;
}

DetachedChart detachEditedCopy(PartWriter &out, XmlElement &frame, const QString &part, const ChartData &chart,
                               ChartData *cached)
{
    XmlElement *reference = frame.name == u"graphicFrame" ? chartReference(frame) : nullptr;
    const QString chartPart = reference != nullptr ? sourceChartPart(out, part, reference->attribute(nsR(), u"id")) : QString();
    if (chartPart.isEmpty()) {
        return {};
    }
    *cached = cachedChart(out.session.source, chartPart);
    return sameData(*cached, chart) ? DetachedChart() : detachChart(out, frame, part);
}

QString writeEditedChart(PartWriter &out, const QString &sourcePart, const ChartData &chart, const ChartData &previous)
{
    WriteSession &session = out.session;
    if (!session.source || !session.copier || chart.series.isEmpty()) {
        return {};
    }
    const XmlElementPtr source = session.source->xml(sourcePart);
    if (!source) {
        return {};
    }
    XmlElementPtr chartSpace = source->clone();
    XmlElement *body = chartSpace->child(nsC(), u"chart");
    XmlElement *plotArea = body != nullptr ? body->child(nsC(), u"plotArea") : nullptr;
    XmlElement *plot = nullptr;
    if (plotArea != nullptr) {
        for (XmlChild &c : plotArea->children) {
            if (c.element && c.element->ns == nsC() && c.element->name.endsWith(QLatin1String("Chart"))) {
                plot = c.element.get();
                break;
            }
        }
    }
    const XmlElement *first = plot != nullptr ? plot->child(nsC(), u"ser") : nullptr;
    if (first == nullptr) {
        return {};
    }
    // Where the data sits, from the first series as the file has it.
    DataShape shape;
    const bool scatterLike = first->child(nsC(), u"xVal") != nullptr || first->child(nsC(), u"yVal") != nullptr;
    const XmlElement *sourceCat = first->child(nsC(), scatterLike ? u"xVal" : u"cat");
    const XmlElement *sourceVal = first->child(nsC(), scatterLike ? u"yVal" : u"val");
    shape.layout = ChartWorkbook::layoutFrom(formulaOf(first->child(nsC(), u"tx")), formulaOf(sourceCat), formulaOf(sourceVal));
    if (const XmlElement *numbers = sourceCat != nullptr ? sourceCat->descendant(nsC(), u"numCache") : nullptr) {
        const XmlElement *format = numbers->child(nsC(), u"formatCode");
        shape.categoryFormat = format != nullptr ? format->text() : QStringLiteral("General");
        shape.layout.categoriesNumeric = true;
    }
    if (const XmlElement *numbers = sourceVal != nullptr ? sourceVal->descendant(nsC(), u"numCache") : nullptr) {
        if (const XmlElement *format = numbers->child(nsC(), u"formatCode")) {
            shape.valueFormat = format->text();
        }
    }
    if (chart.type != previous.type || chart.grouping != previous.grouping) {
        plot = convertPlot(*plotArea, plot, chart);
        if (plot == nullptr) {
            return {};
        }
        // The kinds QindaShow draws are flat: a 3-D view has nothing left to show.
        for (const char16_t *name : {u"view3D", u"floor", u"sideWall", u"backWall"}) {
            body->removeChildren(nsC(), name);
        }
        if (plot->name == u"scatterChart") {
            shape.categoryFormat = shape.categoryFormat.isNull() ? QStringLiteral("General") : shape.categoryFormat;
        } else if (!previous.type.isEmpty() && previous.type == u"scatter") {
            shape.categoryFormat = QString(); // labels again
            shape.layout.categoriesNumeric = false;
        }
    }
    patchAllSeries(*plot, chart, shape);

    // The new part and its relationships: the source's, but the workbook.
    const QString part = session.writer.uniquePartName(QStringLiteral("ppt/charts/chart"), QStringLiteral(".xml"));
    XmlElement *external = chartSpace->child(nsC(), u"externalData");
    const QString externalId = external != nullptr ? external->attribute(nsR(), u"id") : QString();
    QByteArray workbook;
    for (const Ooxml::Relationship &rel : session.source->relationships(sourcePart)) {
        if (rel.id == externalId && !rel.external) {
            const QString target = session.source->targetPart(sourcePart, rel);
            if (session.source->contentType(target) == kXlsxType) {
                workbook = session.source->data(target);
            }
            continue;
        }
        if (rel.external) {
            session.writer.addRelationshipWithId(part, rel.id, rel.type, rel.target, true);
        } else if (const QString target = session.source->targetPart(sourcePart, rel); !target.isEmpty()) {
            session.writer.addRelationshipWithId(part, rel.id, rel.type, session.copier->copyPart(target));
        }
    }
    const bool linked = !externalId.isEmpty() && [&] {
        const auto rel = session.source->relationship(sourcePart, externalId);
        return rel && rel->external;
    }();
    if (!linked) {
        // AGENT-NOTE: a chart linked to an outside workbook keeps its link
        // (PowerPoint refreshes from that file); an embedded one gets the
        // edited data written into its workbook, or a new workbook.
        QString error;
        const QByteArray bytes = ChartWorkbook::write(workbook, chart, previous, shape.layout, &error);
        if (bytes.isEmpty()) {
            if (external != nullptr) {
                chartSpace->removeChildren(nsC(), u"externalData");
            }
        } else {
            const QString embedding = session.writer.uniquePartName(QStringLiteral("ppt/embeddings/Microsoft_Excel_Worksheet"),
                                                                    QStringLiteral(".xlsx"));
            session.writer.addPart(embedding, bytes, kXlsxType);
            QString id = externalId;
            if (id.isEmpty()) {
                id = session.writer.addRelationship(part, relType("package"), embedding);
                XmlElementPtr data = make(QStringLiteral("externalData"));
                data->setAttribute(nsR(), QStringLiteral("id"), id);
                data->appendElement(valued(QStringLiteral("autoUpdate"), QStringLiteral("0")));
                chartSpace->insertElementBefore(data, {u"printSettings", u"userShapes", u"extLst"});
            } else {
                session.writer.addRelationshipWithId(part, id, relType("package"), embedding);
            }
        }
    }
    session.writer.addPart(part, Ooxml::serializeDocument(*chartSpace), kChartType);
    return out.relate(relType("chart"), part);
}

} // namespace QindaOffice::Show::Pptx
