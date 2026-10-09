// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Workbook-level pieces of an xlsx save (M61, D-069): every defined name the
// file gets, and Excel's rule for table column names.

#include "xlsx_manifest.h"

#include <QHash>
#include <QList>

namespace QindaOffice::Calc::IO {

// Every defined name of `wb` in file spelling: each sheet's print area and
// titles (localSheetId), the workbook-wide range names, then the scoped,
// constant and formula names in the order the file had them. Sheet names
// are quoted wherever a reference needs it ('Q1 Sales'!$A$1) and formulas
// carry Excel's _xlfn. prefixes.
[[nodiscard]] QList<PatchManifest::DefinedNameEntry> definedNamesForFile(const Workbook &wb);

// A table the way Excel requires it in the file: every column named exactly
// as its header cell reads, the names unique within the table (compared
// without case). A blank header becomes "ColumnN", a duplicate gains a
// number ("Amount2"), a number reads as its text; each header cell whose
// text must change to match goes into `headerText` (keyed by
// CellAddr::key()), so the cell and the column name are written alike.
[[nodiscard]] TableSpec tableForFile(const Workbook &wb, qint32 sheet, const TableSpec &table,
                                     QHash<quint64, QString> *headerText);

// The table part to write (M69, D-078): tableForFile's record and, per
// column, the calculated column formula (the first data row's formula
// while it has one, so the part never trails an edit; else the record's)
// and what the totals row cell is — one of Excel's totals functions
// (SUBTOTAL(1xx,[Column]) of its own column), a custom formula or a label —
// all in Excel's spelling.
[[nodiscard]] PatchManifest::TablePart tablePartForFile(const Workbook &wb, qint32 sheet, const TableSpec &table,
                                                        QHash<quint64, QString> *headerText);

} // namespace QindaOffice::Calc::IO
