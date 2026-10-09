// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Editing a chart's data and kind (M67, D-076): the chart as PowerPoint's
// Edit Data sheet shows it — categories down, series across — and the
// kinds the renderer draws (chart_painter.cpp). The pptx writer turns an
// edited ChartData into the chart part's caches and formulas and into its
// embedded workbook (pptx_write_chart.cpp, chart_workbook.h), so
// PowerPoint's own Edit Data opens on the same numbers.

#include "show_chart.h"

namespace QindaOffice::Show::ChartEdit {

// The data sheet of a chart. Scatter charts keep their x values in
// `categories` (as text) and their y values in `values`.
struct ChartTable {
    QStringList categories;     // one per row
    QStringList series;         // one per column
    QList<QList<qreal>> values; // [series][category]; NaN = an empty cell
    // M82 (D-091): the chart's category / series each row / column came
    // from (-1: added). Empty: by position, as before M82.
    QList<int> categorySource;
    QList<int> seriesSource;
    // The data only (the sources are where it came from, not what it is).
    bool operator==(const ChartTable &other) const;
};

[[nodiscard]] ChartTable tableOf(const ChartData &chart);
// `chart` with `table`'s data. Each series and category keeps what it had
// — a series its colour, its plot and its place in the file, a category
// its points' own colours and data labels and its outer levels' group —
// by the table's sources (by position without them); an added series
// joins the plot of the series before it. Series names are made unique
// and never empty, as an Excel table header requires ("Series 3",
// "North2"), because the embedded workbook keeps them in one.
void setTable(ChartData &chart, const ChartTable &table);

// One kind the renderer draws: a type and its grouping.
struct ChartKind {
    QString type;     // ChartData::type
    QString grouping; // ChartData::grouping ("" where the type has none)
    const char *label;
};
[[nodiscard]] const QList<ChartKind> &kinds();
// The index of `chart`'s kind in kinds(), -1 when it is none of them.
[[nodiscard]] int kindIndex(const ChartData &chart);
// A kind change keeps the series' colours, per-point colours and data
// labels (the writer carries c:dPt and c:dLbls into the new plot).
void setKind(ChartData &chart, int index);
// M82 (D-091): a combo chart's plots one by one (plot 0 is the chart's
// own type). The kinds a combo's plot can take: those with axes.
[[nodiscard]] int plotKindIndex(const ChartData &chart, int plot);
void setPlotKind(ChartData &chart, int plot, int index);
[[nodiscard]] bool comboKind(int index);

// The number in a data cell's text; NaN when it is empty or not a number.
[[nodiscard]] qreal parseValue(const QString &text);
// A value as a data cell shows it ("" for NaN).
[[nodiscard]] QString valueText(qreal value);

} // namespace QindaOffice::Show::ChartEdit
