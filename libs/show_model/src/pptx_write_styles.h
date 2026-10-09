// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Text styles of masters and layouts for the pptx writer (internal; M75,
// D-084). The model keeps a master's p:titleStyle / p:bodyStyle /
// p:otherStyle as effective paragraphs per level (SlideMaster::titleStyle
// …) and a layout placeholder's look in its sample paragraphs; what the
// Slide Master view changed is written as PowerPoint writes it — into the
// level's a:lvlNpPr (and its a:defRPr) of the master's text styles, or of
// the placeholder's own a:lstStyle — so every slide that inherits the
// level follows, in PowerPoint and when the deck is read again.
//
// Only properties that differ from what was read are spelled: a level
// keeps its theme links (+mj-lt, scheme colours) for everything else. A
// family equal to the theme's heading or body font is written as the
// theme reference.

#include "pptx_write_context.h"

namespace QindaOffice::Show::Pptx {

// The formatting of `paragraphs` per outline level: the first paragraph of
// each level (nine entries; a level with no paragraph has listLevel -1).
[[nodiscard]] QList<TextParagraph> levelFormats(const QList<TextParagraph> &paragraphs);

// Writes into `style` (p:titleStyle, a:lstStyle, …) the properties of
// each level of `now` that differ from `was` (same indexing; a level
// missing on either side is left alone).
void patchListStyle(Ooxml::XmlElement &style, const QList<TextParagraph> &now, const QList<TextParagraph> &was,
                    const ThemeFonts &fonts);

// A complete list style element (`ns`:`name`) spelling every level of
// `levels` (generated masters whose source is gone).
[[nodiscard]] Ooxml::XmlElementPtr listStyleElement(const QString &ns, const QString &name,
                                                    const QList<TextParagraph> &levels, const ThemeFonts &fonts);

// A txBody holding `shape`'s words with nothing of their look (the runs
// inherit every property): a placeholder QindaShow made on a layout.
[[nodiscard]] Ooxml::XmlElementPtr plainTextBody(PartWriter &out, const QString &ns, const Shape &shape);

// The p:txBody of a master's or layout's placeholder: the source's (its
// bodyPr and paragraphs as written) with the level formatting the model
// changed written into its a:lstStyle — unless `stylesElsewhere` (a
// master's title and body, whose look is the master's text styles) — and
// its words rewritten, inheriting everything, when they changed.
[[nodiscard]] Ooxml::XmlElementPtr templateTextBody(PartWriter &out, const Shape &shape, const ShapeSource &source,
                                                    bool stylesElsewhere, const ThemeFonts &fonts);

} // namespace QindaOffice::Show::Pptx
