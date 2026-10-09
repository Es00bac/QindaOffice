// SPDX-License-Identifier: GPL-3.0-or-later
#include "message_document.h"

#include <QBuffer>
#include <QImageReader>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextImageFormat>

namespace QindaOffice::Apps::Mail {

namespace {

constexpr int kMaximumSide = 10000;
constexpr qint64 kMaximumPixels = 40LL * 1000 * 1000;

} // namespace

MessageImages decodeMessageImages(const QindaOffice::Mail::RenderedBody &body)
{
    MessageImages images;
    for (const auto &[url, bytes] : body.images) {
        QBuffer buffer;
        buffer.setData(bytes);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        reader.setAutoTransform(true);
        // AGENT-GUARD: raster formats only — an SVG (or any other format a
        // plugin adds) inside a message part is never rendered.
        static const QList<QByteArray> raster{"png", "jpeg", "jpg", "gif", "bmp", "webp"};
        if (!raster.contains(reader.format().toLower())) continue;
        const QSize size = reader.size();
        if (!size.isValid() || size.width() > kMaximumSide || size.height() > kMaximumSide
            || qint64(size.width()) * size.height() > kMaximumPixels) {
            continue;
        }
        const QImage image = reader.read();
        if (!image.isNull()) images.insert(QUrl(url), image);
    }
    return images;
}

void setBody(QTextDocument *document, const QindaOffice::Mail::RenderedBody &body, const MessageImages &images)
{
    if (document == nullptr) return;
    // AGENT-GUARD (message_document.h): clear() drops the old resources;
    // every URL is registered BEFORE setHtml lays anything out.
    document->clear();
    for (const QString &url : body.imageUrls) {
        const QUrl key(url);
        document->addResource(QTextDocument::ImageResource, key, QVariant::fromValue(images.value(key)));
    }
    document->setHtml(body.html);
}

void constrainImages(QTextDocument *document, const MessageImages &images, qreal maxWidth)
{
    if (document == nullptr || maxWidth <= 0) return;
    struct Change {
        int position;
        int length;
        QTextImageFormat format;
    };
    // Collected first: changing a format while walking the fragments would
    // invalidate the walk.
    QList<Change> changes;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || !fragment.charFormat().isImageFormat()) continue;
            QTextImageFormat format = fragment.charFormat().toImageFormat();
            const QImage image = images.value(QUrl(format.name()));
            const qreal naturalWidth = image.isNull() ? 0 : image.width();
            const qreal naturalHeight = image.isNull() ? 0 : image.height();
            const qreal width = format.hasProperty(QTextFormat::ImageWidth) ? format.width() : naturalWidth;
            qreal height = format.hasProperty(QTextFormat::ImageHeight) ? format.height() : naturalHeight;
            if (width <= maxWidth || width <= 0) continue;
            if (height <= 0 && naturalWidth > 0) height = naturalHeight * width / naturalWidth;
            format.setWidth(maxWidth);
            if (height > 0) format.setHeight(height * maxWidth / width);
            changes.append({fragment.position(), fragment.length(), format});
        }
    }
    QTextCursor cursor(document);
    for (const Change &change : std::as_const(changes)) {
        cursor.setPosition(change.position);
        cursor.setPosition(change.position + change.length, QTextCursor::KeepAnchor);
        cursor.setCharFormat(change.format);
    }
}

} // namespace QindaOffice::Apps::Mail
