// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_host.h"
#include "note_page_render.h"
#include "note_versions.h"

#include <QBuffer>
#include <QImage>
#include <QPainter>

namespace QindaOffice::Apps::Notes {
namespace {
QVariantMap result(bool ok, const QString &error = {})
{
    return {{QStringLiteral("ok"), ok}, {QStringLiteral("error"), error}};
}
}

QVariantMap NoteHost::pageVersions() const
{
    const Notebook *book = m_document->notebook();
    if (!currentPage() || !book->isOpen()) return result(false, tr("Save this notebook to keep page versions."));
    QString error;
    QVariantList rows;
    for (const auto &entry : Versions::list(book->directory(), m_currentSectionId, m_currentPageId, &error)) {
        rows.append(QVariantMap{{QStringLiteral("id"), entry.id}, {QStringLiteral("title"), entry.title},
                                {QStringLiteral("captured"), entry.captured.toLocalTime().toString(Qt::ISODate)},
                                {QStringLiteral("bytes"), entry.bytes}});
    }
    return {{QStringLiteral("ok"), error.isEmpty()}, {QStringLiteral("error"), error},
            {QStringLiteral("entries"), rows}, {QStringLiteral("enabled"), book->versionsEnabled()}};
}

QVariantMap NoteHost::pageVersionPreview(const QString &id) const
{
    const Notebook *book = m_document->notebook();
    Ink::PageEnvelope envelope;
    QString error;
    if (!Versions::read(book->directory(), m_currentSectionId, m_currentPageId, id, &envelope, &error))
        return result(false, error);
    // A bounded first-sheet preview uses the same painter as print/PDF.
    const QSizeF paper(794, 1123);
    QImage image(QSize(530, 750), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        painter.scale(image.width() / paper.width(), image.height() / paper.height());
        NotePage page;
        page.id = envelope.id;
        page.title = envelope.title;
        page.envelope = envelope;
        page.loaded = true;
        renderNotePage(&painter, page, paper);
    }
    QByteArray bytes;
    QBuffer output(&bytes);
    output.open(QIODevice::WriteOnly);
    if (!image.save(&output, "PNG")) return result(false, tr("Cannot render this page version."));
    return {{QStringLiteral("ok"), true}, {QStringLiteral("title"), envelope.title},
            {QStringLiteral("image"), QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64())}};
}

QVariantMap NoteHost::savePageVersion()
{
    if (m_pageItem) m_pageItem->commitEdit();
    NotePage *page = currentPage();
    if (!page) return result(false, tr("No page is open."));
    Ink::PageEnvelope content = page->envelope;
    content.title = page->title;
    QString error;
    const bool ok = Versions::capture(m_document->notebook()->directory(), m_currentSectionId, content, true, &error);
    return result(ok, error);
}

QVariantMap NoteHost::restorePageVersion(const QString &id)
{
    if (!m_pageItem || !currentPage()) return result(false, tr("No page is open."));
    Ink::PageEnvelope restored;
    QString error;
    if (!Versions::read(m_document->notebook()->directory(), m_currentSectionId, m_currentPageId,
                        id, &restored, &error)) return result(false, error);
    // Keep today's state before applying an older version, even when timed
    // capture is disabled. A failed capture never changes the visible page.
    const QVariantMap saved = savePageVersion();
    if (!saved.value(QStringLiteral("ok")).toBool()) return saved;
    if (!m_pageItem->restoreEnvelope(restored)) return result(false, tr("Finish the current pen gesture before restoring."));
    return result(true);
}

QVariantMap NoteHost::deletePageVersion(const QString &id)
{
    QString error;
    const bool ok = Versions::remove(m_document->notebook()->directory(), m_currentSectionId, m_currentPageId, id, &error);
    return result(ok, error);
}

QVariantMap NoteHost::keepPageVersions(bool enabled)
{
    Notebook *book = m_document->notebook();
    if (!book->isOpen()) return result(false, tr("Save this notebook first."));
    const bool before = book->versionsEnabled();
    book->setVersionsEnabled(enabled);
    QString error;
    if (!book->save(&error)) {
        book->setVersionsEnabled(before);
        return result(false, error);
    }
    return result(true);
}
} // namespace QindaOffice::Apps::Notes
