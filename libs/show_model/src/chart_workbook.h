// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The workbook a PowerPoint chart embeds (M67, D-076): PowerPoint's Edit
// Data opens it in Excel and re-reads the chart from its cells through the
// series formulas (c:f), so an edited chart must change the workbook too,
// not only the values the chart part caches. The workbook goes through
// calc_io (the suite's own xlsx reader and writer), keeping everything of
// it calc_io models — other cells, styles, the Excel table PowerPoint puts
// over the data — and writing the data where the formulas point.
//
// Internal to show_model's pptx writer (pptx_write_chart.cpp). The only
// show_model code that reaches calc_io.

#include "show_chart.h"

#include <QByteArray>

namespace QindaOffice::Show::ChartWorkbook {

struct Cell {
    int row = 0; // 0-based
    int column = 0;
};

// Where a chart's data sits, as its first series' formulas say: the sheet,
// and either series in columns (PowerPoint's default: categories down the
// category column from firstRow, series names along headerRow from
// firstSeriesColumn) or series in rows (after Switch Row/Column:
// categories along headerRow from firstSeriesColumn, names down the
// category column from firstRow).
struct Layout {
    QString sheet = QStringLiteral("Sheet1");
    int headerRow = 0;
    int categoryColumn = 0;
    int firstRow = 1;
    int firstSeriesColumn = 1;
    bool seriesInRows = false;
    bool categoriesNumeric = false; // the categories are a c:numRef (numbers, dates)

    [[nodiscard]] Cell categoryCell(int category) const;
    [[nodiscard]] Cell nameCell(int series) const;
    [[nodiscard]] Cell valueCell(int series, int category) const;
};

// The layout the formulas name; PowerPoint's default (Sheet1, A2 down,
// B1 across) for whatever they leave out.
[[nodiscard]] Layout layoutFrom(const QString &nameFormula, const QString &categoryFormula, const QString &valueFormula);

// A range as a series formula spells it: Sheet1!$A$2:$A$5 (one cell:
// Sheet1!$B$1), the sheet quoted when its name needs it.
[[nodiscard]] QString reference(const QString &sheet, Cell first, Cell last);

// The workbook bytes holding `chart`'s data at `layout`. `source` is the
// embedded workbook as read (empty, or one calc_io cannot read: a new
// workbook is made). The cells `previous` occupied that the new data does
// not are emptied, and an Excel table that held exactly the previous data
// holds the new data. Empty bytes with `error` set when it cannot be
// written.
[[nodiscard]] QByteArray write(const QByteArray &source, const ChartData &chart, const ChartData &previous,
                               const Layout &layout, QString *error = nullptr);

} // namespace QindaOffice::Show::ChartWorkbook
