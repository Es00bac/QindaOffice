// SPDX-License-Identifier: GPL-3.0-or-later
#include "chart_edit.h"

#include <QLocale>
#include <QSet>

#include <cmath>
#include <limits>

namespace QindaOffice::Show::ChartEdit {

namespace {

constexpr qreal kGap = std::numeric_limits<qreal>::quiet_NaN();

bool sameNumbers(const QList<qreal> &a, const QList<qreal> &b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (qsizetype i = 0; i < a.size(); ++i) {
        const bool gapA = std::isnan(a.at(i));
        const bool gapB = std::isnan(b.at(i));
        if (gapA != gapB || (!gapA && a.at(i) != b.at(i))) {
            return false;
        }
    }
    return true;
}

QString defaultGrouping(const QString &type)
{
    if (type == u"column" || type == u"bar") {
        return QStringLiteral("clustered");
    }
    if (type == u"line" || type == u"area") {
        return QStringLiteral("standard");
    }
    return {};
}

// An outer category level after the rows moved: each row keeps its
// group, and a group's label stands at its first row (M82). `source` maps
// each new row to an old one (-1: added — it joins the group above).
QStringList regroup(const QStringList &level, const QList<int> &source)
{
    QList<int> starts(level.size(), -1);
    int start = -1;
    for (qsizetype k = 0; k < level.size(); ++k) {
        if (!level.at(k).isEmpty() || start < 0) {
            start = int(k);
        }
        starts[k] = start;
    }
    QStringList out;
    int previous = -1;
    for (qsizetype k = 0; k < source.size(); ++k) {
        const int from = source.at(k);
        const int group = from >= 0 && from < starts.size() ? starts.at(from) : (k > 0 ? previous : -1);
        out.append(group >= 0 && (k == 0 || group != previous) ? level.value(group) : QString());
        previous = group;
    }
    return out;
}

// Point colours after the rows moved; trailing "no colour of its own"
// entries go, as the reader leaves them out.
QList<QColor> movedColors(const QList<QColor> &colors, const QList<int> &source)
{
    QList<QColor> out;
    for (const int from : source) {
        out.append(from >= 0 ? colors.value(from) : QColor());
    }
    while (!out.isEmpty() && !out.last().isValid()) {
        out.removeLast();
    }
    return out;
}

} // namespace

bool ChartTable::operator==(const ChartTable &other) const
{
    if (categories != other.categories || series != other.series || values.size() != other.values.size()) {
        return false;
    }
    for (qsizetype i = 0; i < values.size(); ++i) {
        if (!sameNumbers(values.at(i), other.values.at(i))) {
            return false;
        }
    }
    return true;
}

ChartTable tableOf(const ChartData &chart)
{
    ChartTable table;
    qsizetype count = 0;
    for (const ChartSeries &series : chart.series) {
        count = qMax(count, qMax(series.values.size(), series.categories.size()));
        if (series.categories.size() > table.categories.size()) {
            table.categories = series.categories;
        }
    }
    while (table.categories.size() < count) {
        table.categories.append(QString());
    }
    for (const ChartSeries &series : chart.series) {
        table.series.append(series.name);
        QList<qreal> values = series.values;
        while (values.size() < count) {
            values.append(kGap);
        }
        table.values.append(values);
    }
    for (int k = 0; k < table.categories.size(); ++k) {
        table.categorySource.append(k);
    }
    for (int s = 0; s < table.series.size(); ++s) {
        table.seriesSource.append(s);
    }
    return table;
}

void setTable(ChartData &chart, const ChartTable &table)
{
    const qsizetype count = table.categories.size();
    const qsizetype before = tableOf(chart).categories.size();
    // Where each row came from: the table's sources, else by position.
    QList<int> rows;
    for (qsizetype k = 0; k < count; ++k) {
        const int from = table.categorySource.isEmpty() ? (k < before ? int(k) : -1) : table.categorySource.value(k, -1);
        rows.append(from >= 0 && from < before ? from : -1);
    }
    QList<ChartSeries> out;
    QSet<QString> seen;
    for (qsizetype s = 0; s < table.series.size(); ++s) {
        const int from = table.seriesSource.isEmpty() ? int(s) : table.seriesSource.value(s, -1);
        ChartSeries series;
        if (from >= 0 && from < chart.series.size()) {
            series = chart.series.at(from);
            series.pointColors = movedColors(series.pointColors, rows);
        } else {
            // Added: in the plot of the series before it (Edit Data in a
            // combo chart extends the plot it is next to).
            series.plot = !out.isEmpty() ? out.last().plot : (chart.series.isEmpty() ? 0 : chart.series.last().plot);
        }
        // An Excel table header: never empty, unique without regard to case.
        QString name = table.series.at(s).trimmed().isEmpty() ? QStringLiteral("Series %1").arg(s + 1) : table.series.at(s);
        QString unique = name;
        for (int n = 2; seen.contains(unique.toUpper()); ++n) {
            unique = name + QString::number(n);
        }
        seen.insert(unique.toUpper());
        series.name = unique;
        series.categories = table.categories;
        series.values = s < table.values.size() ? table.values.at(s) : QList<qreal>();
        series.values.resize(count, kGap);
        series.xValues.clear();
        if (chart.type == u"scatter") {
            for (const QString &x : table.categories) {
                series.xValues.append(parseValue(x));
            }
        }
        out.append(series);
    }
    chart.series = out;
    // The categories' outer levels and their place in the file follow the rows.
    for (QStringList &level : chart.categoryLevels) {
        level = regroup(level, rows);
    }
    QList<int> sources;
    for (const int from : std::as_const(rows)) {
        sources.append(from < 0 ? -1 : (chart.categorySource.isEmpty() ? from : chart.categorySource.value(from, -1)));
    }
    chart.categorySource = sources;
}

const QList<ChartKind> &kinds()
{
    static const QList<ChartKind> list{
        {QStringLiteral("column"), QStringLiteral("clustered"), QT_TRANSLATE_NOOP("ChartEdit", "Clustered column")},
        {QStringLiteral("column"), QStringLiteral("stacked"), QT_TRANSLATE_NOOP("ChartEdit", "Stacked column")},
        {QStringLiteral("column"), QStringLiteral("percentStacked"), QT_TRANSLATE_NOOP("ChartEdit", "100% stacked column")},
        {QStringLiteral("bar"), QStringLiteral("clustered"), QT_TRANSLATE_NOOP("ChartEdit", "Clustered bar")},
        {QStringLiteral("bar"), QStringLiteral("stacked"), QT_TRANSLATE_NOOP("ChartEdit", "Stacked bar")},
        {QStringLiteral("bar"), QStringLiteral("percentStacked"), QT_TRANSLATE_NOOP("ChartEdit", "100% stacked bar")},
        {QStringLiteral("line"), QStringLiteral("standard"), QT_TRANSLATE_NOOP("ChartEdit", "Line")},
        {QStringLiteral("line"), QStringLiteral("stacked"), QT_TRANSLATE_NOOP("ChartEdit", "Stacked line")},
        {QStringLiteral("area"), QStringLiteral("standard"), QT_TRANSLATE_NOOP("ChartEdit", "Area")},
        {QStringLiteral("area"), QStringLiteral("stacked"), QT_TRANSLATE_NOOP("ChartEdit", "Stacked area")},
        {QStringLiteral("area"), QStringLiteral("percentStacked"), QT_TRANSLATE_NOOP("ChartEdit", "100% stacked area")},
        {QStringLiteral("pie"), QString(), QT_TRANSLATE_NOOP("ChartEdit", "Pie")},
        {QStringLiteral("doughnut"), QString(), QT_TRANSLATE_NOOP("ChartEdit", "Doughnut")},
        {QStringLiteral("scatter"), QString(), QT_TRANSLATE_NOOP("ChartEdit", "Scatter")},
    };
    return list;
}

int kindIndex(const ChartData &chart)
{
    return plotKindIndex(chart, 0);
}

int plotKindIndex(const ChartData &chart, int plot)
{
    const ChartPlot kind = chart.plotAt(plot);
    const QString grouping = kind.grouping.isEmpty() ? defaultGrouping(kind.type) : kind.grouping;
    const QList<ChartKind> &all = kinds();
    for (int i = 0; i < all.size(); ++i) {
        if (all.at(i).type == kind.type && (all.at(i).grouping.isEmpty() || all.at(i).grouping == grouping)) {
            return i;
        }
    }
    return -1;
}

bool comboKind(int index)
{
    const QList<ChartKind> &all = kinds();
    return index >= 0 && index < all.size() && all.at(index).type != u"pie" && all.at(index).type != u"doughnut"
        && all.at(index).type != u"scatter";
}

void setPlotKind(ChartData &chart, int plot, int index)
{
    const QList<ChartKind> &all = kinds();
    if (chart.plots.size() <= 1) {
        setKind(chart, index); // one plot: the chart's own kind
        return;
    }
    if (plot < 0 || plot >= chart.plots.size() || !comboKind(index)) {
        return;
    }
    chart.plots[plot] = ChartPlot{all.at(index).type, all.at(index).grouping};
    if (plot == 0) {
        chart.type = all.at(index).type;
        chart.grouping = all.at(index).grouping;
    }
}

void setKind(ChartData &chart, int index)
{
    const QList<ChartKind> &all = kinds();
    if (index < 0 || index >= all.size()) {
        return;
    }
    if (chart.plots.size() > 1) {
        setPlotKind(chart, 0, index); // a combo chart: its first plot
        return;
    }
    const bool toScatter = all.at(index).type == u"scatter";
    const bool fromScatter = chart.type == u"scatter";
    // M82 (D-091): series keep their per-point colours through a kind
    // change — the writer carries c:dPt and c:dLbls into the new plot.
    chart.type = all.at(index).type;
    chart.grouping = all.at(index).grouping;
    if (toScatter != fromScatter) {
        // Scatter keeps its x values as numbers; the others as labels.
        for (ChartSeries &series : chart.series) {
            series.xValues.clear();
            if (toScatter) {
                for (const QString &x : series.categories) {
                    series.xValues.append(parseValue(x));
                }
            }
        }
    }
}

qreal parseValue(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return kGap;
    }
    bool ok = false;
    double value = trimmed.toDouble(&ok);
    if (!ok) {
        value = QLocale().toDouble(trimmed, &ok);
    }
    return ok && std::isfinite(value) ? value : kGap;
}

QString valueText(qreal value)
{
    return std::isnan(value) ? QString() : QString::number(value, 'g', QLocale::FloatingPointShortest);
}

} // namespace QindaOffice::Show::ChartEdit
