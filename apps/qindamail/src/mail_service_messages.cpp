// SPDX-License-Identifier: GPL-3.0-or-later
// MailService: message actions, attachments and opened .eml files.
#include "mail_service.h"

#include "provider_calls.h"

#include <qindatk/imap_mail_provider.h>
#include <qindatk/local_attachment_store.h>
#include <qindatk/local_productivity_store.h>
#include <qindatk/mail_mime.h>
#include <qindatk/mail_record.h>
#include <qindatk/pop3_mail_provider.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QUrl>

namespace QindaOffice::Apps::Mail {

namespace {

constexpr qint64 kMaxMessageFileBytes = 32 * 1024 * 1024;

QVariantMap failure(const QString &state, const QString &error)
{
    return ProviderCalls::result({}, state, error);
}

// A file name that cannot leave the directory it is written to.
QString safeName(const QString &name)
{
    QString out = name;
    out.replace(QLatin1Char('/'), QLatin1Char('_')).replace(QLatin1Char('\\'), QLatin1Char('_'));
    while (out.startsWith(QLatin1Char('.'))) out.remove(0, 1);
    out = out.trimmed().left(200);
    return out.isEmpty() ? QStringLiteral("attachment") : out;
}

} // namespace

QVariantMap MailService::rawMessage(const QString &messageId) const
{
    for (const QVariant &value : m_store->messages()) {
        const QVariantMap record = value.toMap();
        if (record.value(QStringLiteral("id")).toString() == messageId) return record;
    }
    return {};
}

void MailService::editLocalMessage(const QString &batchId, const QString &messageId, const QVariantMap &changes)
{
    QVariantMap record = rawMessage(messageId);
    for (auto it = changes.cbegin(); it != changes.cend(); ++it) record.insert(it.key(), it.value());
    const QVariantMap upserted = m_store->upsert(QStringLiteral("messages"), record);
    QString error;
    if (!upserted.value(QStringLiteral("ok")).toBool()) {
        batchDone(batchId, failure(QStringLiteral("rejected"), upserted.value(QStringLiteral("error")).toString()));
    } else if (!saveStore(&error)) {
        batchDone(batchId, failure(m_store->status() == QLatin1String("conflict") ? QStringLiteral("conflict")
                                                                                    : QStringLiteral("rejected"),
                                   error));
    } else {
        batchDone(batchId, failure(QStringLiteral("accepted"), {}));
    }
}

void MailService::removeRecordAttachments(const QVariantMap &record)
{
    for (const QVariant &value : record.value(QStringLiteral("attachments")).toList()) {
        const QVariantMap attachment = value.toMap();
        if (attachment.value(QStringLiteral("state")).toString() == QLatin1String("local"))
            static_cast<void>(m_attachments->remove(attachment.value(QStringLiteral("id")).toString()));
    }
}

void MailService::runMessageAction(const QString &batchId, const QString &messageId, const QString &action,
                                   const QString &target)
{
    const QVariantMap record = rawMessage(messageId);
    if (record.isEmpty()) {
        const bool draft = !messageById(messageId).isEmpty();
        if (draft && action == QLatin1String("delete")) {
            batchDone(batchId, failure(discardDraft(messageId) ? QStringLiteral("accepted") : QStringLiteral("rejected"),
                                       {}));
        } else {
            batchDone(batchId, failure(draft ? QStringLiteral("accepted") : QStringLiteral("rejected"),
                                       draft ? QString() : tr("The message is no longer stored")));
        }
        return;
    }
    AccountRuntime *rt = runtime(ownerAccountOf(record));
    const auto done = [this, batchId](const QVariantMap &result) { batchDone(batchId, result); };

    if (rt != nullptr && rt->isImap()) {
        const QString mailbox = record.value(QStringLiteral("remoteMailbox")).toString();
        QindaTK::ImapMailProvider *provider = rt->imap(mailbox);
        QString move;
        QString remoteAction = action;
        if (action == QLatin1String("delete")) {
            const QString trash = rt->specialMailbox(QStringLiteral("trash"));
            if (!trash.isEmpty() && trash != mailbox) move = trash;
        } else if (action == QLatin1String("archive")) {
            const QString archive = rt->specialMailbox(QStringLiteral("archive"));
            if (archive == mailbox) {
                batchDone(batchId, failure(QStringLiteral("rejected"), tr("The message is already archived")));
                return;
            }
        } else if (action == QLatin1String("move")) {
            move = target;
        }
        rt->run(QStringLiteral("imap"), provider,
                [provider, messageId, move, remoteAction](const QString &rid) {
                    return move.isEmpty() ? provider->applyAction(rid, messageId, remoteAction)
                                          : provider->moveMessage(rid, messageId, move);
                },
                done);
        return;
    }
    if (rt != nullptr && rt->isPop3()) {
        QindaTK::Pop3MailProvider *pop3 = rt->pop3();
        if (action == QLatin1String("move")) {
            editLocalMessage(batchId, messageId, {{QStringLiteral("localMailbox"), target}});
        } else if (action == QLatin1String("delete")) {
            rt->run(QStringLiteral("pop3"), pop3,
                    [pop3, messageId](const QString &rid) { return pop3->applyAction(rid, messageId, QStringLiteral("delete")); },
                    done);
        } else {
            // Read/flag/archive are local edits of the POP3 mailbox (D-298
            // there): no server, so no credentials.
            const QString rid = ProviderCalls::newRequestId(QStringLiteral("pop3"));
            m_calls->expect(rid, done);
            static_cast<void>(pop3->applyAction(rid, messageId, action));
        }
        return;
    }
    // The local account (and records whose account is gone).
    const QString mailbox = record.value(QStringLiteral("localMailbox")).toString();
    if (action == QLatin1String("read") || action == QLatin1String("unread")) {
        editLocalMessage(batchId, messageId, {{QStringLiteral("unread"), action == QLatin1String("unread")}});
    } else if (action == QLatin1String("flag") || action == QLatin1String("unflag")) {
        editLocalMessage(batchId, messageId, {{QStringLiteral("flagged"), action == QLatin1String("flag")}});
    } else if (action == QLatin1String("archive")) {
        editLocalMessage(batchId, messageId, {{QStringLiteral("localMailbox"), QStringLiteral("Archive")}});
    } else if (action == QLatin1String("move")) {
        editLocalMessage(batchId, messageId, {{QStringLiteral("localMailbox"), target}});
    } else if (action == QLatin1String("delete") && mailbox != QLatin1String("Trash")) {
        editLocalMessage(batchId, messageId, {{QStringLiteral("localMailbox"), QStringLiteral("Trash")}});
    } else if (action == QLatin1String("delete")) {
        // Deleting from Trash is final: the bytes go with the record.
        const QVariantMap removed = m_store->remove(QStringLiteral("messages"), messageId);
        QString error;
        const bool ok = removed.value(QStringLiteral("ok")).toBool() && saveStore(&error);
        if (ok) removeRecordAttachments(record);
        batchDone(batchId, failure(ok ? QStringLiteral("accepted") : QStringLiteral("rejected"),
                                   ok ? QString() : error));
    } else {
        batchDone(batchId, failure(QStringLiteral("rejected"), tr("Unknown action %1").arg(action)));
    }
}

QString MailService::markRead(const QStringList &ids, bool read)
{
    QStringList todo;
    for (const QString &id : ids) {
        const QVariantMap record = rawMessage(id);
        if (!record.isEmpty() && record.value(QStringLiteral("unread")).toBool() == read) todo.append(id);
    }
    const QString batch = beginBatch(QStringLiteral("read"), int(todo.size()));
    for (const QString &id : std::as_const(todo))
        runMessageAction(batch, id, read ? QStringLiteral("read") : QStringLiteral("unread"));
    return batch;
}

QString MailService::setFlagged(const QStringList &ids, bool flagged)
{
    QStringList todo;
    for (const QString &id : ids) {
        const QVariantMap record = rawMessage(id);
        if (!record.isEmpty() && record.value(QStringLiteral("flagged")).toBool() != flagged) todo.append(id);
    }
    const QString batch = beginBatch(QStringLiteral("flag"), int(todo.size()));
    for (const QString &id : std::as_const(todo))
        runMessageAction(batch, id, flagged ? QStringLiteral("flag") : QStringLiteral("unflag"));
    return batch;
}

QString MailService::archive(const QStringList &ids)
{
    const QString batch = beginBatch(QStringLiteral("archive"), int(ids.size()));
    for (const QString &id : ids) runMessageAction(batch, id, QStringLiteral("archive"));
    return batch;
}

QString MailService::deleteMessages(const QStringList &ids)
{
    const QString batch = beginBatch(QStringLiteral("delete"), int(ids.size()));
    for (const QString &id : ids) runMessageAction(batch, id, QStringLiteral("delete"));
    return batch;
}

QString MailService::moveTo(const QStringList &ids, const QString &folderKey)
{
    const QString account = folderKey.section(QLatin1Char(':'), 1, 1);
    const QString prefix = QStringLiteral("acct:") + account + QLatin1Char(':');
    const QString mailbox = folderKey.startsWith(prefix) ? folderKey.mid(prefix.size()) : QString();
    const QString batch = beginBatch(QStringLiteral("move"), int(ids.size()));
    for (const QString &id : ids) {
        const QVariantMap record = rawMessage(id);
        if (mailbox.isEmpty() || record.isEmpty()) {
            batchDone(batch, failure(QStringLiteral("rejected"), tr("Choose a folder to move to")));
        } else if (ownerAccountOf(record) != account) {
            batchDone(batch, failure(QStringLiteral("rejected"), tr("Messages move only within their own account")));
        } else if (folderKeyOf(record) == folderKey) {
            batchDone(batch, failure(QStringLiteral("accepted"), {}));
        } else {
            runMessageAction(batch, id, QStringLiteral("move"), mailbox);
        }
    }
    return batch;
}

void MailService::withAttachment(const QString &messageId, const QString &attachmentId,
                                 std::function<void(const QVariantMap &)> done)
{
    const QVariantMap stored = m_attachments->read(attachmentId);
    if (stored.value(QStringLiteral("ok")).toBool()) {
        done(stored);
        return;
    }
    const QVariantMap record = rawMessage(messageId);
    AccountRuntime *rt = runtime(ownerAccountOf(record));
    if (record.isEmpty() || rt == nullptr || !(rt->isImap() || rt->isPop3())) {
        done(failure(QStringLiteral("rejected"), tr("The attachment is not stored on this computer")));
        return;
    }
    const auto after = [this, attachmentId, done](const QVariantMap &result) {
        if (!result.value(QStringLiteral("ok")).toBool()) {
            done(result);
            return;
        }
        done(m_attachments->read(attachmentId));
    };
    if (rt->isImap()) {
        QindaTK::ImapMailProvider *provider = rt->imap(record.value(QStringLiteral("remoteMailbox")).toString());
        rt->run(QStringLiteral("imap"), provider,
                [provider, messageId, attachmentId](const QString &rid) {
                    return provider->fetchAttachment(rid, messageId, attachmentId);
                },
                after);
    } else {
        QindaTK::Pop3MailProvider *pop3 = rt->pop3();
        rt->run(QStringLiteral("pop3"), pop3,
                [pop3, messageId, attachmentId](const QString &rid) {
                    return pop3->fetchAttachment(rid, messageId, attachmentId);
                },
                after);
    }
}

QString MailService::fetchAttachment(const QString &messageId, const QString &attachmentId)
{
    const QString batch = beginBatch(QStringLiteral("attachment"), 1);
    withAttachment(messageId, attachmentId, [this, batch](const QVariantMap &read) {
        batchDone(batch, read.value(QStringLiteral("ok")).toBool() ? failure(QStringLiteral("accepted"), {}) : read);
    });
    return batch;
}

QString MailService::openAttachment(const QString &messageId, const QString &attachmentId)
{
    const QString batch = beginBatch(QStringLiteral("open"), 1);
    withAttachment(messageId, attachmentId, [this, batch](const QVariantMap &read) {
        if (!read.value(QStringLiteral("ok")).toBool()) {
            batchDone(batch, read.contains(QStringLiteral("state")) ? read
                                                                     : failure(QStringLiteral("rejected"),
                                                                               read.value(QStringLiteral("error")).toString()));
            return;
        }
        // AGENT-GUARD: only bytes from the attachment store are opened, from a
        // private per-process directory (0700), never a path a message names.
        const QString dir = QDir::tempPath() + QStringLiteral("/qindamail-")
            + QString::number(QCoreApplication::applicationPid());
        QDir().mkpath(dir);
        QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        const QString path = dir + QLatin1Char('/') + safeName(read.value(QStringLiteral("name")).toString());
        QSaveFile file(path);
        const QByteArray bytes = read.value(QStringLiteral("bytes")).toByteArray();
        const bool written = file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
        if (!written) {
            batchDone(batch, failure(QStringLiteral("rejected"), tr("Could not write %1").arg(path)));
            return;
        }
        const QUrl url = QUrl::fromLocalFile(path);
        const bool opened = m_openUrl ? m_openUrl(url) : QDesktopServices::openUrl(url);
        batchDone(batch, failure(opened ? QStringLiteral("accepted") : QStringLiteral("rejected"),
                                 opened ? QString() : tr("No application opens this attachment")));
    });
    return batch;
}

QString MailService::saveAttachmentAs(const QString &messageId, const QString &attachmentId, const QUrl &url)
{
    const QString batch = beginBatch(QStringLiteral("save"), 1);
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    withAttachment(messageId, attachmentId, [this, batch, path](const QVariantMap &read) {
        if (!read.value(QStringLiteral("ok")).toBool()) {
            batchDone(batch, failure(QStringLiteral("rejected"), read.value(QStringLiteral("error")).toString()));
            return;
        }
        QSaveFile file(path);
        const QByteArray bytes = read.value(QStringLiteral("bytes")).toByteArray();
        const bool written = file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
        batchDone(batch, failure(written ? QStringLiteral("accepted") : QStringLiteral("rejected"),
                                 written ? QString() : tr("Could not write %1").arg(path)));
    });
    return batch;
}

QString MailService::importMessageFile(const QString &path)
{
    const QString local = path.startsWith(QStringLiteral("file:")) ? QUrl(path).toLocalFile() : path;
    QFile file(local);
    if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxMessageFileBytes) {
        emit notify(tr("Could not open the message"), local, QStringLiteral("danger"));
        return {};
    }
    const QByteArray raw = file.readAll();
    // The same file opened twice is the same message.
    const QString id = QStringLiteral("local:eml:")
        + QString::fromLatin1(QCryptographicHash::hash(raw, QCryptographicHash::Sha256).toHex().left(24));
    const QindaTK::Mime::Parsed parsed = QindaTK::Mime::parse(raw);
    QVariantMap record = QindaTK::MailRecord::fromParsed(id, parsed, false);
    QVariantList attachments;
    const QVariantList listed = record.value(QStringLiteral("attachments")).toList();
    for (qsizetype i = 0; i < listed.size(); ++i) {
        QVariantMap attachment = listed.at(i).toMap();
        for (const QindaTK::Mime::Part &part : parsed.attachments) {
            if (part.section != attachment.value(QStringLiteral("section")).toString()) continue;
            const QVariantMap imported = m_attachments->importBytes(attachment.value(QStringLiteral("id")).toString(),
                                                                    part.name, part.mimeType, part.data);
            if (imported.value(QStringLiteral("ok")).toBool()) attachment.insert(QStringLiteral("state"), QStringLiteral("local"));
        }
        attachments.append(attachment);
    }
    record.insert(QStringLiteral("attachments"), attachments);
    record.insert(QStringLiteral("localMailbox"), QStringLiteral("Inbox"));
    record.insert(QStringLiteral("unread"), false);
    record.insert(QStringLiteral("flagged"), false);
    record.insert(QStringLiteral("sourceFile"), local);
    static_cast<void>(m_store->upsert(QStringLiteral("messages"), record));
    QString error;
    if (!saveStore(&error)) {
        emit notify(tr("Could not keep the message"), error, QStringLiteral("danger"));
    }
    refreshDerived();
    setCurrentFolderKey(QStringLiteral("acct:local:Inbox"));
    setSelectedMessageId(id);
    return id;
}

} // namespace QindaOffice::Apps::Mail
