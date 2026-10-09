// SPDX-License-Identifier: GPL-3.0-or-later
// MailOrganizer: carrying out what a message's rules decided — tags, read
// state, forwards and redirects through the outgoing queue, alerts,
// copies, then the move or delete last (mail_organizer.h).
#include "mail_organizer.h"

#include "account_runtime.h"
#include "imap_extras.h"
#include "mail_service.h"
#include "provider_calls.h"
#include "smtp_redirector.h"

#include "desktop_notifier.h"
#include "message_content.h"
#include "rfc822_writer.h"

#include <qindatk/imap_mail_provider.h>
#include <qindatk/local_attachment_store.h>
#include <qindatk/local_productivity_store.h>
#include <qindatk/mail_address_validator.h>
#include <qindatk/pop3_mail_provider.h>

#include <QCoreApplication>
#include <QFile>
#include <QLocale>
#include <QTimer>
#include <QUuid>

namespace QindaOffice::Apps::Mail {

using namespace Qt::StringLiterals;
namespace M = QindaOffice::Mail;

namespace {

// The Outbox copy of a redirected message: the same 64 MB bound as a stored source.
constexpr qint64 kMaximumRedirectBytes = 64 * 1024 * 1024;

using Step = std::function<void(MailOrganizer::Done next)>;

// Runs `steps` one after another, then `done`; a step's failure does not
// stop the later ones (a refused forward must not keep the move from happening).
void runSteps(QList<Step> steps, MailOrganizer::Done done)
{
    if (steps.isEmpty()) {
        done(ProviderCalls::result({}, u"accepted"_s));
        return;
    }
    const Step first = steps.takeFirst();
    first([steps, done](const QVariantMap &) { runSteps(steps, done); });
}

// The addresses of an address list the composer would accept; `error` names the first bad one.
QStringList addressesIn(const QString &list, QString *error)
{
    QindaTK::MailAddressValidator validator;
    const QVariantMap parsed = validator.parseList(list);
    QStringList out;
    for (const QVariant &value : parsed.value(u"recipients"_s).toList()) {
        const QVariantMap entry = value.toMap();
        if (!entry.value(u"valid"_s).toBool()) {
            if (error != nullptr) *error = entry.value(u"error"_s).toString();
            return {};
        }
        out.append(entry.value(u"address"_s).toString());
    }
    if (out.isEmpty() && error != nullptr) *error = QCoreApplication::translate("QindaOffice::Apps::Mail::MailOrganizer", "no address");
    return out;
}

} // namespace

void MailOrganizer::carryOut(const QString &messageId, const M::RulePlan &plan, Done done)
{
    const QVariantMap before = record(messageId);
    // 1. Categories, importance and the flag in one local edit.
    QVariantMap changes;
    QStringList names = plan.clearCategories ? QStringList() : before.value(u"categories"_s).toStringList();
    names = M::Categories::normalized(names + plan.addCategories);
    const bool categories = names != before.value(u"categories"_s).toStringList();
    if (categories) changes.insert(u"categories"_s, names);
    if (!plan.importance.isEmpty()) changes.insert(u"importance"_s, plan.importance);
    bool flag = false;
    if (plan.flag == "clear"_L1) {
        flag = before.value(u"flagged"_s).toBool() || !before.value(u"followUp"_s).toMap().isEmpty();
        changes.insert(u"followUp"_s, QVariant());
        changes.insert(u"flagged"_s, false);
    } else if (!plan.flag.isEmpty()) {
        const M::FollowUp follow = M::FollowUps::forPreset(plan.flag, today(), QLocale().firstDayOfWeek());
        changes.insert(u"followUp"_s, M::FollowUps::toVariantMap(follow));
        changes.insert(u"flagged"_s, true);
        flag = true;
    }
    if (categories || flag) changes.insert(u"tagsPending"_s, true);
    if (!changes.isEmpty()) static_cast<void>(editRecord(u"messages"_s, messageId, changes));

    QList<Step> steps;
    QPointer<MailOrganizer> self(this);
    if (categories || flag) {
        steps.append([self, messageId, categories, flag](Done next) {
            if (self == nullptr) return next({});
            self->pushMessageTags(messageId, categories, flag, next);
        });
    }
    if (plan.markRead && before.value(u"unread"_s).toBool()) {
        steps.append([self, messageId](Done next) {
            if (self == nullptr) return next({});
            self->expect(self->m_service->markRead({messageId}, true), next);
        });
    }
    for (const QString &to : plan.forwardTo) {
        steps.append([self, messageId, to](Done next) {
            if (self != nullptr) self->forward(messageId, to);
            next({});
        });
    }
    for (const QString &to : plan.redirectTo) {
        steps.append([self, messageId, to](Done next) {
            if (self == nullptr) return next({});
            self->redirect(messageId, to, next);
        });
    }
    if (!plan.alerts.isEmpty()) {
        const QString who = before.value(u"sender"_s).toString();
        const QString subject = before.value(u"subject"_s).toString();
        steps.append([self, plan, who, subject](Done next) {
            if (self != nullptr) {
                for (const QString &alert : plan.alerts) {
                    const QString body = tr("%1: %2").arg(who, subject);
                    M::DesktopNotifier::notify(alert, body);
                    emit self->m_service->notify(alert, body, u"info"_s);
                }
            }
            next({});
        });
    }
    for (const QString &key : plan.copyTo) {
        steps.append([self, messageId, key](Done next) {
            if (self == nullptr) return next({});
            self->copyMessage(messageId, key, next);
        });
    }
    if (plan.disposes()) {
        steps.append([self, messageId, plan](Done next) {
            if (self == nullptr) return next({});
            self->dispose(messageId, plan, next);
        });
    }
    runSteps(steps, std::move(done));
}

void MailOrganizer::forward(const QString &messageId, const QString &recipients)
{
    QString error;
    const QStringList to = addressesIn(recipients, &error);
    const QVariantMap message = record(messageId);
    if (to.isEmpty()) {
        emit m_service->notify(tr("A rule could not forward a message"), tr("“%1”: %2").arg(recipients, error), u"warning"_s);
        return;
    }
    // AGENT-GUARD: never forward a message back to whoever sent it (a loop
    // between two mailboxes that forward to each other).
    if (to.contains(message.value(u"senderAddress"_s).toString(), Qt::CaseInsensitive)) return;
    QVariantMap draft = m_service->forwardDraft(messageId);
    if (draft.isEmpty()) return;
    draft.insert(u"to"_s, recipients);
    // send() files it in the Outbox first: offline, it waits there.
    static_cast<void>(m_service->send(draft));
}

void MailOrganizer::redirect(const QString &messageId, const QString &recipients, Done done)
{
    QString error;
    const QStringList to = addressesIn(recipients, &error);
    const QVariantMap message = record(messageId);
    if (to.isEmpty() || to.contains(message.value(u"senderAddress"_s).toString(), Qt::CaseInsensitive)) {
        if (to.isEmpty())
            emit m_service->notify(tr("A rule could not redirect a message"), tr("“%1”: %2").arg(recipients, error), u"warning"_s);
        done(ProviderCalls::result({}, u"rejected"_s, error));
        return;
    }
    const QString owner = m_service->ownerAccountOf(message);
    AccountRuntime *rt = m_service->runtime(owner);
    const QString account = rt != nullptr && rt->canSend() ? owner : m_service->defaultAccountId();
    QPointer<MailOrganizer> self(this);
    m_service->loadSource(messageId, [self, messageId, recipients, account, message, done](const QByteArray &source, const QString &) {
        if (self == nullptr) return done({});
        QByteArray bytes = source;
        if (bytes.isEmpty()) bytes = M::writeRfc822(self->m_service->contentOf(messageId));
        // The redirect waits in the Outbox (with its own copy of the bytes,
        // since the same rules may move or delete the message next).
        const QString draftId = u"draft:redirect:"_s + QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QVariantMap draft{{u"id"_s, draftId}, {u"accountId"_s, account}, {u"to"_s, recipients},
                                {u"subject"_s, message.value(u"subject"_s)}, {u"body"_s, QString()},
                                {u"redirectOf"_s, messageId}, {u"queued"_s, true}};
        QString error;
        if (!self->m_service->writeSource(draftId, bytes, &error)
            || !self->m_service->store()->upsert(u"drafts"_s, draft).value(u"ok"_s).toBool() || !self->m_service->saveStore()) {
            done(ProviderCalls::result({}, u"rejected"_s, error));
            return;
        }
        self->m_service->flushOutbox();
        done(ProviderCalls::result({}, u"accepted"_s));
    });
}

void MailOrganizer::deliverQueuedRedirect(const QString &requestId, const QVariantMap &draft)
{
    const QString draftId = draft.value(u"id"_s).toString();
    AccountRuntime *rt = m_service->runtime(draft.value(u"accountId"_s).toString());
    SmtpRedirector *redirector = rt != nullptr ? rt->redirector() : nullptr;
    // AGENT-NOTE: the bytes sit under the draft's own id (redirect() above);
    // storedSource() only serves message records, so read the file itself.
    QByteArray bytes;
    QFile file(m_service->sourcePath(draftId));
    if (file.open(QIODevice::ReadOnly) && file.size() <= kMaximumRedirectBytes) bytes = file.readAll();
    QString error;
    const QStringList to = addressesIn(draft.value(u"to"_s).toString(), &error);
    const auto finish = [this, requestId, draftId](QVariantMap result) {
        result.insert(u"draftId"_s, draftId);
        QTimer::singleShot(0, this, [this, requestId, result] { m_service->finishSend(requestId, result); });
    };
    if (!m_service->online()) {
        finish(ProviderCalls::result(requestId, u"offline"_s, tr("You are offline; the message waits in the Outbox")));
        return;
    }
    if (redirector == nullptr || bytes.isEmpty() || to.isEmpty()) {
        const QString why = redirector == nullptr ? tr("Choose an account that can send mail")
                            : bytes.isEmpty()     ? tr("The message to redirect is gone")
                                                  : error;
        // It stays in the Outbox, stopped, with the reason (as a failed send does).
        QVariantMap kept = draft;
        kept.insert(u"queued"_s, false);
        kept.insert(u"lastError"_s, why);
        static_cast<void>(m_service->store()->upsert(u"drafts"_s, kept));
        static_cast<void>(m_service->saveStore());
        finish(ProviderCalls::result(requestId, u"rejected"_s, why));
        return;
    }
    m_service->m_sending.insert(draftId);
    m_service->refreshBusy();
    QPointer<MailOrganizer> self(this);
    rt->run(u"smtp"_s, redirector, [redirector, to, bytes](const QString &rid) { return redirector->send(rid, to, bytes); },
            [self, requestId, draft, draftId](const QVariantMap &sent) {
                if (self == nullptr) return;
                MailService *service = self->m_service;
                service->m_sending.remove(draftId);
                QVariantMap result = sent;
                result.insert(u"draftId"_s, draftId);
                const QString state = sent.value(u"state"_s).toString();
                if (state == "accepted"_L1) {
                    QFile::remove(service->sourcePath(draftId));
                    static_cast<void>(service->store()->remove(u"drafts"_s, draftId));
                    static_cast<void>(service->saveStore());
                } else if (state != "offline"_L1) {
                    QVariantMap kept = draft;
                    kept.insert(u"queued"_s, false);
                    kept.insert(u"lastError"_s, sent.value(u"error"_s));
                    static_cast<void>(service->store()->upsert(u"drafts"_s, kept));
                    static_cast<void>(service->saveStore());
                }
                service->finishSend(requestId, result);
            });
}

void MailOrganizer::copyMessage(const QString &messageId, const QString &folderKey, Done done)
{
    const QVariantMap message = record(messageId);
    const QString owner = m_service->ownerAccountOf(message);
    const QString account = folderKey.section(u':', 1, 1);
    const QString prefix = u"acct:"_s + account + u':';
    const QString mailbox = folderKey.startsWith(prefix) ? folderKey.mid(prefix.size()) : QString();
    if (message.isEmpty() || mailbox.isEmpty() || account != owner || m_service->folderKeyOf(message) == folderKey) {
        if (account != owner)
            emit m_service->notify(tr("A rule could not copy a message"), tr("Messages copy only within their own account"), u"warning"_s);
        done(ProviderCalls::result({}, account == owner ? u"accepted"_s : u"rejected"_s));
        return;
    }
    AccountRuntime *rt = m_service->runtime(owner);
    if (rt != nullptr && rt->isImap()) {
        ImapExtras *extras = rt->imapExtras();
        const QString from = message.value(u"remoteMailbox"_s).toString();
        const qint64 validity = message.value(u"remoteUidValidity"_s).toLongLong();
        const qint64 uid = message.value(u"remoteUid"_s).toLongLong();
        rt->run(u"imap"_s, extras, [extras, from, validity, uid, mailbox](const QString &rid) {
            return extras->copy(rid, from, validity, uid, mailbox);
        }, done, false);
        return;
    }
    // POP3 and this computer: a second record, with its own copies of the
    // source and the attachment bytes (deleting one must not empty the other).
    const QString id = u"local:copy:"_s + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QVariantMap copy = message;
    copy.insert(u"id"_s, id);
    copy.insert(u"messageId"_s, id);
    copy.insert(u"localMailbox"_s, mailbox);
    if (rt == nullptr) copy.remove(u"remoteAccount"_s);
    QVariantList attachments;
    int n = 0;
    for (const QVariant &value : message.value(u"attachments"_s).toList()) {
        QVariantMap attachment = value.toMap();
        if (attachment.value(u"state"_s).toString() == "local"_L1) {
            const QVariantMap read = m_service->attachmentStore()->read(attachment.value(u"id"_s).toString());
            const QVariantMap imported = m_service->attachmentStore()->importBytes(
                id + u"/att"_s + QString::number(++n), attachment.value(u"name"_s).toString(),
                attachment.value(u"mimeType"_s).toString(), read.value(u"bytes"_s).toByteArray());
            if (imported.value(u"ok"_s).toBool()) attachment.insert(u"id"_s, imported.value(u"id"_s));
        }
        attachments.append(attachment);
    }
    copy.insert(u"attachments"_s, attachments);
    const QByteArray source = m_service->storedSource(messageId);
    QString error;
    if (!source.isEmpty()) static_cast<void>(m_service->writeSource(id, source, &error));
    const bool ok = m_service->store()->upsert(u"messages"_s, copy).value(u"ok"_s).toBool() && m_service->saveStore(&error);
    done(ProviderCalls::result({}, ok ? u"accepted"_s : u"rejected"_s, error));
}

void MailOrganizer::dispose(const QString &messageId, const M::RulePlan &plan, Done done)
{
    if (plan.deletePermanently) {
        const QVariantMap message = record(messageId);
        AccountRuntime *rt = m_service->runtime(m_service->ownerAccountOf(message));
        if (rt != nullptr && rt->isImap()) {
            // Straight out (UID STORE \Deleted + expunge), not into Trash.
            QindaTK::ImapMailProvider *provider = rt->imap(message.value(u"remoteMailbox"_s).toString());
            rt->run(u"imap"_s, provider,
                    [provider, messageId](const QString &rid) { return provider->applyAction(rid, messageId, u"delete"_s); }, done);
            return;
        }
        if (rt != nullptr && rt->isPop3()) {
            expect(m_service->deleteMessages({messageId}), done);   // POP3's delete removes the server copy
            return;
        }
        const bool removed = m_service->store()->remove(u"messages"_s, messageId).value(u"ok"_s).toBool() && m_service->saveStore();
        if (removed) m_service->removeRecordAttachments(message);
        done(ProviderCalls::result({}, removed ? u"accepted"_s : u"rejected"_s));
        return;
    }
    if (plan.deleteMessage) {
        expect(m_service->deleteMessages({messageId}), done);
        return;
    }
    expect(m_service->moveTo({messageId}, plan.moveTo), done);
}

} // namespace QindaOffice::Apps::Mail
