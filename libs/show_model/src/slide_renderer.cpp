// SPDX-License-Identifier: GPL-3.0-or-later
#include "slide_renderer.h"

#include "render_parts.h"

#include <QPainter>
#include <QPixmap>

#include <cmath>

namespace QindaOffice::Show {

namespace {

// Applies the shape's arrival animation at `progress` (0 = not yet there,
// 1 = fully arrived). Returns false when the shape should not be drawn at
// all yet. The transform is left on the painter for the caller's save/
// restore pair to undo.
// The box a shape paints in: its rect grown by its outline.
QRectF paintedBox(const Shape &shape)
{
    const qreal margin = qMax<qreal>(1.0, shape.strokeWidth);
    return shape.rect.normalized().adjusted(-margin, -margin, margin, margin);
}

// An emphasis effect at `progress` of its step (M67): Spin turns once
// about the centre, Grow/Shrink scales to 150 % — and stays there after
// its step during the show (`after`).
void applyEmphasis(QPainter *painter, const Shape &shape, qreal progress, bool after)
{
    const QPointF centre = shape.rect.normalized().center();
    const qreal eased = after ? 1.0 : 0.5 - std::cos(progress * M_PI) / 2.0; // ease in-out
    if (shape.animation == ShapeAnimation::Spin && !after) {
        painter->translate(centre);
        painter->rotate(360.0 * eased);
        painter->translate(-centre);
    } else if (shape.animation == ShapeAnimation::GrowShrink) {
        const qreal scale = 1.0 + 0.5 * eased;
        painter->translate(centre);
        painter->scale(scale, scale);
        painter->translate(-centre);
    }
}

// The part of `box` a wipe from the `direction` edge has revealed at
// `amount` (0..1).
QRectF wipedPart(const QRectF &box, int direction, qreal amount)
{
    switch (direction) {
    case Direction::Top: return QRectF(box.left(), box.top(), box.width(), box.height() * amount);
    case Direction::Left: return QRectF(box.left(), box.top(), box.width() * amount, box.height());
    case Direction::Right: return QRectF(box.right() - box.width() * amount, box.top(), box.width() * amount, box.height());
    default: return QRectF(box.left(), box.bottom() - box.height() * amount, box.width(), box.height() * amount);
    }
}

// Split's four ways (M91): a band opening from the middle (out) or two
// closing in from the edges (in), vertical or horizontal.
QPainterPath splitPart(const QRectF &box, int direction, qreal amount)
{
    QPainterPath area;
    const QPointF c = box.center();
    switch (direction) {
    case Direction::HorizontalOut:
        area.addRect(QRectF(box.left(), c.y() - box.height() * amount / 2, box.width(), box.height() * amount));
        break;
    case Direction::HorizontalIn:
        area.addRect(QRectF(box.left(), box.top(), box.width(), box.height() * amount / 2));
        area.addRect(QRectF(box.left(), box.bottom() - box.height() * amount / 2, box.width(), box.height() * amount / 2));
        break;
    case Direction::VerticalIn:
        area.addRect(QRectF(box.left(), box.top(), box.width() * amount / 2, box.height()));
        area.addRect(QRectF(box.right() - box.width() * amount / 2, box.top(), box.width() * amount / 2, box.height()));
        break;
    default: // vertical out
        area.addRect(QRectF(c.x() - box.width() * amount / 2, box.top(), box.width() * amount, box.height()));
        break;
    }
    return area;
}

// An entrance (`in`) or exit effect at `progress` (0..1 of the effect;
// M91: Effect Options directions, Peek, Float and exits — an exit runs its
// entrance backwards towards its edge). Returns false when the shape is not
// drawn at all. The transform is left on the painter for the caller's
// save/restore pair to undo.
bool applyEffect(QPainter *painter, const Shape &shape, ShapeAnimation animation, int direction, const QSizeF &slideSize,
                 qreal progress, bool in)
{
    if (isEmphasis(animation) && in) {
        applyEmphasis(painter, shape, qBound(0.0, progress, 1.0), false);
        return true; // on the slide all along
    }
    if (animation == ShapeAnimation::Appear) {
        // Appear / Disappear are instantaneous: there or not.
        return in ? progress > 0.0 : progress <= 0.0;
    }
    // How much of the shape is there: 0 gone, 1 fully arrived.
    const qreal there = in ? progress : 1.0 - progress;
    if (there <= 0.0) {
        return false;
    }
    const qreal eased = 1.0 - (1.0 - there) * (1.0 - there); // out-quad
    const qreal away = 1.0 - eased;
    const int edge = effectiveDirection(animation, direction);
    const QPointF centre = shape.rect.normalized().center();
    const QRectF box = paintedBox(shape);
    switch (animation) {
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
    case ShapeAnimation::FlyInRight:
    case ShapeAnimation::FlyInTop:
    case ShapeAnimation::FlyInBottom: {
        // From (to) the edge, corners both ways.
        const qreal dx = (edge & Direction::Left) != 0 ? -slideSize.width() : (edge & Direction::Right) != 0 ? slideSize.width() : 0.0;
        const qreal dy = (edge & Direction::Top) != 0 ? -slideSize.height() : (edge & Direction::Bottom) != 0 ? slideSize.height() : 0.0;
        painter->translate(dx * away, dy * away);
        break;
    }
    case ShapeAnimation::Wipe:
        // An entrance sweeps in from its edge; an exit wipes away from it.
        painter->setClipRect(in ? wipedPart(box, edge, eased) : wipedPart(box, edge == Direction::Top ? Direction::Bottom
                                                                              : edge == Direction::Bottom ? Direction::Top
                                                                              : edge == Direction::Left   ? Direction::Right
                                                                                                          : Direction::Left,
                                                                           eased),
                             Qt::IntersectClip);
        break;
    case ShapeAnimation::Split:
        painter->setClipPath(splitPart(box, edge, eased), Qt::IntersectClip);
        break;
    case ShapeAnimation::Peek: {
        // Slides in from (out to) its edge, seen only inside its own box.
        painter->setClipRect(box, Qt::IntersectClip);
        const qreal dx = edge == Direction::Left ? -box.width() : edge == Direction::Right ? box.width() : 0.0;
        const qreal dy = edge == Direction::Top ? -box.height() : edge == Direction::Left || edge == Direction::Right ? 0.0 : box.height();
        painter->translate(dx * away, dy * away);
        break;
    }
    case ShapeAnimation::Float: {
        // A short glide with a fade, up (Float Up) or down.
        painter->setOpacity(painter->opacity() * eased);
        const qreal up = edge == Direction::Bottom ? -1.0 : 1.0;
        painter->translate(0, (in ? up : -up) * slideSize.height() * 0.1 * away);
        break;
    }
    case ShapeAnimation::Spin:
    case ShapeAnimation::GrowShrink:
        painter->setOpacity(painter->opacity() * eased); // an exit of an emphasis kind fades
        break;
    case ShapeAnimation::Appear:
        break;
    }
    return true;
}

bool applyBuildAnimation(QPainter *painter, const Shape &shape, const QSizeF &slideSize, qreal progress)
{
    return applyEffect(painter, shape, shape.animation, shape.animationDirection, slideSize, progress, true);
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
    // AGENT-NOTE: buildStep 1000 is the finished slide the editor, the
    // thumbnails and print draw — as designed, no emphasis applied. Only a
    // running show keeps a Grow/Shrink at 150 % after its step.
    const bool showing = buildStep < 1000;
    for (const Shape &shape : slide.shapes) {
        // M75: a video or sound is on the slide before its play click; in
        // the show one that hides while not playing is drawn by the player
        // only while it plays.
        if (shape.media.isMedia()) {
            if (showing && shape.media.hideWhenStopped) {
                continue;
            }
            painter->save();
            Render::paintShape(painter, shape);
            painter->restore();
            continue;
        }
        if (shape.buildStep > buildStep && !isEmphasis(shape.animation)) {
            continue; // appears on a later click
        }
        // M91: an exit takes the shape off at its click (the editor, the
        // thumbnails and print draw every shape).
        const bool exiting = showing && shape.exit.isSet() && shape.exit.step == buildStep;
        if (showing && shape.exit.isSet() && shape.exit.step < buildStep) {
            continue;
        }
        // The master supplies defaults for runs that never chose their own,
        // so restyling a deck is one edit rather than one per text box.
        const bool arriving = shape.buildStep > 0 && shape.buildStep == buildStep;
        painter->save();
        if (showing && !arriving && isEmphasis(shape.animation) && shape.buildStep > 0 && shape.buildStep < buildStep) {
            applyEmphasis(painter, shape, 1.0, true);
        }
        bool draw = !arriving || applyBuildAnimation(painter, shape, slideSize, qBound(0.0, buildProgress, 1.0));
        if (draw && exiting && !arriving) {
            draw = applyEffect(painter, shape, shape.exit.animation, shape.exit.direction, slideSize,
                               qBound(0.0, buildProgress, 1.0), false);
        }
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
