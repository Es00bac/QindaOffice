// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Data ▸ What-If Analysis ▸ Data Table (M78, D-087): Excel's one- and
// two-variable data tables, {=TABLE(r,c)}. A table's result cells hold
// what its formula(s) give when the input cell takes each input value in
// turn; the engine works them out after every recalculation
// (Workbook::recomputeDataTables), so they follow their formulas and inputs
// as Excel's do. In the file the top-left result cell carries
// <f t="dataTable" ref dt2D dtr r1 r2/> and every result cell its value
// (calc_io).
//
// The layout, as in Excel (selection = the whole table):
//   one variable, values down a column (dtr="0"): inputs in the first
//     column below the formula row; formulas in the first row, right of the
//     inputs; results = the selection minus its first row and column.
//   one variable, values across a row (dtr="1"): inputs in the first row;
//     formulas in the first column, below them; results likewise.
//   two variables (dt2D="1"): the formula in the top-left corner, row
//     input values across the first row, column input values down the
//     first column; r1 is the ROW input cell, r2 the COLUMN input cell.

#include "value.h"

#include <QList>
#include <QString>

#include <optional>

namespace QindaOffice::Calc {

class Sheet;
class Workbook;

struct DataTableSpec {
    CellRange ref;          // the result cells (Excel's ref)
    bool twoD = false;      // dt2D
    bool rowInput = false;  // dtr: a one-variable table whose input values run across a row
    CellAddr r1;            // one variable: the input cell; two: the ROW input cell
    CellAddr r2;            // two variables: the COLUMN input cell
    bool r1Deleted = false; // del1: the input cell was deleted (#REF!)
    bool r2Deleted = false; // del2
    bool operator==(const DataTableSpec &) const = default;

    // The row just above `ref` (rows = true) or the column just left of it
    // (rows = false); invalid when `ref` touches row 1 or column A. They
    // hold the input values (across for a row or two-variable table, down
    // for a column or two-variable table) or the formulas (the row above a
    // column table's results, the column left of a row table's).
    [[nodiscard]] CellRange inputValues(bool rows) const;
};

// Makes a table from Excel's dialog: `selection` is the whole table
// (formulas, input values and results), `rowInput` / `colInput` the input
// cells on the same sheet (an invalid address: not given). Excel's rules
// and messages: one of the two is needed; both make a two-variable table;
// the selection must be at least 2×2. nullopt, with `error`, otherwise.
[[nodiscard]] std::optional<DataTableSpec> makeDataTable(const CellRange &selection, const CellAddr &rowInput,
                                                         const CellAddr &colInput, QString *error = nullptr);

// The formula bar's text for a result cell, as Excel shows it:
// "{=TABLE(B1,B2)}", "{=TABLE(,A1)}" (column input), "{=TABLE(A1,)}".
[[nodiscard]] QString dataTableFormulaText(const DataTableSpec &spec);

// The table whose results cover `addr` on `sheet`, or nullptr.
[[nodiscard]] const DataTableSpec *dataTableAt(const Sheet &sheet, const CellAddr &addr);

// True when inserting (`insert`) or removing `count` rows (`rows`) or
// columns at `at` would cut through one of the sheet's data tables — its
// results, formula row and input column — which Excel refuses with
// "Cannot change part of a data table." Removing a whole table's lines is
// not a cut.
[[nodiscard]] bool dataTableSplitBy(const Sheet &sheet, bool rows, qint32 at, qint32 count, bool insert);

// Works out one table's results now (Workbook::recomputeDataTables does
// this for each table of the workbook; public for the tests). Leaves the
// workbook's own content exactly as it was, input cells included; a result
// the formula leaves empty is 0, as in Excel, and a table whose input cell
// was deleted shows #REF!.
void computeDataTable(Workbook &book, qint32 sheet, const DataTableSpec &spec);

} // namespace QindaOffice::Calc
