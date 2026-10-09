// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Data ▸ Data Tools (M78, D-087): Remove Duplicates and Consolidate as
// Excel does them, over the Workbook. Each mutation is the caller's one
// undo step (the app snapshots around it). Qt Core only.

#include "value.h"

#include <QList>
#include <QString>

namespace QindaOffice::Calc {

class Sheet;
class Workbook;

// ---- Remove Duplicates ----------------------------------------------------

struct RemoveDuplicatesResult {
    qint32 removed = 0;   // "N duplicate values found and removed"
    qint32 remaining = 0; // "M unique values remain"
    // Why nothing was done (Excel's refusals: no column chosen, cells merged
    // across rows, part of an array, a PivotTable or a data table in the
    // way); empty when the range was handled.
    QString error;
};

// Removes, from the rows of `range` (its first row left alone when
// `hasHeader`), every row whose values in `columns` (absolute column
// indexes inside the range) repeat an earlier row's — compared as Excel
// does: by value (numbers to fifteen significant digits), text without
// regard to case, blanks equal to blanks; a number and the same digits as
// text differ. The rows that stay move up inside the range with their
// formats, notes and one-row merges (formulas keep pointing at their own
// row's cells, as when the row is moved), and the rows freed at the bottom
// of the range are emptied, content and format; nothing outside the range
// moves. Formulas elsewhere that name the moved cells are not adjusted
// (Excel's behaviour too). Hyperlinks stay where they were.
RemoveDuplicatesResult removeDuplicates(Workbook &book, qint32 sheet, const CellRange &range,
                                        const QList<qint32> &columns, bool hasHeader);

// Whether Excel would take the range's first row for headers (its default
// for "My data has headers"): text above a column that holds a number or a
// date somewhere below.
[[nodiscard]] bool looksLikeHeaderRow(const Workbook &book, qint32 sheet, const CellRange &range);

// Excel's current region (Ctrl+Shift+*, Range.CurrentRegion): the rectangle
// around `at` bounded by blank rows and blank columns — grown while a cell
// of the ring around it, diagonal corners included, holds a value. Just
// `at` when nothing touches it. Flash Fill reads its rows from it.
[[nodiscard]] CellRange currentRegion(const Sheet &sheet, const CellAddr &at);

// ---- Consolidate ------------------------------------------------------------

enum class ConsolidateFunction { Sum, Count, Average, Max, Min, Product, CountNums, StdDev, StdDevP, Var, VarP };

struct ConsolidateSource {
    qint32 sheet = 0;
    CellRange range;
};

struct ConsolidateOptions {
    ConsolidateFunction function = ConsolidateFunction::Sum;
    QList<ConsolidateSource> sources;
    bool topRow = false;     // "Use labels in Top row": columns match by label
    bool leftColumn = false; // "Use labels in Left column": rows match by label
};

struct ConsolidateResult {
    // The block written at the destination, row by row: labels (when used,
    // each with the value it was first seen with — a year stays a number)
    // in the first row / column — the corner stays empty — and the
    // function over every source cell at the same place (by position) or
    // under the same labels (case-insensitive, first-seen order). A cell no
    // source has stays empty; blank labels with data share one row/column.
    // By position, the block is as large as the largest source. The
    // functions follow Excel: Count counts every non-blank cell (COUNTA),
    // the others read only numbers (text, booleans and errors skipped);
    // Average, StdDev and Var with too few numbers give #DIV/0!, Max, Min
    // and Product of none give 0.
    QList<QList<Value>> grid;
    QString error; // Excel's refusal (no references, a reference that is not a range …)
};

[[nodiscard]] ConsolidateResult consolidate(const Workbook &book, const ConsolidateOptions &options);

} // namespace QindaOffice::Calc
