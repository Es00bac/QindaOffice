// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal drawing helpers for the form and report painters: page setup
// in POINTS, the one type family, and wrapped/elided text.
//
// AGENT-NOTE: beginPage() resets the transform and scales the painter by
// device DPI / 72, so every coordinate and every font size below is in
// points whatever the device resolution (1200-dpi QPdfWriter, a 600-dpi
// printer, the preview). Fonts therefore use setPixelSize — a pixel IS a
// point under that transform; setPointSize would be scaled twice.

#include <QColor>
#include <QFont>
#include <QRectF>
#include <QString>

class QPagedPaintDevice;
class QPainter;

namespace QindaOffice::Books::Paint {

// Minimum distance from the paper edge, in points.
inline constexpr qreal kMinMargin = 46;

// Scales the painter to points and returns the content rectangle (device
// paint rect minus whatever inset reaches kMinMargin from the paper edge).
QRectF beginPage(QPagedPaintDevice *device, QPainter *painter);

[[nodiscard]] QFont font(int px, QFont::Weight weight = QFont::Normal);
[[nodiscard]] qreal textWidth(const QFont &font, const QString &text);
[[nodiscard]] qreal lineHeight(const QFont &font);
// Height of `text` wrapped to `width`.
[[nodiscard]] qreal wrappedHeight(const QFont &font, const QString &text, qreal width);
[[nodiscard]] QString elide(const QFont &font, const QString &text, qreal width);

// Ink colours. D-004: paper is white, ink is black; greys are ink shades.
inline const QColor kInk = QColor(0x11, 0x11, 0x11);
inline const QColor kMuted = QColor(0x5C, 0x5C, 0x5C);
inline const QColor kRule = QColor(0xD6, 0xD6, 0xD6);

// Draws `text` with its top-left at (x, top) or right-aligned to x.
void drawText(QPainter *painter, qreal x, qreal top, const QString &text, const QFont &font,
              const QColor &color = kInk, bool alignRight = false);

} // namespace QindaOffice::Books::Paint
