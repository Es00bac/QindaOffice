// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Mapping between WordprocessingML property elements and Qt text formats.
//
// Read direction: an EFFECTIVE w:pPr / w:rPr (the style stack already merged,
// docx_styles.h) becomes a QTextBlockFormat / QTextCharFormat. Paragraph
// spacing is returned separately because Word ADDS the space after one
// paragraph to the space before the next while Qt keeps the larger of the
// two; the body reader folds the sum into the next block's top margin.
//
// Snapshots: blockSnapshot()/charSnapshot() extract the properties this
// mapping controls into a QVariantMap. The reader stores the snapshot on the
// format; the writer recomputes it from the current format and re-derives
// markup only for properties whose values differ (D-068 rule 3). Both sides
// MUST use these functions so they agree on what "unchanged" means.

#include "ooxml_theme.h"
#include "ooxml_xml.h"

#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QVariantMap>

namespace QindaOffice::Docx {

struct ParagraphSpacing {
    qreal before = 0; // px
    qreal after = 0;  // px
    bool contextual = false;
};

[[nodiscard]] ParagraphSpacing spacingOf(const Ooxml::XmlElement *pPr);

// Alignment, indents, line spacing, tabs (relative to the left indent, as
// Qt measures them), shading, keep-lines, page break before, direction.
// Top/bottom margins are left unset (see spacingOf).
[[nodiscard]] QTextBlockFormat blockFormatFromPPr(const Ooxml::XmlElement *pPr, const Ooxml::Theme &theme);

[[nodiscard]] QTextCharFormat charFormatFromRPr(const Ooxml::XmlElement *rPr, const Ooxml::Theme &theme);

// The background a w:shd paints (pattern percentages blended), or an
// invalid colour for none/auto.
[[nodiscard]] QColor shadingColor(const Ooxml::XmlElement *shd, const Ooxml::Theme &theme);

// "FF0000" / "auto" / theme attributes of a colour-bearing element
// (w:color, a border side) → colour; invalid for auto.
[[nodiscard]] QColor wordColor(const Ooxml::XmlElement *element, const Ooxml::Theme &theme,
                               QStringView valueAttribute = u"val");

// A w:pBdr as the kParagraphBorders display spec (docx_properties.h);
// empty when no side draws.
[[nodiscard]] QString paragraphBordersSpec(const Ooxml::XmlElement &pBdr, const Ooxml::Theme &theme);

[[nodiscard]] QVariantMap blockSnapshot(const QTextBlockFormat &format);
[[nodiscard]] QVariantMap charSnapshot(const QTextCharFormat &format);

// The font family a w:rFonts element names for Latin text (theme fonts
// resolved), or empty.
[[nodiscard]] QString latinFont(const Ooxml::XmlElement *rFonts, const Ooxml::Theme &theme);

} // namespace QindaOffice::Docx
