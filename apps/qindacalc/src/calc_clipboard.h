// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaCalc clipboard (M12): copy/cut/paste over the system clipboard.
// Two representations travel together:
//  - text/plain: TSV of display text (paste into any app, paste TSV from
//    any app back into the grid);
//  - application/x-qindacalc-cells: JSON of the raw cell inputs, so a
//    copy/paste inside QindaCalc keeps formulas exact ("=A1*2" stays a
//    formula; cell styles are not carried yet - documented limitation).

#include "value.h"
#include "workbook.h"

class QClipboard;

namespace CalcEng = QindaOffice::Calc;
namespace QindaOffice::Apps::QindaCalc {

class SpreadsheetDocument;

void copyRangeToClipboard(SpreadsheetDocument *document, const CalcEng::CellRange &range);
// Copy + clear in one undo step.
void cutRangeToClipboard(SpreadsheetDocument *document, const CalcEng::CellRange &range);
// Pastes the clipboard at `at` (rich format preferred). Returns false when
// the clipboard holds nothing grid-shaped.
bool pasteClipboardAt(SpreadsheetDocument *document, const CalcEng::CellAddr &at);

// MIME type of the rich representation (exposed for tests).
[[nodiscard]] const char *richCellsMimeType();

} // namespace QindaOffice::Apps::QindaCalc
