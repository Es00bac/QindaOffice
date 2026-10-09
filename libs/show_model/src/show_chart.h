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
    bool operator==(const ChartSeries &other) const;
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
    bool operator==(const ChartData &) const = default;
    [[nodiscard]] bool isEmpty() const { return series.isEmpty(); }
};

} // namespace QindaOffice::Show
