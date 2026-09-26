// SPDX-License-Identifier: GPL-3.0-or-later
// OfficeApplication's recovered-documents flow (D-055), split out of
// office_application.cpp to keep both inside the line budget. The window
// side is RecoveryDialog.qml; runOfficeApp (app_runner.cpp) shows it
// before the first document window when the journal holds entries.

#include "office_application.h"

#include "document_host.h"
#include "recovery_journal.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QLocale>
#include <QSet>
#include <QTextDocumentFragment>

namespace QindaOffice::Quick {

namespace {

constexpr qsizetype kPreviewChars = 90;

// One line of the document's own words, so "Untitled document" rows can be
// told apart. Engines differ: QindaWrite journals HTML, the others JSON
// (not shown) or plain text.
QString previewOf(const QByteArray &payload)
{
    const QByteArray head = payload.left(64).trimmed();
    QString text;
    if (head.startsWith('<')) {
        text = QTextDocumentFragment::fromHtml(QString::fromUtf8(payload)).toPlainText();
    } else if (head.startsWith('{') || head.startsWith('[')) {
        return {};
    } else {
        text = QString::fromUtf8(payload.left(4096));
    }
    text = text.simplified();
    if (text.size() > kPreviewChars) {
        text = text.left(kPreviewChars - 1) + QChar(0x2026);
    }
    return text;
}

} // namespace

QVariantList OfficeApplication::pendingRecovery()
{
    // AGENT-GUARD: build the stores HERE — runOfficeApp calls this before
    // any window exists, and a lazily built journal was never consulted.
    ensureStores();
    QVariantList rows;
    QSet<QByteArray> untitledSeen;
    const QLocale locale;
    for (const QString &key : m_journal->pendingKeys()) {
        const auto entry = m_journal->entry(key);
        if (!entry.has_value()) {
            m_journal->clear(key);
            continue;
        }
        const bool untitled = entry->untitled || entry->path.isEmpty();
        // AGENT-NOTE: builds before the restoreFromJournal key fix copied
        // every recovered untitled entry under a fresh key on each launch;
        // a byte-identical untitled entry is such a copy, not a document.
        if (untitled) {
            const QByteArray digest =
                QCryptographicHash::hash(entry->payload, QCryptographicHash::Sha256);
            if (untitledSeen.contains(digest)) {
                m_journal->clear(key);
                continue;
            }
            untitledSeen.insert(digest);
        }
        const QString saved = entry->savedAt.isValid()
            ? locale.toString(entry->savedAt.toLocalTime(), QLocale::ShortFormat)
            : QString();
        rows.append(QVariantMap{
            {QStringLiteral("key"), key},
            {QStringLiteral("title"),
             untitled ? tr("Untitled document") : QFileInfo(entry->path).fileName()},
            {QStringLiteral("detail"),
             untitled ? (saved.isEmpty() ? tr("Never saved") : tr("Never saved · changes from %1").arg(saved))
                      : (saved.isEmpty() ? entry->path : tr("%1 · changes from %2").arg(entry->path, saved))},
            {QStringLiteral("preview"), previewOf(entry->payload)},
        });
    }
    return rows;
}

void OfficeApplication::recoverEntries(const QStringList &keys)
{
    ensureStores();
    for (const QString &key : keys) {
        const auto entry = m_journal->entry(key);
        if (!entry.has_value()) {
            continue;
        }
        if (m_hosts.size() >= kMaxWindows) {
            emit diagnosticReported(
                tr("Window limit reached: %1 documents are already open in this process.")
                    .arg(kMaxWindows));
            break; // remaining entries stay on disk and are offered next time
        }
        DocumentHost *host = createHost();
        if (host == nullptr) {
            break;
        }
        // AGENT-GUARD: a recovered entry is cleared ONLY by a save or an
        // explicit Discard on close (plan section 2.7); a restore the engine
        // refuses keeps the entry (the user saw the reason).
        if (!host->restoreFromJournal(entry->path, entry->untitled, entry->payload, key)) {
            host->deleteLater();
            pruneClosed();
            continue;
        }
        present(host);
        host->showStatusMessage(
            tr("Recovered unsaved changes to %1")
                .arg(entry->untitled || entry->path.isEmpty() ? tr("an untitled document")
                                                              : entry->path),
            0);
    }
}

void OfficeApplication::resolveRecovery(const QStringList &recoverKeys,
                                        const QStringList &discardKeys)
{
    const int before = windowCount();
    recoverEntries(recoverKeys);
    discardEntries(discardKeys);
    emit recoveryResolved(windowCount() > before);
}

void OfficeApplication::discardEntries(const QStringList &keys)
{
    ensureStores();
    for (const QString &key : keys) {
        m_journal->discard(key);
    }
}

} // namespace QindaOffice::Quick
