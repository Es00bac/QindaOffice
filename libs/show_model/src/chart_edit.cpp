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
    return table;
}

void setTable(ChartData &chart, const ChartTable &table)
{
    const qsizetype count = table.categories.size();
    QList<ChartSeries> out;
    QSet<QString> seen;
    for (qsizetype s = 0; s < table.series.size(); ++s) {
        ChartSeries series = s < chart.series.size() ? chart.series.at(s) : ChartSeries();
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
        if (series.pointColors.size() > count) {
            series.pointColors.resize(count);
        }
        out.append(series);
    }
    chart.series = out;
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
    const QString grouping = chart.grouping.isEmpty() ? defaultGrouping(chart.type) : chart.grouping;
    const QList<ChartKind> &all = kinds();
    for (int i = 0; i < all.size(); ++i) {
        if (all.at(i).type == chart.type && (all.at(i).grouping.isEmpty() || all.at(i).grouping == grouping)) {
            return i;
        }
    }
    return -1;
}

void setKind(ChartData &chart, int index)
{
    const QList<ChartKind> &all = kinds();
    if (index < 0 || index >= all.size()) {
        return;
    }
    const bool toScatter = all.at(index).type == u"scatter";
    const bool fromScatter = chart.type == u"scatter";
    // Another plot element (bars and columns share one) is a new plot in
    // the file: per-point colours do not survive it (pptx_write_chart_kind.cpp).
    const auto family = [](const QString &type) { return type == u"column" ? QStringLiteral("bar") : type; };
    if (family(chart.type) != family(all.at(index).type)) {
        for (ChartSeries &series : chart.series) {
            series.pointColors.clear();
        }
    }
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
