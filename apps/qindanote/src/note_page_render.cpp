// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_page_render.h"

#include "ink_persistence.h"
#include "note_blocks.h"

#include <QImage>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>

namespace QindaOffice::Apps::Notes {

namespace {

void paintBackground(QPainter *painter, const QString &background, const QRectF &page)
{
    painter->setPen(QPen(QColor(0xc8, 0xc8, 0xd2), 1));
    if (background == QLatin1String("ruled")) {
        for (qreal y = page.top() + 32; y < page.bottom(); y += 28) {
            painter->drawLine(QPointF(page.left() + 24, y), QPointF(page.right() - 24, y));
        }
    } else if (background == QLatin1String("grid")) {
        for (qreal y = page.top() + 24; y < page.bottom(); y += 28) {
            painter->drawLine(QPointF(page.left() + 24, y), QPointF(page.right() - 24, y));
        }
        for (qreal x = page.left() + 24; x < page.right(); x += 28) {
            painter->drawLine(QPointF(x, page.top() + 24), QPointF(x, page.bottom() - 24));
        }
    } else if (background == QLatin1String("dotted")) {
        for (qreal y = page.top() + 24; y < page.bottom(); y += 22) {
            for (qreal x = page.left() + 24; x < page.right(); x += 22) {
                painter->drawPoint(QPointF(x, y));
            }
        }
    }
}

// AGENT-CONTRACT: the layout comes from note_blocks.h, the SAME function
// the screen view positions its widgets with. Computing it twice is how
// printed text ends up somewhere the user never saw it.
void paintBlocks(QPainter *painter, const QByteArray &blocksJson, const QRectF &page)
{
    painter->setPen(QColor(0x20, 0x20, 0x28));
    for (const PageBlock &block : layoutBlocks(blocksJson, page.width())) {
        const QRectF box = block.rect.translated(page.topLeft());
        switch (block.kind) {
        case PageBlock::Kind::Text:
            painter->drawText(box, Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, block.text);
            break;
        case PageBlock::Kind::Check: {
            // A printed checklist needs a real box and a real tick: a bare
            // "[x]" prefix reads as text, not as a checked item.
            const qreal side = qMin<qreal>(14, box.height() - 4);
            const QRectF tick(box.left(), box.center().y() - side / 2, side, side);
            painter->drawRect(tick);
            if (block.checked) {
                painter->drawLine(tick.topLeft() + QPointF(2, side / 2),
                                  tick.bottomLeft() + QPointF(side / 2, -2));
                painter->drawLine(tick.bottomLeft() + QPointF(side / 2, -2),
                                  tick.topRight() + QPointF(-2, 2));
            }
            painter->drawText(box.adjusted(side + 8, 0, 0, 0),
                              Qt::AlignLeft | Qt::AlignVCenter, block.text);
            break;
        }
        case PageBlock::Kind::Image: {
            QImage image;
            if (!block.imageData.isEmpty() && image.loadFromData(block.imageData)
                && !image.isNull()) {
                painter->drawImage(box, image);
            } else {
                painter->drawRect(box);
            }
            break;
        }
        }
    }
}

} // namespace

void renderNotePage(QPainter *painter, const NotePage &page, const QSizeF &pageSize)
{
    const QRectF rect(QPointF(0, 0), pageSize);
    painter->fillRect(rect, Qt::white);
    paintBackground(painter, page.envelope.background, rect);
    painter->save();
    painter->setClipRect(rect);
    for (const Ink::Stroke &stroke : page.envelope.ink.strokes()) {
        Ink::renderStroke(painter, stroke);
    }
    painter->restore();
    paintBlocks(painter, page.envelope.blocksJson, rect);
}

} // namespace QindaOffice::Apps::Notes
