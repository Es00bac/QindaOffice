// SPDX-License-Identifier: GPL-3.0-or-later
// MailService: the RFC 5322 source of a message (M64, D-072) — where it is
// kept, fetching it from an IMAP server, the parsed content the reading pane
// and the printer show, and attachment bytes taken from it.
//
// AGENT-NOTE: QindaTK's providers parse a message at sync time and keep only
// its plain text and attachment list (MailRecord::fromParsed); the HTML body,
// Content-IDs and the part structure are gone (toolkit follow-up in D-072).
// So QindaMail keeps the source itself, in <dataDir>/sources/<sha256(id)>.eml:
//  - local messages (opened .eml/.msg, imported mbox) at import
//    (`localSource: true` on the record);
//  - IMAP messages on first display, downloaded whole by ImapSourceFetcher
//    (`sourceCached: true`) through the account's own credential path;
//  - POP3 messages and local Sent copies have none: they show their record.
// AGENT-GUARD: the file goes with the record (removeSource), and files whose
// record is gone (an IMAP expunge the toolkit applied) are pruned at start.
#include "mail_service.h"

#include "imap_source_fetcher.h"
#include "message_content.h"
#include "message_render.h"
#include "rfc822_reader.h"

#include <qindatk/local_attachment_store.h>
#include <qindatk/local_productivity_store.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTimer>

#include <cstdio>

#include <fcntl.h>
#include <unistd.h>

namespace QindaOffice::Apps::Mail {

using QindaOffice::Mail::MessageAttachment;
using QindaOffice::Mail::MessageContent;

namespace {

constexpr qint64 kMaximumSourceBytes = 64 * 1024 * 1024;
constexpr int kContentCacheSize = 8;

QByteArray readBounded(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > kMaximumSourceBytes) return {};
    return file.readAll();
}

} // namespace

QString MailService::sourcePath(const QString &messageId) const
{
    const QByteArray digest = QCryptographicHash::hash(messageId.toUtf8(), QCryptographicHash::Sha256).toHex();
    return m_dataDir + QStringLiteral("/sources/") + QString::fromLatin1(digest) + QStringLiteral(".eml");
}

bool MailService::writeSource(const QString &messageId, const QByteArray &raw, QString *error)
{
    const QString path = sourcePath(messageId);
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (m_bulkSources) {
        // AGENT-NOTE (M76): an Outlook import writes thousands of sources;
        // QSaveFile syncs each one (5 ms apiece). Here each file still lands
        // atomically (write, then rename over), and the importer calls
        // syncSources() once before it saves the store — the records, the
        // commit point, never reach disk before their sources.
        QFile part(path + QStringLiteral(".part"));
        const bool written = raw.size() <= kMaximumSourceBytes && part.open(QIODevice::WriteOnly)
            && part.write(raw) == raw.size();
        part.close();
        if (written && std::rename(QFile::encodeName(part.fileName()).constData(), QFile::encodeName(path).constData()) == 0)
            return true;
        part.remove();
        if (error != nullptr) *error = raw.size() > kMaximumSourceBytes ? tr("The message is larger than 64 MB") : part.errorString();
        return false;
    }
    QSaveFile file(path);
    const bool ok = raw.size() <= kMaximumSourceBytes && file.open(QIODevice::WriteOnly)
        && file.write(raw) == raw.size() && file.commit();
    if (!ok && error != nullptr) {
        *error = raw.size() > kMaximumSourceBytes ? tr("The message is larger than 64 MB") : file.errorString();
    }
    return ok;
}

void MailService::syncSources()
{
#ifdef Q_OS_LINUX
    const int fd = ::open(QFile::encodeName(m_dataDir + QStringLiteral("/sources")).constData(), O_RDONLY | O_DIRECTORY);
    if (fd >= 0) {
        ::syncfs(fd);
        ::close(fd);
        return;
    }
#endif
    ::sync();
}

void MailService::removeSource(const QVariantMap &record)
{
    if (record.value(QStringLiteral("localSource")).toBool() || record.value(QStringLiteral("sourceCached")).toBool())
        QFile::remove(sourcePath(record.value(QStringLiteral("id")).toString()));
    m_contentCache.remove(record.value(QStringLiteral("id")).toString());
}

