// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_blocks.h"

#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace QindaOffice::Apps::Notes {

namespace {

// An image never takes more than this much of the page, so one photograph
// cannot push every other block out of sight.
constexpr qreal kMaxImageHeight = 420;

} // namespace

qreal imageBlockHeight(const QByteArray &imageData, qreal availableWidth)
{
    QImage image;
    if (imageData.isEmpty() || !image.loadFromData(imageData) || image.isNull()
        || image.width() <= 0) {
        // An unreadable image still occupies a slot: silently collapsing it
        // would shuffle every block below without explanation.
        return 60;
    }
    const qreal scale = qMin<qreal>(1.0, availableWidth / image.width());
    return qBound<qreal>(40, image.height() * scale, kMaxImageHeight);
}

QList<PageBlock> layoutBlocks(const QByteArray &blocksJson, qreal pageWidth)
{
    QList<PageBlock> blocks;
    const QJsonArray list = QJsonDocument::fromJson(blocksJson)
                                .object()
                                .value(QStringLiteral("blocks"))
                                .toArray();
    const qreal width = qMax<qreal>(80, pageWidth - 2 * kBlockLeft);
    qreal y = kBlockTop;
    int index = 0;
    for (const QJsonValue &value : list) {
        const QJsonObject object = value.toObject();
        const QString type = object.value(QStringLiteral("type")).toString();
        PageBlock block;
        block.index = index++;
        qreal height = kTextBlockHeight;
        if (type == QLatin1String("check")) {
            block.kind = PageBlock::Kind::Check;
            block.text = object.value(QStringLiteral("text")).toString();
            block.checked = object.value(QStringLiteral("checked")).toBool();
            height = kCheckBlockHeight;
        } else if (type == QLatin1String("image")) {
            block.kind = PageBlock::Kind::Image;
            block.imageData =
                QByteArray::fromBase64(object.value(QStringLiteral("data")).toString().toLatin1());
            height = imageBlockHeight(block.imageData, width);
        } else if (type == QLatin1String("text")) {
            block.kind = PageBlock::Kind::Text;
            block.text = object.value(QStringLiteral("text")).toString();
        } else {
            // An unknown block type from a newer version: skip it, but keep
            // its index so nothing else is renumbered when it is written
            // back.
            continue;
        }
        block.rect = QRectF(kBlockLeft, y, width, height);
        y += height + kBlockGap;
        blocks.append(block);
    }
    return blocks;
}

} // namespace QindaOffice::Apps::Notes
