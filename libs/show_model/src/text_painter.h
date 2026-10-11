// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Slide text for the renderer (M62, D-070): a text body laid out the way
// PowerPoint lays it out — insets, vertical anchor, autofit font scale and
// line-spacing reduction, wrap, vertical text; per paragraph the alignment,
// left margin and first-line indent, line spacing (percent or points),
// space before/after, bullets and auto-numbering; per run the font, colour,
// underline/strike, super/subscript, caps, letter spacing and highlight.
//
// AGENT-NOTE: text is laid out on a 72-dpi device with design metrics, so
// one point of type is one unit of the slide's point coordinates on every
// painter (canvas, thumbnail, projector, PDF). Before M62 the renderer let
// QTextDocument use screen metrics and drew 18 pt as 24 slide points.

#include "show_style.h"
#include "show_text.h"

#include <QRectF>

class QPainter;

namespace QindaOffice::Show::Render {

// Paints `paragraphs` into `box` (the shape's text rectangle, insets not
// yet applied).
void paintText(QPainter *painter, const QRectF &box, const QList<TextParagraph> &paragraphs,
               const TextBodyStyle &style);

// Height the paragraphs need at `width` (insets included), for table rows
// that grow to their text.
[[nodiscard]] qreal textHeight(const QList<TextParagraph> &paragraphs, qreal width, const TextBodyStyle &style);

// The label of an auto-numbered paragraph ("3.", "c)", "iv.", …).
[[nodiscard]] QString autoNumberLabel(const QString &scheme, int number);

// M91 (D-099): the hyperlink of the run of text at `point` (slide points)
// when the paragraphs are painted into `box` exactly as paintText() paints
// them; empty when no linked text is there (vertical text is not hit).
[[nodiscard]] QString hyperlinkAt(const QRectF &box, const QList<TextParagraph> &paragraphs, const TextBodyStyle &style,
                                  const QPointF &point);

} // namespace QindaOffice::Show::Render
