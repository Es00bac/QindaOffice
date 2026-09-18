// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_blocks.h"

#include <QFontMetricsF>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace QindaOffice::Apps::Notes {

namespace {

// An image never takes more than this much of the page, so one photograph
// cannot push every other block out of sight.
constexpr qreal kMaxImageHeight = 420;
constexpr qreal kPageFontPoints = 11;

} // namespace

QFont pageFont()
{
    // AGENT-NOTE: the page is paper (D-004): its typed text keeps one
    // face regardless of the chrome theme or the desktop font, so print
    // and screen agree and a notebook looks the same on every desktop.
    QFont font = QGuiApplication::font();
    font.setPointSizeF(kPageFontPoints);
    return font;
}

qreal textBlockHeight(const QString &text, qreal width)
{
    const QFontMetricsF metrics(pageFont());
    const qreal inner = qMax<qreal>(20, width - kTextPadding * 2);
    const QString measured = text.isEmpty() ? QStringLiteral("Ag") : text;
    const QRectF bounds = metrics.boundingRect(QRectF(0, 0, inner, 100000),
                                               Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop,
                                               measured);
    return qMax<qreal>(kTextBlockMinHeight, std::ceil(bounds.height()) + kTextPadding * 2);
}

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

QJsonArray blocksArray(const QByteArray &blocksJson)
{
    return QJsonDocument::fromJson(blocksJson).object().value(QStringLiteral("blocks")).toArray();
}

QByteArray withBlocksArray(const QByteArray &blocksJson, const QJsonArray &blocks)
{
    QJsonObject root = QJsonDocument::fromJson(blocksJson).object();
    root.insert(QStringLiteral("blocks"), blocks);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

void setBlockRect(QJsonObject *block, const QRectF &rect)
{
    block->insert(QStringLiteral("rect"),
                  QJsonArray{std::round(rect.x()), std::round(rect.y()), std::round(rect.width())});
}

QList<PageBlock> layoutBlocks(const QByteArray &blocksJson, qreal pageWidth)
{
    QList<PageBlock> blocks;
    const QJsonArray list = blocksArray(blocksJson);
    const qreal stackWidth = qMax<qreal>(80, pageWidth - 2 * kBlockLeft);
    qreal y = kBlockTop;
    int index = 0;
    for (const QJsonValue &value : list) {
        const QJsonObject object = value.toObject();
        const QString type = object.value(QStringLiteral("type")).toString();
        PageBlock block;
        block.index = index++;
        qreal x = kBlockLeft;
        qreal width = stackWidth;
        const QJsonArray rect = object.value(QStringLiteral("rect")).toArray();
        if (rect.size() >= 3) {
            block.floating = true;
            x = rect.at(0).toDouble();
            width = qMax<qreal>(60, rect.at(2).toDouble());
        }
        qreal height = kTextBlockMinHeight;
        if (type == QLatin1String("check")) {
            block.kind = PageBlock::Kind::Check;
            block.text = object.value(QStringLiteral("text")).toString();
            block.checked = object.value(QStringLiteral("checked")).toBool();
            block.level = qBound(0, object.value(QStringLiteral("level")).toInt(0), 3);
            height = kCheckBlockHeight;
        } else if (type == QLatin1String("image")) {
            block.kind = PageBlock::Kind::Image;
            block.imageData =
                QByteArray::fromBase64(object.value(QStringLiteral("data")).toString().toLatin1());
            height = imageBlockHeight(block.imageData, width);
        } else if (type == QLatin1String("text")) {
            block.kind = PageBlock::Kind::Text;
            block.text = object.value(QStringLiteral("text")).toString();
            height = textBlockHeight(block.text, width);
        } else {
            // An unknown block type from a newer version: skip it, but keep
            // its index so nothing else is renumbered when it is written
            // back.
            continue;
        }
        if (block.floating) {
            block.rect = QRectF(x, rect.at(1).toDouble(), width, height);
        } else {
            block.rect = QRectF(x, y, width, height);
            y += height + kBlockGap;
        }
        blocks.append(block);
    }
    return blocks;
}

} // namespace QindaOffice::Apps::Notes
