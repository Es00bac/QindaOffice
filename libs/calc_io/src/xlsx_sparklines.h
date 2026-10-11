// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Sparklines in Excel's markup (M90, D-098): a worksheet's
// <extLst><ext uri="{05C60535-1F16-4fd2-B633-F4F36F0B64E0}"> holding
// <x14:sparklineGroups> — each <x14:sparklineGroup> with its attributes,
// its eight colours (CT_Color: rgb, or theme with tint), the date-axis
// range and <x14:sparklines> of <xm:f> data and <xm:sqref> cell — read into
// Sheet::sparklineGroups and written from it.
//
// AGENT-CONTRACT (xlsx_carry_types.h sheetExtCarried): the carry stage no
// longer carries this ext — the model holds every group and the post-pass
// writes them, after the sheet's Excel 2010 conditional formats and
// validations (Excel's order of the worksheet's extensions).

#include "xlsx_manifest.h"

#include <QString>

namespace QXlsx {
class ZipReader;
}

namespace QindaOffice::Calc {
class Sheet;
class Workbook;
}

namespace QindaOffice::Calc::IO {

void readSparklines(QXlsx::ZipReader &zip, const QString &sheetPart, Sheet *sheet);
// The sheet's groups as Excel's ext, appended to patch->extXml.
void fillSparklinePatch(const Workbook &wb, qint32 sheet, PatchManifest::SheetPatch *patch);

} // namespace QindaOffice::Calc::IO
