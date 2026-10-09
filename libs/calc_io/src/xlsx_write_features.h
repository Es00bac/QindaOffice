// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The write half of M66 (D-075): what a sheet's hyperlinks, outline,
// hidden lines, protection, code name and source tie need from the post-
// pass, gathered into its SheetPatch while the model is in hand. The bytes
// are written by xlsx_post_sheet.cpp.

#include "xlsx_manifest.h"

namespace QindaOffice::Calc::IO {

void fillSheetFeatures(const Workbook &wb, const Sheet &sheet, PatchManifest::SheetPatch *patch);

// "A1" or "A1:B4".
[[nodiscard]] QString rangeText(const CellRange &range);

// <sheetProtection …/> for an enabled protection (only the attributes that
// differ from the schema's defaults besides the password check).
[[nodiscard]] QByteArray sheetProtectionXml(const SheetProtection &protection);
// <workbookProtection …/>, or empty when nothing is locked and no password.
[[nodiscard]] QByteArray workbookProtectionXml(const WorkbookProtection &protection);

} // namespace QindaOffice::Calc::IO
