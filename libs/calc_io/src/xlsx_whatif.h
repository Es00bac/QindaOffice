// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What-if analysis in the file (M78, D-087): a sheet's <scenarios>
// (scenario.h) and its data tables — the top-left result cell's
// <f t="dataTable" ref dt2D dtr r1 r2 del1 del2/> with every result cell's
// value (data_table.h) — read into the model and written as Excel writes
// them.

#include "xlsx_manifest.h"

#include <QByteArray>

namespace QindaOffice::Calc {
class Sheet;
class Workbook;
}

namespace QindaOffice::Calc::IO {

// Reads a worksheet part's scenarios and data tables into `sheet` (call
// while the workbook is bulk loading: the tables are worked out when the
// load's recalculation runs).
void readSheetWhatIf(const QByteArray &sheetXml, Sheet *sheet);

// The sheet's scenarios and data-table formulas for `patch` (scenariosXml,
// cellFormulaXml; a table cell whose value is text, a boolean or an error
// is retyped through cellTypes).
void fillWhatIfPatch(const Workbook &wb, qint32 sheet, PatchManifest::SheetPatch *patch);

} // namespace QindaOffice::Calc::IO
