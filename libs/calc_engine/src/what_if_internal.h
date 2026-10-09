// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to calc_engine's what-if analysis (M78, D-087): Goal Seek,
// scenarios and data tables all try values in cells and must then give
// each cell back exactly what it held — its value, its formula and the
// formula's dependency edges, its style, its spill and array marks.
// what_if_cells.cpp.

#include "cell.h"
#include "dependencies.h"

#include <optional>

namespace QindaOffice::Calc {

class Workbook;

namespace WhatIf {

// A cell as it was before a trial; `cell` is empty when there was none.
struct SavedCell {
    CellAddr addr;
    std::optional<Cell> cell;
};

[[nodiscard]] SavedCell saveCell(const Workbook &book, qint32 sheet, const CellAddr &addr);

// Puts the saved content back and re-registers its formula's dependency
// edges (a cell that did not exist goes again; its style stays). Does not
// recalculate: the caller recalculates the returned key with the others.
CellKey restoreCell(Workbook &book, qint32 sheet, const SavedCell &saved);

// A trial value in the cell, the formula set aside WITHOUT touching the
// dependency index (restoreCell puts the formula back): the cell's readers
// see `value` once the caller recalculates the returned key. Never through
// setCellValue — that would defer the recalculation inside a batch and
// churn the dependency index for every value tried.
CellKey putTrialValue(Workbook &book, qint32 sheet, const CellAddr &addr, const Value &value);

// A number in the file's spelling — the shortest text that reads back as
// the same double ("0.05", not "0.050000000000000003").
[[nodiscard]] QString numberText(double value);

} // namespace WhatIf
} // namespace QindaOffice::Calc
