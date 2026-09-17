// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Structural edits: inserting and removing rows and columns, sheet-list
// operations and range sorting.
//
// These are the operations that MOVE existing data, which is why they live
// together and not on Sheet: moving a cell is only half the job. Every
// formula in the workbook that pointed at it has to follow, references that
// fall inside a removed band have to become #REF! rather than silently
// pointing at whatever slid into their place, and the dependency index —
// which is keyed by sheet INDEX and cell position — has to be rebuilt
// afterwards (Workbook::reindexAll).
//
// AGENT-CONTRACT: callers own undo. Everything here mutates the workbook in
// place; QindaCalc wraps each call in a snapshot command (calc_undo.h).

#include "value.h"

#include <QString>

namespace QindaOffice::Calc {

class Workbook;

enum class StructureOp { InsertRows, RemoveRows, InsertColumns, RemoveColumns };

struct StructureEdit {
    StructureOp op = StructureOp::InsertRows;
    qint32 sheet = 0;
    qint32 at = 0;    // 0-based index of the first affected row/column
    qint32 count = 1;
};

// Moves cells, cell styles, merges and explicit row heights / column widths
// on the edited sheet, rewrites every formula in the workbook (including
// ones on other sheets that name this one), adjusts named ranges and chart
// anchors, then rebuilds the dependency index and recalculates.
void applyStructureEdit(Workbook &book, const StructureEdit &edit);

// Sheet-list operations. Formulas that name the sheet follow a rename and
// become #REF! after a removal; the last sheet cannot be removed (a
// workbook always has one). Charts anchored on the removed sheet go with
// it. All of these reindex and recalculate.
bool removeSheet(Workbook &book, qint32 index);
bool renameSheet(Workbook &book, qint32 index, const QString &newName);
bool moveSheet(Workbook &book, qint32 from, qint32 to);

// Sorts the rows of `range` by the values in column `keyColumn` (an
// absolute column index, which must lie inside the range). Cell content,
// formulas (relative references shift with the row they travel on) and
// styles move together; blanks sort last in both directions so an empty
// tail never floats to the top. `headerRow` keeps the range's first row in
// place.
void sortRange(Workbook &book, qint32 sheet, const CellRange &range, qint32 keyColumn,
               bool ascending, bool headerRow);

} // namespace QindaOffice::Calc
