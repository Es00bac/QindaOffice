// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to the Visio interchange (M65, D-073): shape text.
//
// A <Text> element is mixed content: character data interleaved with empty
// <cp IX/>, <pp IX/> and <tp IX/> markers that switch to row IX of the
// Character, Paragraph and Tabs sections for what follows, and <fld IX>
// whose content is a field's cached result. Visio ends the text with a
// paragraph mark ("\n"), which is not part of what the shape shows.
//
// The drawing model formats a shape's text as one run (D-050), so the
// reader takes the look of the first run that shows something; the source
// <Text> stays in the document and is written back untouched while the
// text and its format are unchanged (D-068 rule 3).

#include "drawing.h"
#include "visio_doc.h"

#include <QList>

namespace QindaOffice::Diagram::Visio {

struct TextRun {
    int start = 0;
    int length = 0;
    int charIx = 0;
    int paraIx = 0;
};

struct ParsedText {
    QString text;         // without the closing paragraph mark
    QList<TextRun> runs;  // in order, covering `text`
    bool fields = false;  // a <fld> contributed text
};

[[nodiscard]] ParsedText parseText(const XmlElement &text);
// The run whose format the shape takes (the first with visible text).
[[nodiscard]] TextRun leadingRun(const ParsedText &text);

// Character row `charIx` + Paragraph row `paraIx` + the text block's
// VerticalAlign as the model's one-run format. A themed colour stays
// invalid (the reader picks light or dark by the fill).
[[nodiscard]] TextStyle readTextStyle(const Lookup &lookup, int charIx, int paraIx);

// <Text><cp IX="0"/><pp IX="0"/>text\n</Text>.
[[nodiscard]] XmlElementPtr textElement(const QString &text);
// Character / Paragraph sections with one row (IX 0) for `style`; the
// colour of a style with no valid colour is left to inherit.
[[nodiscard]] XmlElementPtr characterSection(const TextStyle &style, const QString &family);
[[nodiscard]] XmlElementPtr paragraphSection(const TextStyle &style);
// VerticalAlign's value for `style`.
[[nodiscard]] QString verticalAlignValue(const TextStyle &style);

} // namespace QindaOffice::Diagram::Visio
