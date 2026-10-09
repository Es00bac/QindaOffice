// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// SlideRenderer (plan section 7.5): paints a slide (up to a build step) to
// a painter or pixmap. The player, the editor canvas and thumbnails, and
// the PDF exporter all render through here so WYSIWYG holds everywhere.
// Everything is in slide points.
//
// M62 (D-070): a slide paints over its BACKDROP — the background (the
// slide's own, else its layout's, else its master's; plain, gradient or
// picture) and the decorative shapes of its master and layout (logos,
// rules; placeholders never) — and every shape kind of the extended model
// (render_parts.h). renderSlide(painter, deck, slide) resolves the backdrop
// from the deck; callers that hold no deck (the canvas keeps a copy, never
// a pointer into the deck's lists) pass one from backdropFor().

#include "presentation.h"

#include <QImage>

class QPainter;

namespace QindaOffice::Show {

struct SlideBackdrop {
    QColor color = Qt::white; // the plain background
    FillStyle style;          // when it is a gradient / picture / pattern
    QList<Shape> shapes;      // master then layout decorations, behind the slide
    // A QindaShow master's type defaults (withMasterDefaults); unset for an
    // Office master, whose type the reader resolved into every run.
    SlideMaster typeMaster;
    bool hasTypeMaster = false;
    bool operator==(const SlideBackdrop &) const = default;
};

// The backdrop `slide` paints over in `deck`.
[[nodiscard]] SlideBackdrop backdropFor(const Presentation &deck, const Slide &slide);
// The backdrop of a bare master (nullptr = white paper).
[[nodiscard]] SlideBackdrop backdropFor(const SlideMaster *master);
void paintBackdrop(QPainter *painter, const SlideBackdrop &backdrop, const QSizeF &slideSize);

// Renders `slide` of `deck` with its backdrop, up to `buildStep`;
// `buildProgress` is how far the shapes AT that step have animated in
// (0..1, the player drives it; 1 = the finished slide).
void renderSlide(QPainter *painter, const Presentation &deck, const Slide &slide, int buildStep = 1000,
                 qreal buildProgress = 1.0);
void renderSlide(QPainter *painter, const Slide &slide, const QSizeF &slideSize, const SlideBackdrop &backdrop,
                 int buildStep = 1000, qreal buildProgress = 1.0);

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
// `slide` of `deck` with its backdrop, as an image of `pixels`.
[[nodiscard]] QImage renderSlideToImage(const Presentation &deck, const Slide &slide, const QSize &pixels);

// The grid a table is drawn on (M67, D-076): the column edges `xs`
// (columns + 1 of them) and row edges `ys` (rows + 1) in slide points, from
// `rect`'s top-left, every row grown to its text the way the renderer grows
// it. The canvas hit-tests cells and borders through this, so a click lands
// on the cell painted under it.
struct TableGrid {
    QList<qreal> xs;
    QList<qreal> ys;
    // The box of the cell at (row, column) spanning rowSpan × columnSpan;
    // empty when out of range.
    [[nodiscard]] QRectF cellRect(int row, int column, int rowSpan = 1, int columnSpan = 1) const;
};
[[nodiscard]] TableGrid tableGrid(const Table &table, const QRectF &rect);

} // namespace QindaOffice::Show
