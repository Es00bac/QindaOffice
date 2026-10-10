// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_page_render.h"

#include "note_markdown.h"
#include "note_rich_render.h"
#include "note_tags.h"

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
    // OneNote's rule lines (M84): narrow, standard and wide ruling; small,
    // standard and large grids. Unknown names (a newer version) draw none.
    const qreal ruled = background == QLatin1String("ruledNarrow") ? 20
        : background == QLatin1String("ruled")                    ? 28
        : background == QLatin1String("ruledWide")                ? 36
                                                                   : 0;
    const qreal grid = background == QLatin1String("gridSmall") ? 14
        : background == QLatin1String("grid")                   ? 28
        : background == QLatin1String("gridLarge")               ? 42
                                                                  : 0;
    if (ruled > 0) {
        for (qreal y = 32; y < h; y += ruled) {
            painter->drawLine(QPointF(24, y), QPointF(w - 24, y));
        }
    } else if (grid > 0) {
        for (qreal y = 24; y < h; y += grid) {
            painter->drawLine(QPointF(24, y), QPointF(w - 24, y));
        }
        for (qreal x = 24; x < w; x += grid) {
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
        case PageBlock::Kind::Markdown:
            paintMarkdown(painter, box, block.text);
            break;
        case PageBlock::Kind::Text:
            if (!block.rich.isEmpty()) {
                // M84 (D-093): formatted text through the same QindaTK
                // layout the live editor uses (note_rich_render.h).
                RichRender::paint(painter, box, block.rich);
                break;
            }
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

QColor notePaper(const QByteArray &blocksJson)
{
    const QColor color = pageColor(blocksJson);
    return color.isValid() ? color : QColor(Qt::white);
}

void renderNotePage(QPainter *painter, const NotePage &page, const QSizeF &pageSize)
{
    const QRectF rect(QPointF(0, 0), pageSize);
    painter->fillRect(rect, notePaper(page.envelope.blocksJson));
    painter->save();
    painter->setClipRect(rect);
    paintNoteBackground(painter, page.envelope.background, pageSize);
    const QList<PageBlock> blocks = layoutBlocks(page.envelope.blocksJson, pageSize.width());
    paintNoteBlocks(painter, blocks, PageBlock::Kind::Image);
    Tags::paintBands(painter, blocks);
    paintNoteBlocks(painter, blocks, PageBlock::Kind::Text);
    paintNoteBlocks(painter, blocks, PageBlock::Kind::Markdown);
    paintNoteBlocks(painter, blocks, PageBlock::Kind::Check);
    Tags::paintIcons(painter, blocks);
    Ink::renderStrokes(painter, page.envelope.ink.strokes());
    painter->restore();
}

} // namespace QindaOffice::Apps::Notes
