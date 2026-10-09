// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Excel tables as they are edited (M69, D-078): the behaviours that keep a
// table, its header cells and every structured reference to it in step —
// what Excel does when you type into or next to a table.
//
// AGENT-CONTRACT: QindaCalc's document routes every typed, pasted, filled
// or replaced cell through setTableAwareInput and calls syncTableHeaders
// after clearing cells; the engine never does table work behind a plain
// Workbook::setCellInput (calc_io's loads and the structural edits write
// cells too, and must not rename or grow anything). Every function here
// mutates in place and leaves dependencies current; callers own undo.

#include "value.h"

#include <QList>
#include <QString>

namespace QindaOffice::Calc {

class Workbook;
struct TableSpec;

// Typing `input` at `addr`, the way Excel treats tables:
//  * a header cell renames its column, and every structured reference to
//    the old name follows; a blank header becomes Column<N> and a duplicate
//    gets a number (Sales2), written into the cell;
//  * a value typed into the row just below a table without a totals row,
//    or into the column just right of it, makes the table grow over it
//    (auto-expansion); a new row gets the calculated columns' formulas;
//  * a formula typed into a data cell of a column that is otherwise empty,
//    or is already a calculated column, fills the column's every data row
//    and becomes its calculated column formula.
// Returns what Workbook::setCellInput returns.
bool setTableAwareInput(Workbook &book, qint32 sheet, const CellAddr &addr, const QString &input);

// Brings the header cells of `sheet`'s tables and their column names back
// in step (the first rule above) after cells were cleared or pasted over.
// True when a table changed.
bool syncTableHeaders(Workbook &book, qint32 sheet);

// Insert ▸ Table: a table over `range` whose first row is its header row
// (a one-row range gets an empty data row below it), named Table<N>, in
// Excel's default style with filter buttons; blank headers are named. A
// sheet filter over the range gives way to the table's own. False with
// `error` set, and nothing changed, when the range overlaps a table, a
// merged cell, an array formula or a spill.
bool createTable(Workbook &book, qint32 sheet, const CellRange &range, QString *error);

// Table Design ▸ Total Row. On: the row below the table (which must be
// empty) becomes its totals row, with "Total" in the first column and
// SUBTOTAL(109,[Last]) — 103, a count, for a column of text — in the last.
// Off: the totals row's cells are emptied and the row leaves the table.
bool setTableTotalsRow(Workbook &book, qint32 sheet, qsizetype tableIndex, bool on, QString *error);

// A name no table or defined name of the workbook uses: Table<N>.
[[nodiscard]] QString nextTableName(const Workbook &book);

// The calculated columns of `table` (as it is now) written into its data
// `rows`, skipping column `skipCol` (-1: none): each from the nearest data
// row of its column that holds a formula and is not being filled, shifted
// so relative references keep their meaning, else from the stored
// formula. One recalculation at the end. Auto-expansion and inserted rows
// (structure_tables.cpp) use it.
void fillCalculatedCells(Workbook &book, qint32 sheet, const TableSpec &table, const QList<qint32> &rows,
                         qint32 skipCol);

} // namespace QindaOffice::Calc
