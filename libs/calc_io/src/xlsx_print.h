// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Sheet printing settings as Excel writes them (M78, D-087): <pageSetup>
// (orientation, paper, scale, Fit to, first page number, order, draft,
// black and white, comments, errors), <pageMargins>, <printOptions>
// (gridlines, headings, centring), <headerFooter> (odd, even and first
// headers and footers, alignWithMargins, scaleWithDoc), <rowBreaks> and
// <colBreaks> (manual breaks), and sheetPr's <pageSetUpPr fitToPage> — read
// into Sheet::PageSetup and written from it, attributes at their schema
// defaults left out (a <pageSetup> always says its orientation and always
// comes with <pageMargins>, as Excel's do; a sheet at Excel's defaults gets
// none of them). Margins are written in the shortest text that reads back
// as the same number.
//
// The print area and titles are the _xlnm defined names
// (xlsx_read_book.cpp, xlsx_write_book.cpp): every area of a multi-area
// print area is kept (PageSetup::morePrintAreas), and a print name the
// model cannot hold (a dynamic =OFFSET(…) area) stays a sheet-scoped
// DefinedName, written back as read while QindaCalc sets none of its own.
// The source's printer settings part and the <pageSetup> attributes the
// model does not hold (DPI, copies, a custom paper's size) travel through
// the carry stage's pageSetup merge; the pictures of headers and footers
// (&G, <legacyDrawingHF>) are carried with their VML (xlsx_carry_sheet.cpp).

#include "xlsx_manifest.h"

#include <QByteArray>

#include "sheet.h"

namespace QindaOffice::Calc::IO {

// Reads the print settings of a worksheet part into `sheet`'s PageSetup.
void readSheetPrint(const QByteArray &sheetXml, Sheet *sheet);

// The print settings' elements for `patch` (printXml, fitToPage). Nothing
// is written for settings that are all Excel's defaults.
void fillPrintPatch(const Sheet &sheet, PatchManifest::SheetPatch *patch);

// A Custom View's print settings (M82, D-091): the same elements in
// CT_CustomSheetView's order (breaks, margins, options, setup, header and
// footer), margins and setup always.
[[nodiscard]] QByteArray viewPrintXml(const Sheet::PageSetup &setup);

} // namespace QindaOffice::Calc::IO
