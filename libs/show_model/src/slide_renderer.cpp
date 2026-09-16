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

void renderShape(QPainter *painter, const Shape &shape)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    paintShape(painter, shape);
    painter->restore();
}

void renderSlide(QPainter *painter, const Slide &slide, const QSizeF &slideSize, int buildStep)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->fillRect(QRectF(QPointF(0, 0), slideSize), Qt::white);
    for (const Shape &shape : slide.shapes) {
        if (shape.buildStep > buildStep) {
            continue; // appears on a later click
        }
        paintShape(painter, shape);
    }
    painter->restore();
}

QPixmap renderSlideToPixmap(const Slide &slide, const QSizeF &slideSize, const QSize &pixels,
                             int buildStep)
{
    QPixmap pixmap(pixels);
    pixmap.fill(Qt::white);
    QPainter painter(&pixmap);
    const qreal scaleX = pixels.width() / slideSize.width();
    const qreal scaleY = pixels.height() / slideSize.height();
    painter.scale(scaleX, scaleY);
    renderSlide(&painter, slide, slideSize, buildStep);
    return pixmap;
}

} // namespace QindaOffice::Show
