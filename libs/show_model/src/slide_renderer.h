// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// SlideRenderer (plan section 7.5): paints a slide (up to a build step) to
// a painter or pixmap. The player, the editor thumbnails, and the PDF
// exporter all render through here so WYSIWYG holds everywhere.

#include "presentation.h"

class QPainter;

namespace QindaOffice::Show {

// Renders a slide up to `buildStep`. `master` supplies the background and
// the default type (nullptr = plain white). `buildProgress` is how far the
// shapes AT that build step have animated in, 0..1 — the player drives it,
// and at 1 (the default) the result is the finished slide, which is what
// the editor, the thumbnails and the PDF want.
void renderSlide(QPainter *painter, const Slide &slide, const QSizeF &slideSize,
                 int buildStep = 1000, const SlideMaster *master = nullptr,
                 qreal buildProgress = 1.0);
// Single-shape paint for editor items: identical pixels to the slide
// render (the editor canvas must not drift from the player). `master`
// supplies the default type for runs that never chose their own.
void renderShape(QPainter *painter, const Shape &shape, const SlideMaster *master = nullptr);

// A copy of `shape` with the master's defaults filled in. Exposed so the
// editor's per-shape items apply exactly what renderSlide applies.
[[nodiscard]] Shape withMasterDefaults(const Shape &shape, const SlideMaster *master);
[[nodiscard]] QPixmap renderSlideToPixmap(const Slide &slide, const QSizeF &slideSize,
                                          const QSize &pixels, int buildStep = 1000,
                                          const SlideMaster *master = nullptr);

} // namespace QindaOffice::Show
