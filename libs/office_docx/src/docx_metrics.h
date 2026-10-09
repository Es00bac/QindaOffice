// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Word's line height on Qt's layout (D-068 layout adjustments: display only,
// never written).
//
// Word sizes a single-spaced line from the font's Windows metrics — GDI's
// tmHeight + tmExternalLeading: OS/2 usWinAscent + usWinDescent, plus the
// part of the hhea line gap beyond them — and multiplies it for "auto"
// spacing (w:line / 240). Qt's QTextDocumentLayout uses the font's
// ascent + descent + leading rounded up to a whole pixel. For Calibri
// (Carlito) at 11 pt that is 19 px against Word's 17.9 px: about 6 % more
// lines per page, measured as 7.8 % more pages over the owner's 567 Word
// documents. applyWordLineMetrics() gives each imported paragraph with
// auto spacing a QTextBlockFormat::LineDistanceHeight of the difference and
// keeps its own spacing in kLineSpacingOwn, which ownLineSpacing()
// (docx_format.h) — and so the snapshot and the writer — read instead.

#include <QFont>

class QTextDocument;

namespace QindaOffice::Docx {

// Word's single line height for `font` in the layout's pixels, from the
// tables of the font Qt actually draws (a metric-compatible clone such as
// Carlito answers for Calibri); 0 when they cannot be read.
[[nodiscard]] qreal wordSingleLineHeight(const QFont &font);

// The line height QTextDocumentLayout gives `font` (ascent + descent +
// leading, rounded up), in the same pixels.
[[nodiscard]] qreal qtLineHeight(const QFont &font);

// Adjusts every paragraph read from Word (it carries an import snapshot)
// whose line rule is auto, in `document` and its tables. Run once after
// reading; a new paragraph Write makes keeps Qt's own line height.
void applyWordLineMetrics(QTextDocument *document);

} // namespace QindaOffice::Docx
