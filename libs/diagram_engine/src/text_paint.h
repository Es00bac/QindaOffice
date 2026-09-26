// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to diagram_engine: device-independent text drawing for the
// painter (see painter.h's AGENT-GUARD). Everything is in page units.

#include "drawing.h"

#include <QFont>
#include <QRectF>
#include <QSizeF>
#include <QString>

class QPainter;

namespace QindaOffice::Diagram::TextPaint {

// The reference font the layout runs at (100 px), and the factor that maps
// its pixels to page units for `style`.
[[nodiscard]] QFont referenceFont(const TextStyle &style, bool bold, double sizeFactor = 1.0);
[[nodiscard]] double pageScale(const TextStyle &style, double sizeFactor = 1.0);

// Draws `text` wrapped inside `box` with the style's alignment.
void drawBlock(QPainter *painter, const QString &text, const TextStyle &style, const QRectF &box);
// Size `text` needs (unwrapped lines), page units.
[[nodiscard]] QSizeF measure(const QString &text, const TextStyle &style);
// Height of one line of `style`, page units.
[[nodiscard]] double lineHeight(const TextStyle &style, double sizeFactor = 1.0);

// An org card: first line the name (bold, larger), the rest the title.
void drawCard(QPainter *painter, const QString &text, const TextStyle &style, const QRectF &box);
// A UML class: sections separated by "--" lines; the name centred bold,
// the other compartments left-aligned with rules between them (drawn in
// `rule`, across `width`).
void drawClass(QPainter *painter, const QString &text, const TextStyle &style, double width,
               const QColor &rule, double ruleWidth);

} // namespace QindaOffice::Diagram::TextPaint
