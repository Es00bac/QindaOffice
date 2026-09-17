// SPDX-License-Identifier: GPL-3.0-or-later
#include "slide_renderer.h"

#include <QBuffer>
#include <QPainter>
#include <QPixmap>
#include <QAbstractTextDocumentLayout>
#include <QTextDocument>
#include <QTextLayout>
#include <QtMath>

namespace QindaOffice::Show {

namespace {

void drawArrowHead(QPainter *painter, const QPointF &tip, const QPointF &from)
{
    const QLineF line(from, tip);
    if (line.length() < 1) {
        return;
    }
    const qreal angle = line.angle();
    // AGENT-GUARD: barbs trail the tip — polar points at angle ± 150° land
    // BEHIND the direction of travel. The earlier `tip - fromPolar(...)`
    // negated the direction vector and drew every arrow backwards.
    const QPointF left = tip + QLineF::fromPolar(12, angle + 150).p2();
    const QPointF right = tip + QLineF::fromPolar(12, angle - 150).p2();
    painter->drawPolygon({tip, left, right});
}

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

void paintShape(QPainter *painter, const Shape &shape)
{
    painter->save();
    painter->translate(shape.rect.center());
    painter->rotate(shape.rotation);
    painter->translate(-shape.rect.center());

    switch (shape.kind) {
    case ShapeKind::Rectangle:
    case ShapeKind::Ellipse: {
        QRectF rect = shape.rect.normalized();
        if (shape.fill.isValid() && shape.fill.alpha() > 0) {
            painter->setBrush(shape.fill);
        } else {
            painter->setBrush(Qt::NoBrush);
        }
        QPen pen(shape.stroke);
        pen.setWidthF(shape.strokeWidth);
        painter->setPen(shape.strokeWidth > 0 ? pen : Qt::NoPen);
        if (shape.kind == ShapeKind::Rectangle) {
            painter->drawRect(rect);
        } else {
            painter->drawEllipse(rect);
        }
        break;
    }
    case ShapeKind::Line:
    case ShapeKind::Arrow: {
        QPen pen(shape.stroke);
        pen.setWidthF(qMax<qreal>(shape.strokeWidth, 1.5));
        painter->setPen(pen);
        const QPointF from = shape.rect.topLeft();
        const QPointF to = shape.rect.bottomRight();
        painter->drawLine(from, to);
        if (shape.kind == ShapeKind::Arrow) {
            drawArrowHead(painter, to, from);
        }
        break;
    }
    case ShapeKind::Image: {
        QImage image;
        // Auto-detect the format: the canvas stores raw file bytes for any
        // picked image, and forcing "PNG" rendered every JPEG as a dashed
        // placeholder on canvas, in the player and in the PDF.
        image.loadFromData(shape.imagePng);
        if (!image.isNull()) {
            painter->drawImage(shape.rect, image);
        } else {
            painter->setPen(Qt::DashLine);
            painter->drawRect(shape.rect);
        }
        break;
    }
    case ShapeKind::TextBox:
        if (shape.fill.isValid() && shape.fill.alpha() > 0) {
            painter->fillRect(shape.rect.normalized(), shape.fill);
        }
        break; // text painted below
    }

    // Text (text boxes, rect/ellipse content).
    if (!shape.paragraphs.isEmpty()) {
        QRectF rect = shape.rect.normalized().adjusted(6, 4, -6, -4);
        for (const TextParagraph &paragraph : shape.paragraphs) {
            QTextDocument document;
            QTextCursor cursor(&document);
            for (const TextRun &run : paragraph.runs) {
                QTextCharFormat format;
                format.setFontPointSize(run.pointSize);
                format.setFontWeight(run.bold ? QFont::Bold : QFont::Normal);
                format.setFontItalic(run.italic);
                format.setForeground(run.color);
                if (!run.fontFamily.isEmpty()) {
                    format.setFontFamilies(QStringList{run.fontFamily});
                }
                cursor.insertText(run.text, format);
            }
            QTextBlockFormat blockFormat;
            blockFormat.setAlignment(Qt::Alignment(paragraph.alignment));
            blockFormat.setTopMargin(2);
            blockFormat.setBottomMargin(2);
            cursor.select(QTextCursor::Document);
            cursor.mergeBlockFormat(blockFormat);
            document.setTextWidth(rect.width());
            painter->save();
            painter->translate(rect.topLeft());
            QAbstractTextDocumentLayout::PaintContext context;
            context.clip = QRectF(QPointF(0, 0), rect.size());
            document.documentLayout()->draw(painter, context);
            painter->restore();
            rect.adjust(0, document.size().height(), 0, 0);
        }
    }
    painter->restore();
}

} // namespace

Shape withMasterDefaults(const Shape &shape, const SlideMaster *master)
{
    if (master == nullptr) {
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

void renderShape(QPainter *painter, const Shape &shape, const SlideMaster *master)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    paintShape(painter, withMasterDefaults(shape, master));
    painter->restore();
}

void renderSlide(QPainter *painter, const Slide &slide, const QSizeF &slideSize, int buildStep,
                 const SlideMaster *master, qreal buildProgress)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    // The master owns the background; without one a slide is white paper.
    painter->fillRect(QRectF(QPointF(0, 0), slideSize),
                      master != nullptr && master->background.isValid() ? master->background
                                                                        : QColor(Qt::white));
    for (const Shape &shape : slide.shapes) {
        if (shape.buildStep > buildStep) {
            continue; // appears on a later click
        }
        // The master supplies defaults for runs that never chose their own,
        // so restyling a deck is one edit rather than one per text box.
        const Shape styled = withMasterDefaults(shape, master);
        const bool arriving = shape.buildStep > 0 && shape.buildStep == buildStep;
        painter->save();
        const bool draw = !arriving
            || applyBuildAnimation(painter, shape, slideSize, qBound(0.0, buildProgress, 1.0));
        if (draw) {
            paintShape(painter, styled);
        }
        painter->restore();
    }
    painter->restore();
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

} // namespace QindaOffice::Show
