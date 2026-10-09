// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Charts (M62, D-070): what QindaShow needs to DRAW a chart — its type,
// grouping, title, legend and the series' cached values (c:numCache /
// c:strCache). The chart part itself is never re-derived: the graphic
// frame stays preserved XML (Shape::preserved) and, while the data is
// unchanged, the writer carries the chart part, its embedded workbook and
// style parts across verbatim. M67 (D-076): edited data (chart_edit.h) is
// written into a COPY of the source chart part — formulas, caches, series,
// the kind — and into its embedded workbook through calc_io
// (pptx_write_chart.cpp, chart_workbook.h).
// M82 (D-091): every plot of the plot area (a combo chart's columns and
// lines), each series' identity in the file and its plot, and the outer
// levels of multi-level categories — so Edit Data and a kind change keep
// what the file had: both plots, the category groups, per-point colours
// and data labels.

#include <QColor>
#include <QList>
#include <QString>
#include <QStringList>

namespace QindaOffice::Show {

struct ChartSeries {
    QString name;
    QStringList categories; // category labels (or x values as text)
    QList<qreal> values;    // y values (NaN = a gap)
    QList<qreal> xValues;   // scatter x values
    QColor color;           // invalid = the palette's
    QList<QColor> pointColors; // per-point (pie slices); invalid entries = palette
    int plot = 0;           // M82: its plot (ChartData::plots)
    int sourceIdx = -1;     // M82: its c:idx in the file (-1: added here)
    // The content: what the series shows (where it came from, sourceIdx,
    // is the writer's bookkeeping, not part of it).
    bool operator==(const ChartSeries &other) const;
};

// M82: one plot of a chart's plot area (c:barChart, c:lineChart, …).
struct ChartPlot {
    QString type;     // as ChartData::type
    QString grouping; // as ChartData::grouping
    bool operator==(const ChartPlot &) const = default;
};

struct ChartData {
    // "column", "bar" (horizontal), "line", "area", "pie", "doughnut",
    // "scatter", or "" when the plot type is one QindaShow cannot draw.
    QString type;
    QString grouping; // "clustered", "stacked", "percentStacked", "standard"
    QString title;
    bool legend = false;
    QString legendPosition; // "r", "b", "t", "l", "tr"
    QList<ChartSeries> series;
    QList<QColor> palette;  // accent1..6 of the deck's theme, resolved
    // ---- M82 (D-091) ----
    // Every plot, the first one's type and grouping being `type` and
    // `grouping` above; empty: one plot (a chart from before M82, or made
    // here).
    QList<ChartPlot> plots;
    // Multi-level categories (c:multiLvlStrRef): the levels outside the
    // series' categories, the nearest first, as the file lists them — each
    // holds a group's label at the group's first point, "" after it.
    QList<QStringList> categoryLevels;
    // The point each category was read as (-1: added here): the writer
    // keeps a point's own colour and data label with it (pptx_write_chart.cpp).
    QList<int> categorySource;
    // The content (categorySource, like ChartSeries::sourceIdx, is not).
    bool operator==(const ChartData &other) const;
    [[nodiscard]] bool isEmpty() const { return series.isEmpty(); }
    // The plot `index`, or the chart's own type when there is one plot.
    [[nodiscard]] ChartPlot plotAt(int index) const
    {
        return index >= 0 && index < plots.size() ? plots.at(index) : ChartPlot{type, grouping};
    }
    [[nodiscard]] int plotCount() const { return plots.isEmpty() ? 1 : int(plots.size()); }
};

} // namespace QindaOffice::Show
