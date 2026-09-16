// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_page_render.h"

#include "ink_persistence.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>

namespace QindaOffice::Apps::Notes {

namespace {

// AGENT-GUARD: these must stay in step with NotePageView::syncBlocksGeometry()
// — the on-screen block layout derives from the block index too, and print
// would otherwise put typed text somewhere the user never saw it.
constexpr qreal kBlockLeft = 40;
constexpr qreal kBlockTop = 40;
constexpr qreal kBlockStride = 100;
constexpr qreal kBlockHeight = 90;

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

void paintTextBlocks(QPainter *painter, const QByteArray &blocksJson, const QRectF &page)
{
    const QJsonArray list = QJsonDocument::fromJson(blocksJson)
                                .object()
                                .value(QStringLiteral("blocks"))
                                .toArray();
    painter->setPen(QColor(0x20, 0x20, 0x28));
    int index = 0;
    for (const QJsonValue &value : list) {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("type")).toString() != QLatin1String("text")) {
            continue;
        }
        const QRectF box(page.left() + kBlockLeft, page.top() + kBlockTop + index * kBlockStride,
                         page.width() - 2 * kBlockLeft, kBlockHeight);
        painter->drawText(box, Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop,
                          object.value(QStringLiteral("text")).toString());
        ++index;
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
    paintTextBlocks(painter, page.envelope.blocksJson, rect);
}

} // namespace QindaOffice::Apps::Notes
