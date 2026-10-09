// SPDX-License-Identifier: GPL-3.0-or-later
// MailService: message files (M64, D-072) — .eml and Outlook .msg opened
// into the local Inbox, mbox mailboxes imported into a local folder, a
// message saved as .eml, every attachment saved at once.
//
// AGENT-CONTRACT (docs/mail-interchange.md): every import becomes RFC 5322
// source first (an Outlook .msg through msg_reader + rfc822_writer) and goes
// through importRawMessage: ONE record shape (MailRecord::fromParsed, like
// the toolkit providers write) with `localSource: true` and the bytes kept
// in <dataDir>/sources. The record id is "local:eml:" + the source's
// SHA-256, so the same message imported twice is one message.
#include "mail_service.h"

#include "mbox.h"
#include "message_content.h"
#include "msg_reader.h"
#include "provider_calls.h"
#include "rfc822_writer.h"

#include <qindatk/local_productivity_store.h>
#include <qindatk/mail_mime.h>
#include <qindatk/mail_record.h>

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QTimer>

namespace QindaOffice::Apps::Mail {

namespace {

constexpr qint64 kMaximumMessageFileBytes = 64 * 1024 * 1024;
constexpr qint64 kMaximumMsgFileBytes = 128 * 1024 * 1024;
const QStringList kFixedLocalFolders{QStringLiteral("Inbox"), QStringLiteral("Drafts"), QStringLiteral("Outbox"),
                                     QStringLiteral("Sent"), QStringLiteral("Archive"), QStringLiteral("Trash")};

QString localPathOf(const QString &pathOrUrl)
{
    return pathOrUrl.startsWith(QStringLiteral("file:")) ? QUrl(pathOrUrl).toLocalFile() : pathOrUrl;
}

bool isCompoundFile(const QByteArray &head)
{
    return head.startsWith(QByteArray::fromHex("d0cf11e0a1b11ae1"));
}

// A file name that cannot leave the folder it is written to.
QString safeFileName(const QString &name, const QString &fallback)
{
    QString out = name;
    for (const QChar c : {QChar(u'/'), QChar(u'\\'), QChar(u':'), QChar(u'\0')}) out.replace(c, QLatin1Char('_'));
    while (out.startsWith(QLatin1Char('.'))) out.remove(0, 1);
    out = out.simplified().left(180);
    return out.isEmpty() ? fallback : out;
}

QString uniquePath(const QDir &dir, const QString &name, QSet<QString> *taken)
{
    const QFileInfo info(name);
    const QString base = info.completeBaseName();
    const QString suffix = info.suffix().isEmpty() ? QString() : QLatin1Char('.') + info.suffix();
    QString candidate = name;
    for (int n = 2; QFileInfo::exists(dir.filePath(candidate)) || taken->contains(candidate); ++n)
        candidate = QStringLiteral("%1 (%2)%3").arg(base).arg(n).arg(suffix);
    taken->insert(candidate);
    return dir.filePath(candidate);
}

} // namespace

struct MailService::MboxJob {
    QindaOffice::Mail::MboxReader reader;
    QString folder;
    QString path;
    int imported = 0;
    int failed = 0;
};

QString MailService::importRawMessage(const QByteArray &raw, const QString &mailbox, const QString &origin,
                                      QString *error)
{
    const QString id = QStringLiteral("local:eml:")
        + QString::fromLatin1(QCryptographicHash::hash(raw, QCryptographicHash::Sha256).toHex().left(24));
    const QindaTK::Mime::Parsed parsed = QindaTK::Mime::parse(raw);
    QVariantMap record = QindaTK::MailRecord::fromParsed(id, parsed, false);
    // Every part is in the kept source: available without a download.
    QVariantList attachments;
    for (const QVariant &value : record.value(QStringLiteral("attachments")).toList()) {
        QVariantMap attachment = value.toMap();
        attachment.insert(QStringLiteral("state"), QStringLiteral("local"));
        attachments.append(attachment);
    }
    record.insert(QStringLiteral("attachments"), attachments);
    record.insert(QStringLiteral("localMailbox"), mailbox);
    record.insert(QStringLiteral("unread"), false);
    record.insert(QStringLiteral("flagged"), false);
    record.insert(QStringLiteral("localSource"), true);
    if (!origin.isEmpty()) record.insert(QStringLiteral("sourceFile"), origin);
    if (!writeSource(id, raw, error)) return {};
    m_contentCache.remove(id);
    const QVariantMap upserted = m_store->upsert(QStringLiteral("messages"), record);
    if (!upserted.value(QStringLiteral("ok")).toBool()) {
        if (error != nullptr) *error = upserted.value(QStringLiteral("error")).toString();
        return {};
    }
    return id;
}

QString MailService::importMessageFile(const QString &path)
{
    const QString local = localPathOf(path);
    QFile file(local);
    if (!file.open(QIODevice::ReadOnly)) {
        emit notify(tr("Could not open the message"), local, QStringLiteral("danger"));
        return {};
    }
    const QByteArray head = file.peek(4096);
    if (QindaOffice::Mail::MboxReader::looksLikeMbox(head) && !local.endsWith(QStringLiteral(".eml"), Qt::CaseInsensitive)) {
        file.close();
        static_cast<void>(importMailbox(QUrl::fromLocalFile(local)));
        return {};
    }
    const bool msg = isCompoundFile(head);
    // A ".msg" that is not an Outlook compound file (other programs use the
    // suffix too) is refused rather than shown as a garbled message.
    if (!msg && local.endsWith(QStringLiteral(".msg"), Qt::CaseInsensitive)) {
        emit notify(tr("Could not open the Outlook message"),
                    tr("%1 is not an Outlook message").arg(QFileInfo(local).fileName()), QStringLiteral("danger"));
        return {};
    }
    if (file.size() > (msg ? kMaximumMsgFileBytes : kMaximumMessageFileBytes)) {
        emit notify(tr("Could not open the message"), tr("%1 is too large").arg(QFileInfo(local).fileName()),
                    QStringLiteral("danger"));
        return {};
    }
    QByteArray raw = file.readAll();
    QString error;
    if (msg) {
        // Outlook .msg → MessageContent → RFC 5322: from here on it is an
        // ordinary message (shown, printed, saved as .eml like any other).
        QindaOffice::Mail::MessageContent content;
        if (!QindaOffice::Mail::readMsg(raw, &content, &error)) {
            emit notify(tr("Could not open the Outlook message"), error, QStringLiteral("danger"));
            return {};
        }
        raw = QindaOffice::Mail::writeRfc822(content);
    }
    const QString id = importRawMessage(raw, QStringLiteral("Inbox"), local, &error);
    if (id.isEmpty() || !saveStore(&error)) {
        emit notify(tr("Could not keep the message"), error, QStringLiteral("danger"));
        return {};
    }
    refreshDerived();
    setCurrentFolderKey(QStringLiteral("acct:local:Inbox"));
    setSelectedMessageId(id);
    return id;
}

QStringList MailService::localMailboxes() const
{
    QStringList names = kFixedLocalFolders;
    QStringList extra;
    for (const QVariant &value : m_store->messages()) {
        const QVariantMap record = value.toMap();
        if (ownerAccountOf(record) != QLatin1String(kLocalAccountId)) continue;
        const QString mailbox = record.value(QStringLiteral("localMailbox")).toString();
        if (!mailbox.isEmpty() && !names.contains(mailbox) && !extra.contains(mailbox)) extra.append(mailbox);
    }
    extra.sort(Qt::CaseInsensitive);
    return names + extra;
}

QString MailService::importMailbox(const QUrl &url)
{
    const QString requestId = beginBatch(QStringLiteral("mbox"), 1);
    const QString path = url.isLocalFile() ? url.toLocalFile() : localPathOf(url.toString());
    auto job = std::make_shared<MboxJob>();
    job->path = path;
    if (!job->reader.open(path)) {
        batchDone(requestId, ProviderCalls::result({}, QStringLiteral("rejected"), job->reader.error()));
        emit notify(tr("Could not import the mailbox"), job->reader.error(), QStringLiteral("danger"));
        return requestId;
    }
    // A local folder named after the file, never one of the fixed folders.
    QString folder = QFileInfo(path).completeBaseName().simplified();
    folder.replace(QLatin1Char(':'), QLatin1Char('-'));
    if (folder.isEmpty()) folder = tr("Imported");
    const QStringList taken = localMailboxes();
    QString candidate = folder;
    for (int n = 2; taken.contains(candidate, Qt::CaseInsensitive); ++n) candidate = QStringLiteral("%1 (%2)").arg(folder).arg(n);
    job->folder = candidate;
    m_mboxJobs.insert(requestId, job);
    setStatusText(tr("Importing %1…").arg(QFileInfo(path).fileName()));
    QTimer::singleShot(0, this, [this, requestId] { importMailboxStep(requestId); });
    return requestId;
}

void MailService::importMailboxStep(const QString &requestId)
{
    const std::shared_ptr<MboxJob> job = m_mboxJobs.value(requestId);
    if (job == nullptr) return;
    // A slice of at most ~40 ms per turn keeps the windows responsive.
    QElapsedTimer slice;
    slice.start();
    QByteArray raw;
    bool more = true;
    while (slice.elapsed() < 40) {
        if (!job->reader.next(&raw)) {
            more = false;
            break;
        }
        QString error;
        if (importRawMessage(raw, job->folder, QString(), &error).isEmpty()) ++job->failed;
        else ++job->imported;
    }
    if (more) {
        // AGENT-NOTE: the views refresh once at the end, not after every
        // slice — rebuilding folders and lists per slice made a large mbox
        // import quadratic.
        m_refreshTimer->stop();
        setStatusText(tr("Importing %1… %n message(s)", nullptr, job->imported).arg(QFileInfo(job->path).fileName()));
        QTimer::singleShot(0, this, [this, requestId] { importMailboxStep(requestId); });
        return;
    }
    m_mboxJobs.remove(requestId);
    QString error;
    const bool saved = saveStore(&error);
    setStatusText(QString());
    refreshDerived();
    const int skipped = job->failed + job->reader.oversized();
    QString message = tr("%n message(s) imported into \"%1\"", nullptr, job->imported).arg(job->folder);
    if (skipped > 0) message += QLatin1Char(' ') + tr("(%n skipped)", nullptr, skipped);
    if (job->reader.limitReached()) message += QLatin1Char(' ') + tr("— stopped at the import limit");
    if (!saved || job->imported == 0) {
        const QString why = !saved ? error : job->reader.error().isEmpty() ? tr("No messages were found") : job->reader.error();
        batchDone(requestId, ProviderCalls::result({}, QStringLiteral("rejected"), why));
        emit notify(tr("Could not import the mailbox"), why, QStringLiteral("danger"));
        return;
    }
    setCurrentFolderKey(QStringLiteral("acct:local:") + job->folder);
    batchDone(requestId, ProviderCalls::result({}, QStringLiteral("accepted"), message));
    emit notify(tr("Mailbox imported"), message, skipped > 0 ? QStringLiteral("warning") : QStringLiteral("success"));
}

QString MailService::saveMessageAs(const QString &messageId, const QUrl &url)
{
    const QString batch = beginBatch(QStringLiteral("save"), 1);
    const QString path = url.isLocalFile() ? url.toLocalFile() : localPathOf(url.toString());
    loadSource(messageId, [this, batch, path, messageId](const QByteArray &source, const QString &) {
        // Without its source (POP3, a local Sent copy) the message is
        // written from its record: plain text, headers and attachments
        // this computer has.
        QByteArray bytes = source;
        if (bytes.isEmpty()) {
            QindaOffice::Mail::MessageContent content = contentOf(messageId);
            const QVariantMap record = rawMessage(messageId);
            for (qsizetype i = 0; i < content.attachments.size(); ++i) {
                const QVariantMap read = attachmentBytes(record, messageId + QLatin1Char('/') + content.attachments.at(i).section);
                if (read.value(QStringLiteral("ok")).toBool()) content.attachments[i].data = read.value(QStringLiteral("bytes")).toByteArray();
            }
            bytes = QindaOffice::Mail::writeRfc822(content);
        }
        QSaveFile file(path);
        const bool written = !messageById(messageId).isEmpty() && file.open(QIODevice::WriteOnly)
            && file.write(bytes) == bytes.size() && file.commit();
        const QString error = written ? QString() : tr("Could not write %1").arg(path);
        emit notify(written ? tr("Message saved") : tr("Could not save the message"),
                    written ? QFileInfo(path).fileName() : error,
                    written ? QStringLiteral("success") : QStringLiteral("danger"));
        batchDone(batch, ProviderCalls::result({}, written ? QStringLiteral("accepted") : QStringLiteral("rejected"), error));
    });
    return batch;
}

QString MailService::saveAllAttachments(const QString &messageId, const QUrl &folder)
{
    // The attachments the reading pane lists: pictures placed inside the
    // body (cid parts) are part of the message, not files to save.
    QSet<QString> inlineSections;
    for (const QindaOffice::Mail::MessageAttachment &part : contentOf(messageId).attachments) {
        if (part.inlinePart && !part.contentId.isEmpty()) inlineSections.insert(part.section);
    }
    QVariantList attachments;
    for (const QVariant &value : messageById(messageId).value(QStringLiteral("attachments")).toList()) {
        const QString section = value.toMap().value(QStringLiteral("section")).toString();
        if (section.isEmpty() || !inlineSections.contains(section)) attachments.append(value);
    }
    const QString batch = beginBatch(QStringLiteral("save"), int(attachments.size()));
    const QDir dir(folder.isLocalFile() ? folder.toLocalFile() : localPathOf(folder.toString()));
    struct Progress {
        QSet<QString> taken;
        int remaining = 0;
        int saved = 0;
        QString error;
    };
    auto progress = std::make_shared<Progress>();
    progress->remaining = int(attachments.size());
    const auto finished = [this, dir, progress] {
        if (--progress->remaining > 0) return;
        emit notify(progress->error.isEmpty() ? tr("Attachments saved") : tr("Some attachments were not saved"),
                    progress->error.isEmpty() ? tr("%n file(s) in %1", nullptr, progress->saved).arg(dir.absolutePath())
                                              : progress->error,
                    progress->error.isEmpty() ? QStringLiteral("success") : QStringLiteral("warning"));
    };
    for (const QVariant &value : attachments) {
        const QVariantMap attachment = value.toMap();
        const QString name = attachment.value(QStringLiteral("name")).toString();
        withAttachment(messageId, attachment.value(QStringLiteral("id")).toString(),
                       [this, batch, dir, name, progress, finished](const QVariantMap &read) {
            QString error;
            if (!read.value(QStringLiteral("ok")).toBool()) {
                error = read.value(QStringLiteral("error")).toString();
            } else {
                const QString path = uniquePath(dir, safeFileName(name, QStringLiteral("attachment")), &progress->taken);
                QSaveFile file(path);
                const QByteArray bytes = read.value(QStringLiteral("bytes")).toByteArray();
                if (!dir.exists() || !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
                    error = tr("Could not write %1").arg(path);
            }
            if (error.isEmpty()) ++progress->saved;
            else if (progress->error.isEmpty()) progress->error = error;
            batchDone(batch, ProviderCalls::result({}, error.isEmpty() ? QStringLiteral("accepted") : QStringLiteral("rejected"), error));
            finished();
        });
    }
    return batch;
}

} // namespace QindaOffice::Apps::Mail
