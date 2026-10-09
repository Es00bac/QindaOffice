// SPDX-License-Identifier: GPL-3.0-or-later
// The series of an edited chart (pptx_write_chart_internal.h; M67, D-076;
// M82, D-091). Every plot of the plot area — a combo chart's columns and
// lines alike — gets its series as the data now has them, each matched to
// the file's by c:idx: its name, categories and values rewritten (series
// formulas for the data's extent, the caches PowerPoint draws from),
// multi-level categories with their outer levels (c:multiLvlStrRef), and
// its per-point colours and data labels (c:dPt, c:dLbl) moved with their
// categories. Added series copy their plot's last one (accent advanced),
// removed ones go.

#include "pptx_write_chart_internal.h"

#include "chart_edit.h"

#include "ooxml_namespaces.h"

#include <QLocale>
#include <QSet>

#include <cmath>

namespace QindaOffice::Show::Pptx::ChartXml {

using Ooxml::XmlChild;
using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;

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

QList<XmlElement *> plotsOf(XmlElement &plotArea)
{
    QList<XmlElement *> plots;
    for (XmlChild &c : plotArea.children) {
        if (c.element && c.element->ns == nsC() && c.element->name.endsWith(QLatin1String("Chart"))) {
            plots.append(c.element.get());
        }
    }
    return plots;
}

XmlElementPtr labelsForKind(const XmlElement &labels)
{
    XmlElementPtr out = labels.clone();
    out->removeChildren(nsC(), u"dLblPos");
    for (XmlChild &c : out->children) {
        if (c.element && c.element->is(nsC(), u"dLbl")) {
            c.element->removeChildren(nsC(), u"dLblPos");
        }
    }
    return out;
}

namespace {

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

// c:multiLvlStrRef: the series' own labels first, then the outer levels.
XmlElementPtr multiLevelReference(const QString &formula, const QStringList &leaf, const QList<QStringList> &outer)
{
    XmlElementPtr ref = make(QStringLiteral("multiLvlStrRef"));
    ref->appendElement(nsC(), QStringLiteral("f")).appendText(formula);
    XmlElement &cache = ref->appendElement(nsC(), QStringLiteral("multiLvlStrCache"));
    cache.appendElement(valued(QStringLiteral("ptCount"), QString::number(leaf.size())));
    for (const QStringList &level : QList<QStringList>{leaf} + outer) {
        XmlElement &lvl = cache.appendElement(nsC(), QStringLiteral("lvl"));
        for (int i = 0; i < leaf.size(); ++i) {
            const QString label = level.value(i);
            if (label.isEmpty()) {
                continue;
            }
            XmlElement &pt = lvl.appendElement(nsC(), QStringLiteral("pt"));
            pt.setAttribute(QString(), QStringLiteral("idx"), QString::number(i));
            pt.appendElement(nsC(), QStringLiteral("v")).appendText(Ooxml::xmlSafeText(label));
        }
    }
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

// Per-point overrides (c:dPt, c:dLbl) moved to their categories' new
// places (`moved`: the file's point → its place now); a deleted
// category's go with it.
void movePoints(XmlElement &holder, const QHash<int, int> &moved)
{
    const auto isPoint = [](const XmlChild &c) { return c.element && (c.element->name == u"dPt" || c.element->name == u"dLbl"); };
    holder.children.removeIf([&](const XmlChild &c) {
        if (!isPoint(c)) {
            return false;
        }
        const XmlElement *idx = c.element->child(nsC(), u"idx");
        return idx == nullptr || !moved.contains(idx->attr(u"val").toInt());
    });
    for (XmlChild &c : holder.children) {
        if (isPoint(c)) {
            XmlElement *idx = c.element->child(nsC(), u"idx");
            idx->setAttribute(QString(), QStringLiteral("val"), QString::number(moved.value(idx->attr(u"val").toInt())));
        }
    }
    if (XmlElement *labels = holder.child(nsC(), u"dLbls")) {
        movePoints(*labels, moved);
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
    if (!scatterLike && layout.levels > 1) {
        // M82: the groups around the labels (Region ▸ Quarter) come along.
        QList<QStringList> outer;
        for (int l = 1; l < layout.levels; ++l) {
            outer.append(chart.categoryLevels.value(l - 1));
        }
        cat->appendElement(multiLevelReference(
            ChartWorkbook::reference(layout.sheet, layout.levelCell(layout.levels - 1, 0), layout.categoryCell(qMax(0, count - 1))),
            categories, outer));
    } else {
        cat->appendElement(reference(numericCategories, range([&](int k) { return layout.categoryCell(k); }), categories,
                                     shape.categoryFormat));
    }
    XmlElementPtr val = make(scatterLike ? QStringLiteral("yVal") : QStringLiteral("val"));
    val->appendElement(reference(true, range([&](int k) { return layout.valueCell(index, k); }),
                                 numberPoints(series.values, count), shape.valueFormat));
    replaceChild(ser, cat->name, cat, {u"val", u"yVal", u"shape", u"smooth", u"bubbleSize", u"bubble3D", u"extLst"});
    replaceChild(ser, val->name, val, {u"shape", u"smooth", u"bubbleSize", u"bubble3D", u"extLst"});
}

} // namespace

bool patchPlots(const QList<XmlElement *> &plots, const ChartData &chart, const DataShape &shape)
{
    int nextIdx = 0;
    for (XmlElement *plot : plots) {
        for (const XmlElement *ser : plot->elements(nsC(), u"ser")) {
            if (const XmlElement *idx = ser->child(nsC(), u"idx")) {
                nextIdx = qMax(nextIdx, idx->attr(u"val").toInt() + 1);
            }
        }
    }
    // The file's points where their categories are now.
    QHash<int, int> moved;
    const int points = int(ChartEdit::tableOf(chart).categories.size());
    for (int k = 0; k < points; ++k) {
        const int from = chart.categorySource.isEmpty() ? k : chart.categorySource.value(k, -1);
        if (from >= 0) {
            moved.insert(from, k);
        }
    }
    QHash<int, XmlElement *> placed; // model series → its c:ser
    bool renumber = false;
    for (int p = 0; p < plots.size(); ++p) {
        XmlElement &plot = *plots.at(p);
        QHash<int, XmlElementPtr> byIdx;
        qsizetype at = -1;
        XmlElementPtr last;
        for (qsizetype i = 0; i < plot.children.size(); ++i) {
            const XmlChild &c = plot.children.at(i);
            if (c.element && c.element->is(nsC(), u"ser")) {
                const XmlElement *idx = c.element->child(nsC(), u"idx");
                byIdx.insert(idx != nullptr ? idx->attr(u"val").toInt() : -1 - int(i), c.element);
                at = at < 0 ? i : at;
                last = c.element;
            }
        }
        QList<XmlElementPtr> ordered;
        for (int s = 0; s < chart.series.size(); ++s) {
            const ChartSeries &series = chart.series.at(s);
            if (qBound(0, series.plot, int(plots.size()) - 1) != p) {
                continue;
            }
            XmlElementPtr ser = series.sourceIdx >= 0 ? byIdx.take(series.sourceIdx) : nullptr;
            if (!ser) {
                if (!last) {
                    return false; // nothing to copy: the caller writes a picture
                }
                // Added: a copy of the plot's last series, in its own colour.
                ser = last->clone();
                ser->children.removeIf([](const XmlChild &c) { return c.element && c.element->name == u"dPt"; });
                if (XmlElement *labels = ser->child(nsC(), u"dLbls")) {
                    labels->children.removeIf([](const XmlChild &c) { return c.element && c.element->name == u"dLbl"; });
                }
                replaceChild(*ser, QStringLiteral("idx"), valued(QStringLiteral("idx"), QString::number(nextIdx++)), {u"order"});
                if (XmlElement *spPr = ser->child(nsC(), u"spPr")) {
                    advanceAccent(*spPr, s);
                }
                if (XmlElement *marker = ser->child(nsC(), u"marker")) {
                    advanceAccent(*marker, s);
                }
                recolor(*ser, series.color);
                renumber = true;
            } else {
                movePoints(*ser, moved);
            }
            placed.insert(s, ser.get());
            ordered.append(ser);
        }
        renumber = renumber || !byIdx.isEmpty(); // removed series
        // The plot's series in the data's order, where they stood.
        plot.children.removeIf([](const XmlChild &c) { return c.element && c.element->is(nsC(), u"ser"); });
        if (at < 0) {
            continue;
        }
        for (qsizetype i = 0; i < ordered.size(); ++i) {
            plot.children.insert(qMin(at + i, plot.children.size()), {ordered.at(i), QString()});
        }
    }
    for (auto it = placed.cbegin(); it != placed.cend(); ++it) {
        XmlElement &ser = *it.value();
        if (renumber) {
            replaceChild(ser, QStringLiteral("order"), valued(QStringLiteral("order"), QString::number(it.key())), {u"tx"});
        }
        const XmlElement *plot = nullptr;
        for (XmlElement *candidate : plots) {
            for (const XmlElement *child : candidate->elements(nsC(), u"ser")) {
                plot = child == &ser ? candidate : plot;
            }
        }
        const bool scatterLike = plot != nullptr && (plot->name == u"scatterChart" || plot->name == u"bubbleChart");
        patchSeries(ser, it.key(), chart, shape, scatterLike);
    }
    return true;
}

} // namespace QindaOffice::Show::Pptx::ChartXml
