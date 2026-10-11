// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Table Design (M90, D-098): Table Name and Resize Table, as Excel does
// them. A rename moves every structured reference that names the table —
// in cells, defined names, other tables' calculated columns — and the
// PivotTable caches built on it. A resize keeps the header row where it is
// and must overlap the table; columns that leave the table make the
// references to them #REF!, columns that join it get names (Column<N>) and
// rows that join it get the calculated columns. Both refuse, changing
// nothing, with Excel's message. Callers own undo; Qt Core only.

#include "value.h"

#include <QString>

namespace QindaOffice::Calc {

class Workbook;

// "" when `name` can name a table — Excel's rules: a letter, an underscore
// or a backslash first, then letters, digits, periods and underscores, no
// cell reference ("A1", "R1C1", "R", "C"), at most 255 characters, and no
// other table or defined name of the workbook already called that (any
// case; `current`, the table's own name, excepted). Else Excel's message.
[[nodiscard]] QString tableNameProblem(const Workbook &book, const QString &name, const QString &current);

bool renameTable(Workbook &book, qint32 sheet, qsizetype tableIndex, const QString &name, QString *error);
bool resizeTable(Workbook &book, qint32 sheet, qsizetype tableIndex, const CellRange &range, QString *error);

} // namespace QindaOffice::Calc
