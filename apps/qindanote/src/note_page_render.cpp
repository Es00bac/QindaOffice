// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_page_render.h"

#include "ink_persistence.h"

#include <QImage>
#include <QPainter>

namespace QindaOffice::Apps::Notes {

void paintNoteBackground(QPainter *painter, const QString &background, const QSizeF &pageSize)
{
    // Document surface, not chrome: the guide grey is a literal (D-004).
    painter->setPen(QPen(QColor(0xc8, 0xc8, 0xd2), 0));
    const qreal w = pageSize.width();
    const qreal h = pageSize.height();
    if (background == QLatin1String("ruled")) {
        for (qreal y = 32; y < h; y += 28) {
            painter->drawLine(QPointF(24, y), QPointF(w - 24, y));
        }
    } else if (background == QLatin1String("grid")) {
        for (qreal y = 24; y < h; y += 28) {
            painter->drawLine(QPointF(24, y), QPointF(w - 24, y));
        }
        for (qreal x = 24; x < w; x += 28) {
            painter->drawLine(QPointF(x, 24), QPointF(x, h - 24));
        }
    } else if (background == QLatin1String("dotted")) {
        for (qreal y = 24; y < h; y += 22) {
            for (qreal x = 24; x < w; x += 22) {
                painter->drawPoint(QPointF(x, y));
            }
        }
    }
}

// AGENT-CONTRACT: the layout comes from note_blocks.h, the SAME function
// the screen item paints and edits with. Computing it twice is how printed
// text ends up somewhere the user never saw it.
void paintNoteBlocks(QPainter *painter, const QList<PageBlock> &blocks, PageBlock::Kind kind,
                     int skipIndex)
{
    painter->save();
    painter->setFont(pageFont());
    painter->setPen(QColor(0x20, 0x20, 0x28));
    for (const PageBlock &block : blocks) {
        if (block.kind != kind || block.index == skipIndex) {
            continue;
        }
        const QRectF box = block.rect;
        switch (block.kind) {
        case PageBlock::Kind::Text:
            painter->drawText(box.adjusted(kTextPadding, kTextPadding, -kTextPadding, -kTextPadding),
                              Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, block.text);
            break;
        case PageBlock::Kind::Check: {
            // A printed checklist needs a real box and a real tick: a bare
            // "[x]" prefix reads as text, not as a checked item.
            const qreal indent = block.level * kCheckIndent;
            const qreal side = qMin<qreal>(14, box.height() - 4);
            const QRectF tick(box.left() + indent + 2, box.center().y() - side / 2, side, side);
            painter->setPen(QPen(QColor(0x20, 0x20, 0x28), 1.2));
            painter->drawRect(tick);
            if (block.checked) {
                painter->drawLine(tick.topLeft() + QPointF(2, side / 2),
                                  tick.bottomLeft() + QPointF(side / 2, -2));
                painter->drawLine(tick.bottomLeft() + QPointF(side / 2, -2),
                                  tick.topRight() + QPointF(-2, 2));
            }
            QFont font = pageFont();
            font.setStrikeOut(block.checked);
            painter->setFont(font);
            painter->setPen(block.checked ? QColor(0x20, 0x20, 0x28, 128) : QColor(0x20, 0x20, 0x28));
            painter->drawText(box.adjusted(indent + side + 10, 0, -kTextPadding, 0),
                              Qt::AlignLeft | Qt::AlignVCenter, block.text);
            painter->setFont(pageFont());
            painter->setPen(QColor(0x20, 0x20, 0x28));
            break;
        }
        case PageBlock::Kind::Image: {
            QImage image;
            if (!block.imageData.isEmpty() && image.loadFromData(block.imageData)
                && !image.isNull()) {
                painter->drawImage(box, image);
            } else {
                // An unreadable image says so rather than leaving a gap the
                // user cannot explain.
                painter->setPen(Qt::DashLine);
                painter->drawRect(box);
                painter->drawText(box, Qt::AlignCenter, QObject::tr("Image unavailable"));
                painter->setPen(QColor(0x20, 0x20, 0x28));
            }
            break;
        }
        }
    }
    painter->restore();
}

void renderNotePage(QPainter *painter, const NotePage &page, const QSizeF &pageSize)
{
    const QRectF rect(QPointF(0, 0), pageSize);
    painter->fillRect(rect, Qt::white);
    painter->save();
    painter->setClipRect(rect);
    paintNoteBackground(painter, page.envelope.background, pageSize);
    const QList<PageBlock> blocks = layoutBlocks(page.envelope.blocksJson, pageSize.width());
    paintNoteBlocks(painter, blocks, PageBlock::Kind::Image);
    paintNoteBlocks(painter, blocks, PageBlock::Kind::Text);
    paintNoteBlocks(painter, blocks, PageBlock::Kind::Check);
    Ink::renderStrokes(painter, page.envelope.ink.strokes());
    painter->restore();
}

} // namespace QindaOffice::Apps::Notes
