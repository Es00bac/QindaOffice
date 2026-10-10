// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_clipboard.h"

#include "note_rich_import.h"

#include <QBuffer>
#include <QClipboard>
#include <QGuiApplication>
#include <QImage>

namespace QindaOffice::Apps::Notes {

NoteClipboard::NoteClipboard(QObject *parent)
    : QindaTK::ClipboardAdapter(parent)
{
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &QindaTK::ClipboardAdapter::changed);
}

const QMimeData *NoteClipboard::mimeData() const
{
    const QMimeData *source = QGuiApplication::clipboard()->mimeData();
    if (source == nullptr) {
        return nullptr;
    }
    auto view = std::make_unique<QMimeData>();
    for (const QString &format : source->formats()) {
        if (format == QLatin1String("text/html")) {
            view->setHtml(RichImport::sanitizeHtml(source->html()));
        } else if (!format.startsWith(QLatin1String("image/"))) {
            view->setData(format, source->data(format));
        }
    }
    if (!source->hasHtml() && !source->hasText() && source->hasImage()) {
        // A screenshot: an inline picture, its bytes inside the note.
        const QImage image = qvariant_cast<QImage>(source->imageData());
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        if (!image.isNull() && image.save(&buffer, "PNG")) {
            view->setHtml(RichImport::sanitizeHtml(
                QStringLiteral("<img src=\"data:image/png;base64,%1\">").arg(QString::fromLatin1(png.toBase64()))));
        }
    }
    m_view = std::move(view);
    return m_view.get();
}

bool NoteClipboard::setMimeData(QMimeData *data)
{
    QGuiApplication::clipboard()->setMimeData(data); // takes ownership
    return true;
}

} // namespace QindaOffice::Apps::Notes
