// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Data ▸ What-If Analysis ▸ Goal Seek (M78, D-087): "Set cell" to "To
// value" by changing "By changing cell", with Excel's iteration settings —
// at most 100 iterations, done when the set cell is within 0.001 of the
// target (File ▸ Options ▸ Formulas: Maximum Iterations, Maximum Change).
// Excel does not document its search; this one takes secant steps from the
// changing cell's value, brackets the root and bisects once a sign change
// is seen, and keeps the closest value it met. Qt Core only.

#include "value.h"

#include <QString>

namespace QindaOffice::Calc {

class Workbook;

struct GoalSeekOptions {
    int maxIterations = 100;
    double maxChange = 0.001;
};

struct GoalSeekResult {
    bool started = false;     // false: nothing was tried (see `error`)
    bool found = false;       // the set cell came within maxChange of the target
    double changingValue = 0; // the value left in the changing cell
    double setValue = 0;      // the set cell's value with it
    int iterations = 0;
    // Excel's refusals: "Cell must contain a formula." (the set cell),
    // "Cell must contain a value." (the changing cell holds a formula),
    // and a non-numeric result.
    QString error;
};

// Runs Goal Seek on `sheet`. The workbook is left holding the best value
// found in the changing cell (Excel's Goal Seek Status then offers OK,
// which keeps it, or Cancel, which puts the old value back — the caller
// takes a snapshot first); when nothing came closer than the start, the
// cell keeps its own content. AGENT-GUARD: never inside a Workbook batch —
// each value tried must recalculate at once, or every try reads the start.
[[nodiscard]] GoalSeekResult goalSeek(Workbook &book, qint32 sheet, const CellAddr &setCell, double target,
                                      const CellAddr &changingCell, const GoalSeekOptions &options = {});

} // namespace QindaOffice::Calc