QByteArray MailService::storedSource(const QString &messageId) const
{
    const QVariantMap record = rawMessage(messageId);
    if (record.isEmpty()) return {};
    if (record.value(QStringLiteral("localSource")).toBool() || record.value(QStringLiteral("sourceCached")).toBool())
        return readBounded(sourcePath(messageId));
    // Opened before M64: the .eml it came from, while it is still there.
    const QString file = record.value(QStringLiteral("sourceFile")).toString();
    return file.isEmpty() ? QByteArray() : readBounded(file);
}

void MailService::loadSource(const QString &messageId,
                             std::function<void(const QByteArray &, const QString &)> done)
{
    const QByteArray stored = storedSource(messageId);
    if (!stored.isEmpty()) {
        QTimer::singleShot(0, this, [stored, done] { done(stored, QString()); });
        return;
    }
    const QVariantMap record = rawMessage(messageId);
    AccountRuntime *rt = record.isEmpty() ? nullptr : runtime(ownerAccountOf(record));
    if (rt == nullptr || !rt->isImap() || record.value(QStringLiteral("remoteUid")).toLongLong() <= 0
        || record.value(QStringLiteral("remoteSize")).toLongLong() > kMaximumSourceBytes) {
        QTimer::singleShot(0, this, [done] { done({}, QString()); });
        return;
    }
    // Several views of one message share one download.
    QList<std::function<void(const QByteArray &, const QString &)>> &waiting = m_sourceWaiters[messageId];
    waiting.append(std::move(done));
    if (waiting.size() == 1) fetchImapSource(record, rt);
}

void MailService::fetchImapSource(const QVariantMap &record, AccountRuntime *rt)
{
    const QString messageId = record.value(QStringLiteral("id")).toString();
    ImapSourceFetcher *fetcher = rt->sourceFetcher();
    const QString mailbox = record.value(QStringLiteral("remoteMailbox")).toString();
    const qint64 validity = record.value(QStringLiteral("remoteUidValidity")).toLongLong();
    const qint64 uid = record.value(QStringLiteral("remoteUid")).toLongLong();
    rt->run(QStringLiteral("imap"), fetcher,
            [fetcher, mailbox, validity, uid](const QString &rid) {
                return fetcher->fetch(rid, mailbox, validity, uid, kMaximumSourceBytes);
            },
            [this, messageId](const QVariantMap &result) {
                const QByteArray source = result.value(QStringLiteral("source")).toByteArray();
                QVariantMap current = rawMessage(messageId);
                QString error = result.value(QStringLiteral("error")).toString();
                if (source.isEmpty() || current.isEmpty() || !writeSource(messageId, source, &error)) {
                    finishSource(messageId, {}, error);
                    return;
                }
                current.insert(QStringLiteral("sourceCached"), true);
                static_cast<void>(m_store->upsert(QStringLiteral("messages"), current));
                static_cast<void>(saveStore());
                finishSource(messageId, source, QString());
            });
}

void MailService::finishSource(const QString &messageId, const QByteArray &source, const QString &error)
{
    m_contentCache.remove(messageId);
    const auto waiting = m_sourceWaiters.take(messageId);
    // Always on a later turn (the request-id rule of this service).
    QTimer::singleShot(0, this, [this, messageId, waiting, source, error] {
        for (const auto &done : waiting) done(source, source.isEmpty() ? error : QString());
        if (!source.isEmpty()) emit sourceLoaded(messageId);
    });
}

void MailService::pruneSources()
{
    // AGENT-GUARD: only over a store that loaded cleanly — after a failed or
    // recovered load the records are not the truth, and pruning would delete
    // the sources of messages that come back with the next good load.
    const QString status = m_store->status();
    if (status != QLatin1String("loaded") && status != QLatin1String("saved") && status != QLatin1String("modified"))
        return;
    QSet<QString> kept;
    for (const QVariant &value : m_store->messages()) {
        const QVariantMap record = value.toMap();
        if (record.value(QStringLiteral("localSource")).toBool() || record.value(QStringLiteral("sourceCached")).toBool())
            kept.insert(QFileInfo(sourcePath(record.value(QStringLiteral("id")).toString())).fileName());
    }
    const QDir dir(m_dataDir + QStringLiteral("/sources"));
    for (const QString &name : dir.entryList({QStringLiteral("*.eml")}, QDir::Files)) {
        if (!kept.contains(name)) QFile::remove(dir.filePath(name));
    }
}

