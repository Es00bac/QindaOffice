// SPDX-License-Identifier: GPL-3.0-or-later
// A chart kind change for the pptx writer (pptx_write_chart.cpp; M67,
// D-076): a plot of the plot area rebuilt as one of the kinds QindaShow
// draws (show_model/chart_edit.h). Within one plot element only its
// options change (column ↔ bar, clustered ↔ stacked); across elements the
// plot is rebuilt in the new element's schema order, each series keeping
// its index, name and colour — moved from fill to line or back as the new
// kind draws it — and the plot area gets the axes the kind needs: none for
// pie and doughnut, two value axes for scatter, a category and a value
// axis otherwise (the existing ones kept where their kind fits). The other
// plots of a combo chart keep theirs.
// M82 (D-091): per-point colours (c:dPt) and data labels (the series' and
// the plot's c:dLbls, each point's c:dLbl) come along; a label position the
// new kind may not take is dropped (ChartXml::labelsForKind).

#include "pptx_write_chart_internal.h"

namespace QindaOffice::Show::Pptx {

namespace {

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

bool isAxis(const XmlElement &e)
{
    return e.ns == nsC() && (e.name == u"catAx" || e.name == u"valAx" || e.name == u"dateAx" || e.name == u"serAx");
}

bool isPlot(const XmlElement &e)
{
    return e.ns == nsC() && e.name.endsWith(QLatin1String("Chart"));
}

QString elementFor(const QString &type)
{
    if (type == u"column" || type == u"bar") {
        return QStringLiteral("barChart");
    }
    if (type == u"line" || type == u"area" || type == u"pie" || type == u"doughnut" || type == u"scatter") {
        return type + QStringLiteral("Chart");
    }
    return {};
}

QString grouping(const ChartData &chart)
{
    if (!chart.grouping.isEmpty()) {
        return chart.grouping;
    }
    return chart.type == u"column" || chart.type == u"bar" ? QStringLiteral("clustered") : QStringLiteral("standard");
}

// The colour a series is drawn in: its fill's, else its line's.
XmlElementPtr seriesColor(const XmlElement *spPr)
{
    if (spPr == nullptr) {
        return nullptr;
    }
    const XmlElement *fill = spPr->child(nsA(), u"solidFill");
    if (fill == nullptr) {
        // A gradient is drawn in its first stop's colour (as the reader reads it).
        const XmlElement *gradient = spPr->child(nsA(), u"gradFill");
        const XmlElement *stops = gradient != nullptr ? gradient->child(nsA(), u"gsLst") : nullptr;
        fill = stops != nullptr ? stops->child(nsA(), u"gs") : nullptr;
    }
    if (fill == nullptr) {
        const XmlElement *ln = spPr->child(nsA(), u"ln");
        fill = ln != nullptr ? ln->child(nsA(), u"solidFill") : nullptr;
    }
    const QList<const XmlElement *> colors = fill != nullptr ? fill->elements() : QList<const XmlElement *>();
    return colors.isEmpty() ? nullptr : colors.first()->clone();
}

XmlElementPtr seriesLook(const QString &type, const XmlElementPtr &color)
{
    if (!color) {
        return nullptr;
    }
    XmlElementPtr spPr = make(QStringLiteral("spPr"));
    if (type == u"line" || type == u"scatter") {
        XmlElement &ln = spPr->appendElement(nsA(), QStringLiteral("ln"));
        ln.setAttribute(QString(), QStringLiteral("w"), QStringLiteral("28575"));
        ln.setAttribute(QString(), QStringLiteral("cap"), QStringLiteral("rnd"));
        ln.appendElement(nsA(), QStringLiteral("solidFill")).appendElement(color);
        ln.appendElement(nsA(), QStringLiteral("round"));
    } else {
        spPr->appendElement(nsA(), QStringLiteral("solidFill")).appendElement(color);
        spPr->appendElement(nsA(), QStringLiteral("ln")).appendElement(nsA(), QStringLiteral("noFill"));
    }
    return spPr;
}

// A series of the new kind: index, order, name, colour, per-point colours
// and data labels kept; its data elements are empty here and filled by the
// caller (ChartXml::patchPlots).
XmlElementPtr convertSeries(const XmlElement &ser, const QString &type)
{
    XmlElementPtr out = make(QStringLiteral("ser"));
    for (const char16_t *name : {u"idx", u"order", u"tx"}) {
        if (const XmlElement *child = ser.child(nsC(), name)) {
            out->appendElement(child->clone());
        }
    }
    if (XmlElementPtr spPr = seriesLook(type, seriesColor(ser.child(nsC(), u"spPr")))) {
        out->appendElement(spPr);
    }
    if (type == u"column" || type == u"bar") {
        out->appendElement(valued(QStringLiteral("invertIfNegative"), QStringLiteral("0")));
    } else if (type == u"line" || type == u"scatter") {
        XmlElementPtr marker = make(QStringLiteral("marker"));
        marker->appendElement(valued(QStringLiteral("symbol"), type == u"line" ? QStringLiteral("none") : QStringLiteral("circle")));
        if (type == u"scatter") {
            marker->appendElement(valued(QStringLiteral("size"), QStringLiteral("5")));
        }
        out->appendElement(marker);
    }
    for (const XmlElement *point : ser.elements(nsC(), u"dPt")) {
        out->appendElement(point->clone());
    }
    if (const XmlElement *labels = ser.child(nsC(), u"dLbls")) {
        out->appendElement(ChartXml::labelsForKind(*labels));
    }
    const bool scatter = type == u"scatter";
    out->appendElement(make(scatter ? QStringLiteral("xVal") : QStringLiteral("cat")));
    out->appendElement(make(scatter ? QStringLiteral("yVal") : QStringLiteral("val")));
    if (type == u"line" || scatter) {
        out->appendElement(valued(QStringLiteral("smooth"), QStringLiteral("0")));
    }
    return out;
}

XmlElementPtr newPlot(const ChartData &chart, const QList<XmlElementPtr> &series, const XmlElement *labels)
{
    const QString &type = chart.type;
    XmlElementPtr plot = make(elementFor(type));
    const bool bars = type == u"column" || type == u"bar";
    if (bars) {
        plot->appendElement(valued(QStringLiteral("barDir"), type == u"bar" ? QStringLiteral("bar") : QStringLiteral("col")));
    }
    if (bars || type == u"line" || type == u"area") {
        plot->appendElement(valued(QStringLiteral("grouping"), grouping(chart)));
    } else if (type == u"scatter") {
        plot->appendElement(valued(QStringLiteral("scatterStyle"), QStringLiteral("lineMarker")));
    }
    plot->appendElement(valued(QStringLiteral("varyColors"), type == u"pie" || type == u"doughnut" ? QStringLiteral("1") : QStringLiteral("0")));
    for (const XmlElementPtr &ser : series) {
        plot->appendElement(ser);
    }
    if (labels != nullptr) {
        plot->appendElement(ChartXml::labelsForKind(*labels)); // the plot's data labels (M82)
    }
    if (bars) {
        plot->appendElement(valued(QStringLiteral("gapWidth"), QStringLiteral("150")));
        if (grouping(chart) != u"clustered") {
            plot->appendElement(valued(QStringLiteral("overlap"), QStringLiteral("100")));
        }
    } else if (type == u"line") {
        plot->appendElement(valued(QStringLiteral("marker"), QStringLiteral("1")));
    } else if (type == u"pie" || type == u"doughnut") {
        plot->appendElement(valued(QStringLiteral("firstSliceAng"), QStringLiteral("0")));
        if (type == u"doughnut") {
            plot->appendElement(valued(QStringLiteral("holeSize"), QStringLiteral("50")));
        }
    }
    return plot;
}

struct AxisNeed {
    QString element; // catAx / valAx
    QString position;
};

QList<AxisNeed> axesFor(const QString &type)
{
    if (type == u"pie" || type == u"doughnut") {
        return {};
    }
    if (type == u"scatter") {
        return {{QStringLiteral("valAx"), QStringLiteral("b")}, {QStringLiteral("valAx"), QStringLiteral("l")}};
    }
    if (type == u"bar") {
        return {{QStringLiteral("catAx"), QStringLiteral("l")}, {QStringLiteral("valAx"), QStringLiteral("b")}};
    }
    return {{QStringLiteral("catAx"), QStringLiteral("b")}, {QStringLiteral("valAx"), QStringLiteral("l")}};
}

XmlElementPtr newAxis(const AxisNeed &need, const QString &id, const QString &cross, bool scatter)
{
    XmlElementPtr axis = make(need.element);
    axis->appendElement(valued(QStringLiteral("axId"), id));
    axis->appendElement(make(QStringLiteral("scaling"))).appendElement(valued(QStringLiteral("orientation"), QStringLiteral("minMax")));
    axis->appendElement(valued(QStringLiteral("delete"), QStringLiteral("0")));
    axis->appendElement(valued(QStringLiteral("axPos"), need.position));
    const bool value = need.element == u"valAx";
    if (value && (need.position == u"l" || need.position == u"r")) {
        axis->appendElement(make(QStringLiteral("majorGridlines")));
    }
    XmlElement &format = axis->appendElement(nsC(), QStringLiteral("numFmt"));
    format.setAttribute(QString(), QStringLiteral("formatCode"), QStringLiteral("General"));
    format.setAttribute(QString(), QStringLiteral("sourceLinked"), QStringLiteral("1"));
    axis->appendElement(valued(QStringLiteral("majorTickMark"), QStringLiteral("none")));
    axis->appendElement(valued(QStringLiteral("minorTickMark"), QStringLiteral("none")));
    axis->appendElement(valued(QStringLiteral("tickLblPos"), QStringLiteral("nextTo")));
    axis->appendElement(valued(QStringLiteral("crossAx"), cross));
    axis->appendElement(valued(QStringLiteral("crosses"), QStringLiteral("autoZero")));
    if (value) {
        axis->appendElement(valued(QStringLiteral("crossBetween"), scatter ? QStringLiteral("midCat") : QStringLiteral("between")));
    } else {
        axis->appendElement(valued(QStringLiteral("auto"), QStringLiteral("1")));
        axis->appendElement(valued(QStringLiteral("lblAlgn"), QStringLiteral("ctr")));
        axis->appendElement(valued(QStringLiteral("lblOffset"), QStringLiteral("100")));
        axis->appendElement(valued(QStringLiteral("noMultiLvlLbl"), QStringLiteral("0")));
    }
    return axis;
}

XmlElement *axisWithId(XmlElement &plotArea, const QString &id)
{
    for (XmlChild &c : plotArea.children) {
        if (c.element && isAxis(*c.element)) {
            const XmlElement *axId = c.element->child(nsC(), u"axId");
            if (axId != nullptr && axId->attr(u"val") == id) {
                return c.element.get();
            }
        }
    }
    return nullptr;
}

void setValue(XmlElement &parent, const QString &name, const QString &value, const QList<QStringView> &before)
{
    if (XmlElement *child = parent.child(nsC(), name)) {
        child->setAttribute(QString(), QStringLiteral("val"), value);
    } else {
        parent.insertElementBefore(valued(name, value), before);
    }
}

// The plot's axes made what `type` needs; axes no plot names any more go.
void fitAxes(XmlElement &plotArea, XmlElement &plot, const QString &type, QStringList ids)
{
    const QList<AxisNeed> needs = axesFor(type);
    plot.removeChildren(nsC(), u"axId");
    QStringList chosen;
    qint64 next = 0;
    for (const XmlChild &c : plotArea.children) {
        if (c.element && isAxis(*c.element)) {
            const XmlElement *axId = c.element->child(nsC(), u"axId");
            next = qMax(next, axId != nullptr ? axId->attr(u"val").toLongLong() + 1 : next);
        }
    }
    next = qMax<qint64>(next, 50000001);
    for (const AxisNeed &need : needs) {
        // An axis of the plot whose kind fits, else any axis of the plot,
        // else a new id.
        QString id;
        for (const QString &candidate : std::as_const(ids)) {
            const XmlElement *axis = axisWithId(plotArea, candidate);
            const QString kind = axis != nullptr ? (axis->name == u"dateAx" ? QStringLiteral("catAx") : axis->name) : QString();
            if (kind == need.element) {
                id = candidate;
                break;
            }
        }
        if (id.isEmpty() && !ids.isEmpty()) {
            id = ids.first();
        }
        if (id.isEmpty()) {
            id = QString::number(next++);
        }
        ids.removeAll(id);
        chosen.append(id);
    }
    for (int i = 0; i < needs.size(); ++i) {
        const QString cross = chosen.value(1 - i);
        XmlElement *axis = axisWithId(plotArea, chosen.at(i));
        const bool fits = axis != nullptr && (axis->name == needs.at(i).element || (axis->name == u"dateAx" && needs.at(i).element == u"catAx"));
        if (fits) {
            setValue(*axis, QStringLiteral("axPos"), needs.at(i).position, {u"majorGridlines", u"minorGridlines", u"title", u"numFmt",
                                                                              u"majorTickMark", u"minorTickMark", u"tickLblPos", u"spPr",
                                                                              u"txPr", u"crossAx", u"extLst"});
            setValue(*axis, QStringLiteral("crossAx"), cross, {u"crosses", u"crossesAt", u"crossBetween", u"auto", u"extLst"});
            if (axis->name == u"valAx") {
                setValue(*axis, QStringLiteral("crossBetween"), type == u"scatter" ? QStringLiteral("midCat") : QStringLiteral("between"),
                         {u"majorUnit", u"minorUnit", u"dispUnits", u"extLst"});
            }
            continue;
        }
        XmlElementPtr fresh = newAxis(needs.at(i), chosen.at(i), cross, type == u"scatter");
        bool replaced = false;
        for (XmlChild &c : plotArea.children) {
            if (c.element.get() == axis && axis != nullptr) {
                c.element = fresh;
                replaced = true;
            }
        }
        if (!replaced) {
            plotArea.insertElementBefore(fresh, {u"dTable", u"spPr", u"extLst"});
        }
    }
    for (const QString &id : std::as_const(chosen)) {
        plot.insertElementBefore(valued(QStringLiteral("axId"), id), {u"extLst"});
    }
    // Axes no plot refers to any more.
    QSet<QString> referenced;
    for (const XmlChild &c : plotArea.children) {
        if (c.element && isPlot(*c.element)) {
            for (const XmlElement *axId : c.element->elements(nsC(), u"axId")) {
                referenced.insert(axId->attr(u"val"));
            }
        }
    }
    plotArea.children.removeIf([&referenced](const XmlChild &c) {
        if (!c.element || !isAxis(*c.element)) {
            return false;
        }
        const XmlElement *axId = c.element->child(nsC(), u"axId");
        return axId == nullptr || !referenced.contains(axId->attr(u"val"));
    });
}

} // namespace

XmlElement *convertPlot(XmlElement &plotArea, XmlElement *plot, const ChartData &chart)
{
    const QString target = elementFor(chart.type);
    if (target.isEmpty() || plot == nullptr) {
        return nullptr;
    }
    QStringList ids;
    for (const XmlElement *axId : plot->elements(nsC(), u"axId")) {
        ids.append(axId->attr(u"val"));
    }
    XmlElement *result = plot;
    if (plot->name == target) {
        if (target == u"barChart") {
            setValue(*plot, QStringLiteral("barDir"), chart.type == u"bar" ? QStringLiteral("bar") : QStringLiteral("col"), {u"grouping"});
            plot->removeChildren(nsC(), u"overlap");
            if (grouping(chart) != u"clustered") {
                plot->insertElementBefore(valued(QStringLiteral("overlap"), QStringLiteral("100")), {u"serLines", u"axId", u"extLst"});
            }
        }
        if (target == u"barChart" || target == u"lineChart" || target == u"areaChart") {
            setValue(*plot, QStringLiteral("grouping"), grouping(chart), {u"varyColors", u"ser", u"dLbls", u"axId", u"extLst"});
        }
        // M82: a stacked bar takes no label outside its end; the labels
        // keep their content and take the kind's default place.
        for (XmlChild &c : plot->children) {
            XmlElement *labels = c.element && c.element->is(nsC(), u"dLbls") ? c.element.get()
                : c.element && c.element->is(nsC(), u"ser")                ? c.element->child(nsC(), u"dLbls")
                                                                            : nullptr;
            if (labels != nullptr) {
                *labels = *ChartXml::labelsForKind(*labels);
            }
        }
    } else {
        QList<XmlElementPtr> series;
        for (const XmlElement *ser : plot->elements(nsC(), u"ser")) {
            series.append(convertSeries(*ser, chart.type));
        }
        XmlElementPtr fresh = newPlot(chart, series, plot->child(nsC(), u"dLbls"));
        for (XmlChild &c : plotArea.children) {
            if (c.element.get() == plot) {
                c.element = fresh;
            }
        }
        result = fresh.get();
    }
    fitAxes(plotArea, *result, chart.type, ids);
    return result;
}

} // namespace QindaOffice::Show::Pptx
