// SPDX-License-Identifier: GPL-3.0-or-later
// An edited chart for the pptx writer (pptx_write_shapes.h; M67, D-076;
// M82, D-091). The chart part is never re-derived from the model: the
// SOURCE part is copied and only what the edit touched is rewritten in it —
//   - every plot's series (pptx_write_chart_series.cpp): names, categories
//     — multi-level ones with their outer levels — and values, the series
//     formulas recomputed for the data's new extent where the first
//     series' formulas placed it, the caches PowerPoint draws from refilled;
//     series matched by c:idx, so a combo chart's columns and lines both
//     follow the data and per-point colours and labels stay with their
//     points;
//   - a kind change rebuilds the plot it changes (a combo chart's plots one
//     by one) as the new kind, keeping the series' names, colours, per-point
//     colours and data labels, with the axes that kind needs
//     (pptx_write_chart_kind.cpp);
//   - the embedded workbook (c:externalData) is rewritten through calc_io
//     (chart_workbook.h) so PowerPoint's Edit Data shows the same numbers.
// Titles, legends, text and number formats, chart styles and colours (the
// chartStyle / chartColorStyle parts) stay as they were.

#include "pptx_write_shapes.h"

#include "chart_workbook.h"
#include "pptx_read_shapes.h"
#include "pptx_write_chart_internal.h"

#include "ooxml_namespaces.h"

namespace QindaOffice::Show::Pptx {

namespace {

using Ooxml::XmlChild;
using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;

using ChartXml::make;
using ChartXml::valued;

const QString kChartType = QStringLiteral("application/vnd.openxmlformats-officedocument.drawingml.chart+xml");
const QString kXlsxType = QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet");

QString formulaOf(const XmlElement *holder)
{
    const XmlElement *f = holder != nullptr ? holder->descendant(nsC(), u"f") : nullptr;
    return f != nullptr ? f->text().trimmed() : QString();
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

// Whether two charts hold the same data in the same kinds (colours aside).
bool sameData(const ChartData &a, const ChartData &b)
{
    if (a.type != b.type || a.grouping != b.grouping || a.series.size() != b.series.size() || a.plots != b.plots
        || a.categoryLevels != b.categoryLevels) {
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
    QList<XmlElement *> plots = plotArea != nullptr ? ChartXml::plotsOf(*plotArea) : QList<XmlElement *>();
    const XmlElement *first = !plots.isEmpty() ? plots.first()->child(nsC(), u"ser") : nullptr;
    if (first == nullptr) {
        return {};
    }
    // Where the data sits, from the first series as the file has it.
    ChartXml::DataShape shape;
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
    // Each plot whose kind changed is rebuilt (a combo chart's one by one).
    bool converted = false;
    for (int p = 0; p < plots.size(); ++p) {
        const ChartPlot now = chart.plotAt(p);
        if (now == previous.plotAt(p)) {
            continue;
        }
        ChartData kind;
        kind.type = now.type;
        kind.grouping = now.grouping;
        plots[p] = convertPlot(*plotArea, plots.at(p), kind);
        if (plots.at(p) == nullptr) {
            return {};
        }
        converted = true;
        if (p > 0) {
            continue;
        }
        if (plots.at(p)->name == u"scatterChart") {
            shape.categoryFormat = shape.categoryFormat.isNull() ? QStringLiteral("General") : shape.categoryFormat;
        } else if (!previous.type.isEmpty() && previous.type == u"scatter") {
            shape.categoryFormat = QString(); // labels again
            shape.layout.categoriesNumeric = false;
        }
    }
    if (converted) {
        // The kinds QindaShow draws are flat: a 3-D view has nothing left to show.
        for (const char16_t *name : {u"view3D", u"floor", u"sideWall", u"backWall"}) {
            body->removeChildren(nsC(), name);
        }
    }
    // M82: multi-level categories keep their levels where the workbook has
    // room for them (the outer ones in the columns, or rows, before the
    // series' own labels).
    const int levels = chart.type == u"scatter" ? 1 : 1 + int(chart.categoryLevels.size());
    const int room = shape.layout.seriesInRows ? shape.layout.headerRow : shape.layout.categoryColumn;
    shape.layout.levels = levels - 1 <= room ? levels : 1;
    if (!ChartXml::patchPlots(plots, chart, shape)) {
        return {};
    }

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