MessageContent MailService::recordContent(const QVariantMap &record) const
{
    MessageContent content;
    content.subject = record.value(QStringLiteral("subject")).toString();
    content.fromName = record.value(QStringLiteral("sender")).toString();
    content.fromAddress = record.value(QStringLiteral("senderAddress")).toString();
    if (content.fromName == content.fromAddress) content.fromName.clear();
    content.to = record.value(QStringLiteral("to")).toString();
    content.cc = record.value(QStringLiteral("cc")).toString();
    content.bcc = record.value(QStringLiteral("bcc")).toString();
    content.messageId = record.value(QStringLiteral("internetMessageId")).toString();
    content.date = QDateTime::fromString(record.value(QStringLiteral("date")).toString(), Qt::ISODate);
    content.plain = record.value(QStringLiteral("bodyPlain")).toString();
    if (content.plain.isEmpty()) content.plain = record.value(QStringLiteral("body")).toString();
    for (const QVariant &value : record.value(QStringLiteral("attachments")).toList()) {
        const QVariantMap attachment = value.toMap();
        MessageAttachment part;
        part.name = attachment.value(QStringLiteral("name")).toString();
        part.mimeType = attachment.value(QStringLiteral("mimeType")).toString().toLower();
        part.section = attachment.value(QStringLiteral("section")).toString();
        // Pictures already on this computer can be shown below the body, and
        // a downloaded invitation (POP3 keeps parts, not sources) is a meeting.
        const bool local = attachment.value(QStringLiteral("state")).toString() == QLatin1String("local");
        if (local && (QindaOffice::Mail::isDisplayableImage(part.mimeType) || part.mimeType == QLatin1String("text/calendar"))) {
            part.data = m_attachments->read(attachment.value(QStringLiteral("id")).toString())
                            .value(QStringLiteral("bytes")).toByteArray();
        }
        if (part.mimeType == QLatin1String("text/calendar") && content.calendar.isEmpty() && !part.data.isEmpty()
            && part.data.size() < 4 * 1024 * 1024) {
            content.calendar = QString::fromUtf8(part.data);
            static const QRegularExpression method(QStringLiteral("^METHOD:([A-Za-z-]+)\\s*$"),
                                                   QRegularExpression::MultilineOption);
            content.calendarMethod = method.match(content.calendar).captured(1).toUpper();
        }
        content.attachments.append(part);
    }
    return content;
}

MessageContent MailService::contentOf(const QString &messageId) const
{
    if (const auto cached = m_contentCache.value(messageId)) return *cached;
    const QVariantMap record = rawMessage(messageId);
    if (record.isEmpty()) {
        const QVariantMap draft = messageById(messageId);
        return draft.isEmpty() ? MessageContent() : recordContent(draft);
    }
    const QByteArray source = storedSource(messageId);
    if (source.isEmpty()) return recordContent(record);
    auto parsed = std::make_shared<MessageContent>(QindaOffice::Mail::readRfc822(source));
    m_contentCache.insert(messageId, parsed);
    m_contentOrder.removeAll(messageId);
    m_contentOrder.append(messageId);
    while (m_contentOrder.size() > kContentCacheSize) m_contentCache.remove(m_contentOrder.takeFirst());
    return *parsed;
}

QVariantMap MailService::attachmentBytes(const QVariantMap &record, const QString &attachmentId) const
{
    const QVariantMap stored = m_attachments->read(attachmentId);
    if (stored.value(QStringLiteral("ok")).toBool()) return stored;
    const QString recordId = record.value(QStringLiteral("id")).toString();
    const QString prefix = recordId + QLatin1Char('/');
    if (!attachmentId.startsWith(prefix)) return {{QStringLiteral("ok"), false}};
    const QString section = attachmentId.mid(prefix.size());
    // The parsed source (cached); without a source, contentOf is the record
    // and its parts carry no bytes, so nothing matches below.
    const MessageContent content = contentOf(recordId);
    for (const MessageAttachment &part : content.attachments) {
        if (part.section != section || (part.data.isEmpty() && part.embedded == nullptr)) continue;
        return {{QStringLiteral("ok"), true}, {QStringLiteral("id"), attachmentId},
                {QStringLiteral("name"), part.name}, {QStringLiteral("mimeType"), part.mimeType},
                {QStringLiteral("size"), part.data.size()}, {QStringLiteral("bytes"), part.data}};
    }
    return {{QStringLiteral("ok"), false}};
}

} // namespace QindaOffice::Apps::Mail
