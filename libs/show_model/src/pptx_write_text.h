// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// DrawingML spelling of the model for the pptx writer (internal; D-070):
// colours, fills, outlines, and text bodies.
//
// Text is written so it READS BACK as the same effective values: a run or
// paragraph whose formatting equals one the reader produced from the
// source (same level, same values) reuses that source a:rPr / a:pPr
// verbatim, keeping its inheritance and theme links (D-068 rule 3); any
// other formatting is spelled out explicitly.

#include "pptx_write_context.h"

namespace QindaOffice::Show::Pptx {

// a:srgbClr (with a:alpha below 100 %).
[[nodiscard]] Ooxml::XmlElementPtr colorElement(const QColor &color);
// The fill element for a plain colour or a FillStyle (a:noFill when the
// colour is transparent).
[[nodiscard]] Ooxml::XmlElementPtr fillElement(PartWriter &out, const QColor &plain, const FillStyle &style);
// p:bg with its p:bgPr fill (slides, and generated masters and layouts).
[[nodiscard]] Ooxml::XmlElementPtr backgroundElement(PartWriter &out, const Background &background);
// a:ln; `visible` false writes <a:ln><a:noFill/></a:ln>.
[[nodiscard]] Ooxml::XmlElementPtr lineElement(const QColor &color, qreal width, const LineStyle &line, bool visible);

// Where a text body came from, for reuse.
struct TextSource {
    const Ooxml::XmlElement *txBody = nullptr;     // the source p:txBody / a:txBody
    const QList<TextParagraph> *pristine = nullptr; // what the reader produced from it
    const TextBodyStyle *pristineBody = nullptr;
    QString part;                                   // the source part (hyperlink ids)
};

// p:txBody (or a:txBody inside a table cell when `ns` is DrawingML's).
[[nodiscard]] Ooxml::XmlElementPtr textBodyElement(PartWriter &out, const QString &ns,
                                                   const QList<TextParagraph> &paragraphs,
                                                   const TextBodyStyle &body, const TextSource &source = {});

// Points → EMU as written in attributes.
[[nodiscard]] QString emu(qreal points);

} // namespace QindaOffice::Show::Pptx
