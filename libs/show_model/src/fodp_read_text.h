// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Paragraph and run reading for .fodp text (text boxes, custom shapes,
// notes): text:p / text:h / text:list nesting (outline level = depth),
// spans with their styles resolved through the shape's text and graphic
// styles, and ODF's white-space rules (§6.1.2): raw white space collapses,
// text:s / text:tab / text:line-break are literal. Also reads the pre-M62
// QindaShow spellings (fo: attributes directly on text:span / text:p and
// "qq-list-N" paragraph style names) so old files keep their formatting.

#include "fodp_read_styles.h"
#include "presentation.h"

namespace QindaOffice::Show::Fodp {

// Where a shape's own text defaults come from.
struct TextDefaults {
    QString textStyle;     // draw:text-style-name (paragraph family)
    QString graphicStyle;  // draw:style-name / presentation:style-name
    QString graphicFamily; // "graphic" or "presentation"
};

[[nodiscard]] QList<TextParagraph> readParagraphs(const Odf::OdfNode &container, const StyleSheet &sheet,
                                                  const TextDefaults &defaults);

// The text of every paragraph under `container`, one line each (notes).
[[nodiscard]] QString readPlainText(const Odf::OdfNode &container);

} // namespace QindaOffice::Show::Fodp
