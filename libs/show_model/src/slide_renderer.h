// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// SlideRenderer (plan section 7.5): paints a slide (up to a build step) to
// a painter or pixmap. The player, the editor thumbnails, and the PDF
// exporter all render through here so WYSIWYG holds everywhere.

#include "presentation.h"

class QPainter;

namespace QindaOffice::Show {

void renderSlide(QPainter *painter, const Slide &slide, const QSizeF &slideSize,
                 int buildStep = 1000);
// Single-shape paint for editor items: identical pixels to the slide
// render (the editor canvas must not drift from the player).
void renderShape(QPainter *painter, const Shape &shape);
[[nodiscard]] QPixmap renderSlideToPixmap(const Slide &slide, const QSizeF &slideSize,
                                          const QSize &pixels, int buildStep = 1000);

} // namespace QindaOffice::Show
