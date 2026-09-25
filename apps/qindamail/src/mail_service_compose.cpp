// SPDX-License-Identifier: GPL-3.0-or-later
// MailService: drafts, sending, the Sent copy and mailto: (draft_builder.h
// builds the drafts; this file stores and delivers them).
//
// AGENT-CONTRACT (docs/mail.md#sent-copy): after an ACCEPTED send the copy
// is filed exactly once — Gmail and Microsoft 365 file it themselves
// (nothing), another IMAP account gets an APPEND to its Sent mailbox, and
// POP3 accounts (or a failed APPEND) keep a local copy under "On this
// computer ▸ Sent". The draft record goes only after that decision; a send
// that is not accepted never loses the draft or its attachment bytes.
#include "mail_service.h"

#include "draft_builder.h"
#include "provider_calls.h"

#include <qindatk/imap_mail_provider.h>
#include <qindatk/local_attachment_store.h>
#include <qindatk/local_productivity_store.h>
#include <qindatk/smtp_mail_provider.h>

#include <QLocale>
#include <QTimer>
#include <QUuid>

namespace QindaOffice::Apps::Mail {

namespace {

QString newId(const QString &prefix)
{
    return prefix + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString utcNow()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

QString signatureOf(const AccountRuntime *rt)
{
    return rt != nullptr ? rt->account().signature : QString();
}

} // namespace

QVariantMap MailService::newDraft(const QString &accountId) const
{
    const QString account = accountId.isEmpty() ? defaultAccountId() : accountId;
    const QString signature = signatureOf(runtime(account)).trimmed();
    return {
        {QStringLiteral("id"), newId(QStringLiteral("draft:"))},
        {QStringLiteral("accountId"), account},
        {QStringLiteral("to"), QString()},
        {QStringLiteral("cc"), QString()},
        {QStringLiteral("bcc"), QString()},
        {QStringLiteral("subject"), QString()},
        {QStringLiteral("body"), signature.isEmpty() ? QString() : QStringLiteral("\n\n-- \n") + signature},
        {QStringLiteral("attachments"), QVariantList()},
        {QStringLiteral("inReplyTo"), QString()},
        {QStringLiteral("references"), QString()},
    };
}

QVariantMap MailService::replyDraft(const QString &messageId, bool replyAll)
{
    const QVariantMap message = messageById(messageId);
    if (message.isEmpty()) return {};
    const QString owner = ownerAccountOf(message);
    const AccountRuntime *rt = runtime(owner);
    const QString account = rt != nullptr && rt->canSend() ? owner : defaultAccountId();
    QVariantMap draft = Drafts::reply(message, replyAll, ownAddresses(), signatureOf(runtime(account)), QLocale());
    draft.insert(QStringLiteral("id"), newId(QStringLiteral("draft:")));
    draft.insert(QStringLiteral("accountId"), account);
    return draft;
}

QVariantMap MailService::forwardDraft(const QString &messageId)
{
    const QVariantMap message = messageById(messageId);
    if (message.isEmpty()) return {};
    const QString owner = ownerAccountOf(message);
    const AccountRuntime *rt = runtime(owner);
    const QString account = rt != nullptr && rt->canSend() ? owner : defaultAccountId();
    QVariantMap draft = Drafts::forward(message, signatureOf(runtime(account)), QLocale());
    const QString id = newId(QStringLiteral("draft:"));
    draft.insert(QStringLiteral("id"), id);
    draft.insert(QStringLiteral("accountId"), account);
    // The draft owns COPIES of the bytes: discarding it must never delete
    // the forwarded message's attachments.
    QVariantList attachments;
    QStringList missing;
    int n = 0;
    for (const QVariant &value : message.value(QStringLiteral("attachments")).toList()) {
        const QVariantMap source = value.toMap();
        const QVariantMap read = m_attachments->read(source.value(QStringLiteral("id")).toString());
        if (!read.value(QStringLiteral("ok")).toBool()) {
            missing.append(source.value(QStringLiteral("name")).toString());
            continue;
        }
        const QVariantMap copied = m_attachments->importBytes(
            id + QStringLiteral("/fwd") + QString::number(++n), source.value(QStringLiteral("name")).toString(),
            source.value(QStringLiteral("mimeType")).toString(), read.value(QStringLiteral("bytes")).toByteArray());
        if (!copied.value(QStringLiteral("ok")).toBool()) {
            missing.append(source.value(QStringLiteral("name")).toString());
            continue;
        }
        attachments.append(QVariantMap{{QStringLiteral("id"), copied.value(QStringLiteral("id"))},
                                       {QStringLiteral("name"), copied.value(QStringLiteral("name"))},
                                       {QStringLiteral("mimeType"), copied.value(QStringLiteral("mimeType"))},
                                       {QStringLiteral("size"), copied.value(QStringLiteral("size"))},
                                       {QStringLiteral("state"), QStringLiteral("local")}});
    }
    draft.insert(QStringLiteral("attachments"), attachments);
    // Attachments still on the server (not downloaded): the composer says so.
    draft.insert(QStringLiteral("missingAttachments"), missing);
    return draft;
}

QVariantMap MailService::mailtoDraft(const QString &url) const
{
    const QVariantMap parsed = Drafts::parseMailto(url);
    QVariantMap draft = newDraft();
    if (!parsed.value(QStringLiteral("ok")).toBool()) {
        draft.insert(QStringLiteral("error"), parsed.value(QStringLiteral("error")));
        return draft;
    }
    for (const char *key : {"to", "cc", "bcc", "subject", "inReplyTo"}) {
        draft.insert(QString::fromLatin1(key), parsed.value(QString::fromLatin1(key)));
    }
    const QString body = parsed.value(QStringLiteral("body")).toString();
    if (!body.isEmpty()) draft.insert(QStringLiteral("body"), body + draft.value(QStringLiteral("body")).toString());
    if (!draft.value(QStringLiteral("inReplyTo")).toString().isEmpty()) {
        draft.insert(QStringLiteral("references"),
                     QLatin1Char('<') + draft.value(QStringLiteral("inReplyTo")).toString() + QLatin1Char('>'));
    }
    return draft;
}

QVariantMap MailService::saveDraft(const QVariantMap &draft)
{
    QVariantMap record = draft;
    QString id = record.value(QStringLiteral("id")).toString();
    if (id.isEmpty()) id = newId(QStringLiteral("draft:"));
    record.insert(QStringLiteral("id"), id);
    if (record.value(QStringLiteral("accountId")).toString().isEmpty())
        record.insert(QStringLiteral("accountId"), defaultAccountId());
    record.insert(QStringLiteral("updatedAt"), utcNow());
    // Transient composer fields are not stored.
    record.remove(QStringLiteral("isDraft"));
    record.remove(QStringLiteral("missingAttachments"));
    const QVariantMap upserted = m_store->upsert(QStringLiteral("drafts"), record);
    QString error = upserted.value(QStringLiteral("error")).toString();
    const bool ok = upserted.value(QStringLiteral("ok")).toBool() && saveStore(&error);
    return {{QStringLiteral("ok"), ok}, {QStringLiteral("id"), id}, {QStringLiteral("error"), ok ? QString() : error}};
}

bool MailService::discardDraft(const QString &draftId)
{
    QVariantMap draft;
    for (const QVariant &value : m_store->drafts()) {
        if (value.toMap().value(QStringLiteral("id")).toString() == draftId) draft = value.toMap();
    }
    if (draft.isEmpty() || m_sending.contains(draftId)) return false;
    if (!m_store->remove(QStringLiteral("drafts"), draftId).value(QStringLiteral("ok")).toBool() || !saveStore()) {
        return false;
    }
    // Remove bytes nothing else references (a sent copy may share them).
    QSet<QString> used;
    for (const QVariantList &list : {m_store->messages(), m_store->drafts()}) {
        for (const QVariant &value : list) {
            for (const QVariant &a : value.toMap().value(QStringLiteral("attachments")).toList())
                used.insert(a.toMap().value(QStringLiteral("id")).toString());
        }
    }
    for (const QVariant &a : draft.value(QStringLiteral("attachments")).toList()) {
        const QString id = a.toMap().value(QStringLiteral("id")).toString();
        if (!used.contains(id)) static_cast<void>(m_attachments->remove(id));
    }
    return true;
}

QString MailService::send(const QVariantMap &draft)
{
    const QString requestId = ProviderCalls::newRequestId(QStringLiteral("send"));
    QVariantMap record = draft;
    record.insert(QStringLiteral("queued"), false);
    const QVariantMap saved = saveDraft(record);
    if (!saved.value(QStringLiteral("ok")).toBool()) {
        QVariantMap result = ProviderCalls::result(requestId, QStringLiteral("rejected"),
                                                   saved.value(QStringLiteral("error")).toString());
        QTimer::singleShot(0, this, [this, requestId, result] { finishSend(requestId, result); });
        return requestId;
    }
    record.insert(QStringLiteral("id"), saved.value(QStringLiteral("id")));
    if (record.value(QStringLiteral("accountId")).toString().isEmpty())
        record.insert(QStringLiteral("accountId"), defaultAccountId());
    deliver(requestId, record);
    return requestId;
}

void MailService::deliver(const QString &requestId, const QVariantMap &draft)
{
    const QString draftId = draft.value(QStringLiteral("id")).toString();
    const auto reject = [this, requestId, draftId](const QString &state, const QString &error) {
        QVariantMap result = ProviderCalls::result(requestId, state, error);
        result.insert(QStringLiteral("draftId"), draftId);
        QTimer::singleShot(0, this, [this, requestId, result] { finishSend(requestId, result); });
    };
    AccountRuntime *rt = runtime(draft.value(QStringLiteral("accountId")).toString());
    if (rt == nullptr || !rt->canSend()) {
        reject(QStringLiteral("rejected"), tr("Choose an account that can send mail"));
        return;
    }
    if (m_sending.contains(draftId)) {
        reject(QStringLiteral("rejected"), tr("This message is already being sent"));
        return;
    }
    const auto queue = [this, draft](const QString &error) {
        QVariantMap queued = draft;
        queued.insert(QStringLiteral("queued"), true);
        queued.insert(QStringLiteral("lastError"), error);
        static_cast<void>(m_store->upsert(QStringLiteral("drafts"), queued));
        static_cast<void>(saveStore());
    };
    if (!m_online) {
        queue(tr("Waiting for a network connection"));
        reject(QStringLiteral("offline"), tr("You are offline; the message waits in the Outbox"));
        return;
    }
    m_sending.insert(draftId);
    refreshBusy();
    QindaTK::SmtpMailProvider *smtp = rt->smtp();
    QPointer<AccountRuntime> guard(rt);
    rt->run(QStringLiteral("smtp"), smtp, [smtp, draft](const QString &rid) { return smtp->send(rid, draft); },
            [this, guard, draft, draftId, requestId, queue](const QVariantMap &sent) {
                m_sending.remove(draftId);
                QVariantMap result = sent;
                result.insert(QStringLiteral("requestId"), requestId);
                result.insert(QStringLiteral("draftId"), draftId);
                const QString state = sent.value(QStringLiteral("state")).toString();
                if (state == QLatin1String("accepted") && guard != nullptr) {
                    fileSentCopy(guard, draft, result, requestId);
                    return;
                }
                if (state == QLatin1String("offline")) {
                    queue(sent.value(QStringLiteral("error")).toString());
                    result.insert(QStringLiteral("queued"), true);
                } else {
                    QVariantMap kept = draft;
                    kept.insert(QStringLiteral("queued"), false);
                    kept.insert(QStringLiteral("lastError"), sent.value(QStringLiteral("error")));
                    static_cast<void>(m_store->upsert(QStringLiteral("drafts"), kept));
                    static_cast<void>(saveStore());
                }
                finishSend(requestId, result);
            });
}

void MailService::fileSentCopy(AccountRuntime *runtime, const QVariantMap &draft, const QVariantMap &sendResult,
                               const QString &requestId)
{
    const QString draftId = draft.value(QStringLiteral("id")).toString();
    const QString fromAddress = runtime->account().emailAddress;
    const QString fromName = runtime->account().fullName;
    // The draft record goes; its attachment bytes stay only if the local
    // Sent copy still points at them (discardDraft checks references).
    const auto finish = [this, draftId, requestId](QVariantMap result, const QString &copy) {
        result.insert(QStringLiteral("sentCopy"), copy);
        static_cast<void>(discardDraft(draftId));
        finishSend(requestId, result);
    };
    if (runtime->serverFilesSentMail()) {
        finish(sendResult, QStringLiteral("server"));
        return;
    }
    if (!runtime->isImap()) {
        storeLocalSentCopy(draft, fromAddress, fromName);
        finish(sendResult, QStringLiteral("local"));
        return;
    }
    QString sent = runtime->specialMailbox(QStringLiteral("sent"));
    if (sent.isEmpty()) sent = QStringLiteral("Sent");
    const QByteArray bytes = runtime->smtp()->composeMessage(draft).value(QStringLiteral("message")).toByteArray();
    QindaTK::ImapMailProvider *provider = runtime->imap(sent);
    runtime->run(QStringLiteral("imap"), provider,
                 [provider, sent, bytes](const QString &rid) {
                     return provider->append(rid, sent, bytes, {QStringLiteral("\\Seen")});
                 },
                 [this, draft, sendResult, fromAddress, fromName, finish](const QVariantMap &appended) {
                     if (appended.value(QStringLiteral("ok")).toBool()) {
                         finish(sendResult, QStringLiteral("imap"));
                         return;
                     }
                     // Delivered but not filed: keep the copy here and say so.
                     storeLocalSentCopy(draft, fromAddress, fromName);
                     QVariantMap result = sendResult;
                     result.insert(QStringLiteral("sentCopyError"), appended.value(QStringLiteral("error")));
                     emit notify(tr("Sent, but not filed on the server"),
                                 tr("A copy is under On this computer ▸ Sent."), QStringLiteral("warning"));
                     finish(result, QStringLiteral("local"));
                 });
}

void MailService::storeLocalSentCopy(const QVariantMap &draft, const QString &fromAddress, const QString &fromName)
{
    const QString id = newId(QStringLiteral("local:sent:"));
    const QString body = draft.value(QStringLiteral("body")).toString();
    const QString reply = draft.value(QStringLiteral("inReplyTo")).toString();
    const QString references = draft.value(QStringLiteral("references")).toString();
    QString thread = references.section(QLatin1Char('>'), 0, 0).remove(QLatin1Char('<')).trimmed();
    if (thread.isEmpty()) thread = reply;
    if (thread.isEmpty()) thread = id;
    const QVariantMap record{
        {QStringLiteral("id"), id},
        {QStringLiteral("messageId"), id},
        {QStringLiteral("threadId"), thread},
        {QStringLiteral("localMailbox"), QStringLiteral("Sent")},
        {QStringLiteral("sentFromAccount"), draft.value(QStringLiteral("accountId"))},
        {QStringLiteral("sender"), fromName.isEmpty() ? fromAddress : fromName},
        {QStringLiteral("senderAddress"), fromAddress},
        {QStringLiteral("to"), draft.value(QStringLiteral("to"))},
        {QStringLiteral("cc"), draft.value(QStringLiteral("cc"))},
        {QStringLiteral("subject"), draft.value(QStringLiteral("subject"))},
        {QStringLiteral("date"), utcNow()},
        {QStringLiteral("snippet"), body.simplified().left(160)},
        {QStringLiteral("bodyPlain"), body},
        {QStringLiteral("bodyTrustMode"), QStringLiteral("plain")},
        {QStringLiteral("unread"), false},
        {QStringLiteral("flagged"), false},
        {QStringLiteral("attachments"), draft.value(QStringLiteral("attachments")).toList()},
    };
    static_cast<void>(m_store->upsert(QStringLiteral("messages"), record));
    static_cast<void>(saveStore());
}

void MailService::finishSend(const QString &requestId, QVariantMap result)
{
    result.insert(QStringLiteral("requestId"), requestId);
    refreshBusy();
    emit sendFinished(requestId, result);
}

} // namespace QindaOffice::Apps::Mail
