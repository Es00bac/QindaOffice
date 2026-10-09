// SPDX-License-Identifier: GPL-3.0-or-later
#include "slide_renderer.h"

#include "render_parts.h"

#include <QPainter>
#include <QPixmap>

namespace QindaOffice::Show {

namespace {

// Applies the shape's arrival animation at `progress` (0 = not yet there,
// 1 = fully arrived). Returns false when the shape should not be drawn at
// all yet. The transform is left on the painter for the caller's save/
// restore pair to undo.
bool applyBuildAnimation(QPainter *painter, const Shape &shape, const QSizeF &slideSize,
                         qreal progress)
{
    if (shape.animation == ShapeAnimation::Appear) {
        // "Appear" is instantaneous by definition: it is either there or
        // it is not.
        return progress > 0.0;
    }
    if (progress <= 0.0) {
        return false;
    }
    const qreal eased = 1.0 - (1.0 - progress) * (1.0 - progress); // out-quad
    const QPointF centre = shape.rect.normalized().center();
    switch (shape.animation) {
    case ShapeAnimation::Fade:
        painter->setOpacity(painter->opacity() * eased);
        break;
    case ShapeAnimation::Zoom: {
        painter->setOpacity(painter->opacity() * eased);
        const qreal scale = 0.6 + 0.4 * eased;
        painter->translate(centre);
        painter->scale(scale, scale);
        painter->translate(-centre);
        break;
    }
    case ShapeAnimation::FlyInLeft:
        painter->translate(-slideSize.width() * (1.0 - eased), 0);
        break;
    case ShapeAnimation::FlyInRight:
        painter->translate(slideSize.width() * (1.0 - eased), 0);
        break;
    case ShapeAnimation::FlyInTop:
        painter->translate(0, -slideSize.height() * (1.0 - eased));
        break;
    case ShapeAnimation::FlyInBottom:
        painter->translate(0, slideSize.height() * (1.0 - eased));
        break;
    case ShapeAnimation::Appear:
        break;
    }
    return true;
}

void applyBackground(SlideBackdrop &backdrop, const Background &background)
{
    backdrop.color = background.color.isValid() ? background.color : QColor(Qt::white);
    backdrop.style = background.style;
}

} // namespace

Shape withMasterDefaults(const Shape &shape, const SlideMaster *master)
{
    // AGENT-NOTE: only QindaShow's own masters fill in type defaults (see
    // SlideMaster::typeDefaults); an Office master's type is already in
    // every run, and "black means unset" would repaint real black text.
    if (master == nullptr || !master->typeDefaults) {
        return shape;
    }
    Shape styled = shape;
    for (TextParagraph &paragraph : styled.paragraphs) {
        for (TextRun &run : paragraph.runs) {
            // Only runs that never chose for themselves inherit, so a
            // deliberately red word stays red when the master changes.
            if (run.color == QColor(Qt::black)) {
                run.color = master->bodyColor;
            }
            if (run.fontFamily.isEmpty() && !master->fontFamily.isEmpty()) {
                run.fontFamily = master->fontFamily;
            }
        }
    }
    return styled;
}

SlideBackdrop backdropFor(const SlideMaster *master)
{
    SlideBackdrop backdrop;
    if (master == nullptr) {
        return backdrop;
    }
    backdrop.color = master->background.isValid() ? master->background : QColor(Qt::white);
    backdrop.style = master->backgroundStyle;
    backdrop.shapes = master->shapes;
    if (master->typeDefaults) {
        backdrop.typeMaster = *master;
        backdrop.typeMaster.shapes.clear();
        backdrop.hasTypeMaster = true;
    }
    return backdrop;
}

SlideBackdrop backdropFor(const Presentation &deck, const Slide &slide)
{
    const SlideMaster *master = deck.masterFor(slide);
    const SlideLayout *layout = deck.layoutFor(slide);
    SlideBackdrop backdrop = backdropFor(master);
    backdrop.shapes.clear();
    if (slide.showMasterShapes) {
        if (master != nullptr && (layout == nullptr || layout->showMasterShapes)) {
            backdrop.shapes += master->shapes;
        }
        if (layout != nullptr) {
            backdrop.shapes += layout->shapes;
        }
    }
    if (slide.background.set) {
        applyBackground(backdrop, slide.background);
    } else if (layout != nullptr && layout->background.set) {
        applyBackground(backdrop, layout->background);
    }
    return backdrop;
}

void paintBackdrop(QPainter *painter, const SlideBackdrop &backdrop, const QSizeF &slideSize)
{
    painter->save();
    const QRectF page(QPointF(0, 0), slideSize);
    // A see-through or missing colour still leaves white paper under it.
    painter->fillRect(page, Qt::white);
    QPainterPath area;
    area.addRect(page);
    Render::paintFill(painter, area, backdrop.color, backdrop.style);
    painter->setRenderHint(QPainter::Antialiasing);
    for (const Shape &shape : backdrop.shapes) {
        Render::paintShape(painter, shape);
    }
    painter->restore();
}

void renderShape(QPainter *painter, const Shape &shape, const SlideMaster *master)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    Render::paintShape(painter, withMasterDefaults(shape, master));
    painter->restore();
}

void renderSlide(QPainter *painter, const Slide &slide, const QSizeF &slideSize, const SlideBackdrop &backdrop,
                 int buildStep, qreal buildProgress)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    paintBackdrop(painter, backdrop, slideSize);
    const SlideMaster *typeMaster = backdrop.hasTypeMaster ? &backdrop.typeMaster : nullptr;
    for (const Shape &shape : slide.shapes) {
        if (shape.buildStep > buildStep) {
            continue; // appears on a later click
        }
        // The master supplies defaults for runs that never chose their own,
        // so restyling a deck is one edit rather than one per text box.
        const bool arriving = shape.buildStep > 0 && shape.buildStep == buildStep;
        painter->save();
        const bool draw = !arriving
            || applyBuildAnimation(painter, shape, slideSize, qBound(0.0, buildProgress, 1.0));
        if (draw) {
            Render::paintShape(painter, withMasterDefaults(shape, typeMaster));
        }
        painter->restore();
    }
    painter->restore();
}

void renderSlide(QPainter *painter, const Presentation &deck, const Slide &slide, int buildStep, qreal buildProgress)
{
    renderSlide(painter, slide, deck.slideSize, backdropFor(deck, slide), buildStep, buildProgress);
}

void renderSlide(QPainter *painter, const Slide &slide, const QSizeF &slideSize, int buildStep,
                 const SlideMaster *master, qreal buildProgress)
{
    renderSlide(painter, slide, slideSize, backdropFor(master), buildStep, buildProgress);
}

QPixmap renderSlideToPixmap(const Slide &slide, const QSizeF &slideSize, const QSize &pixels,
                             int buildStep, const SlideMaster *master)
{
    QPixmap pixmap(pixels);
    pixmap.fill(master != nullptr && master->background.isValid() ? master->background
                                                                 : QColor(Qt::white));
    QPainter painter(&pixmap);
    const qreal scaleX = pixels.width() / slideSize.width();
    const qreal scaleY = pixels.height() / slideSize.height();
    painter.scale(scaleX, scaleY);
    renderSlide(&painter, slide, slideSize, buildStep, master);
    return pixmap;
}

QImage renderSlideToImage(const Presentation &deck, const Slide &slide, const QSize &pixels)
{
    QImage image(pixels, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.scale(pixels.width() / deck.slideSize.width(), pixels.height() / deck.slideSize.height());
    renderSlide(&painter, deck, slide);
    painter.end();
    return image;
}

} // namespace QindaOffice::Show
