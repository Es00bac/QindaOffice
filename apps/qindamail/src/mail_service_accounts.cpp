// SPDX-License-Identifier: GPL-3.0-or-later
// MailService: adding, changing, removing, signing in and testing accounts.
//
// AGENT-GUARD: the password argument goes straight to the keyring
// (MailSecrets::store) or, for a test, into a throw-away runtime's provider;
// it is never kept in a member, a result or a log line.
#include "mail_service.h"

#include "account_store.h"
#include "mail_secrets.h"
#include "oauth2_session.h"
#include "provider_calls.h"

#include <qindatk/caldav_provider.h>
#include <qindatk/imap_mail_provider.h>
#include <qindatk/local_attachment_store.h>
#include <qindatk/local_productivity_store.h>
#include <qindatk/pop3_mail_provider.h>

#include <QTimer>
#include <QUrl>
#include <QUuid>

#include <memory>

namespace QindaOffice::Apps::Mail {

using QindaOffice::Mail::MailAccount;

namespace {

// The keyring services a password is written to for this account.
QStringList passwordServices(const MailAccount &account)
{
    QStringList services;
    if (!account.incoming.isEmpty() && account.incoming.authMechanism != QLatin1String("xoauth2"))
        services.append(account.incoming.protocol);
    if (!account.outgoing.isEmpty() && account.outgoing.authMechanism != QLatin1String("xoauth2")
        && !(account.outgoing.sameCredentialsAsIncoming && !account.incoming.isEmpty()))
        services.append(QStringLiteral("smtp"));
    const QString calendar = account.calendar.protocol;
    if ((calendar == QLatin1String("caldav") || calendar == QLatin1String("ews") || calendar == QLatin1String("ics"))
        && account.calendar.authMechanism != QLatin1String("bearer"))
        services.append(calendar);
    return services;
}

int severity(const QString &state)
{
    if (state == QLatin1String("rejected")) return 3;
    if (state == QLatin1String("conflict")) return 2;
    if (state == QLatin1String("offline")) return 1;
    return 0;
}

} // namespace

QVariantMap MailService::addAccount(const QVariantMap &account, const QString &password)
{
    QVariantMap result = m_accountStore->add(account);
    if (!result.value(QStringLiteral("ok")).toBool()) return result;
    const QString id = result.value(QStringLiteral("id")).toString();
    const auto stored = m_accountStore->find(id);
    if (!stored) return result;
    const auto afterSecrets = [this, id, stored] {
        // A calendar file account is imported once into its own calendar.
        if (stored->calendar.protocol == QLatin1String("ics") && QUrl(stored->calendar.url).isLocalFile()) {
            if (AccountRuntime *rt = runtime(id); rt != nullptr && !rt->calendars().isEmpty())
                static_cast<void>(importIcs(QUrl(stored->calendar.url), rt->calendars().first().id));
        }
        if (m_autoSync) static_cast<void>(syncAccount(id));
    };
    const QStringList services = password.isEmpty() ? QStringList() : passwordServices(*stored);
    if (services.isEmpty()) {
        QTimer::singleShot(0, this, afterSecrets);
        return result;
    }
    auto remaining = std::make_shared<int>(int(services.size()));
    for (const QString &service : services) {
        m_secrets->store(id, service, password, [this, id, remaining, afterSecrets](const auto &outcome) {
            if (outcome.status != QindaOffice::Mail::SecretOutcome::Status::Ok) {
                if (AccountRuntime *rt = runtime(id)) rt->setStatus(QStringLiteral("needsSignIn"), outcome.error);
                emit notify(tr("The password was not saved"), outcome.error, QStringLiteral("danger"));
            }
            if (--*remaining == 0) afterSecrets();
        });
    }
    return result;
}

QVariantMap MailService::updateAccount(const QVariantMap &account, const QString &password)
{
    QVariantMap result = m_accountStore->update(account);
    if (!result.value(QStringLiteral("ok")).toBool()) return result;
    const QString id = result.value(QStringLiteral("id")).toString();
    const auto stored = m_accountStore->find(id);
    if (!stored || password.isEmpty()) return result;
    const QStringList services = passwordServices(*stored);
    auto remaining = std::make_shared<int>(int(services.size()));
    for (const QString &service : services) {
        m_secrets->store(id, service, password, [this, id, remaining](const auto &outcome) {
            if (outcome.status != QindaOffice::Mail::SecretOutcome::Status::Ok)
                emit notify(tr("The password was not saved"), outcome.error, QStringLiteral("danger"));
            if (--*remaining == 0 && m_autoSync) static_cast<void>(syncAccount(id));
        });
    }
    return result;
}

QVariantMap MailService::removeAccount(const QString &accountId)
{
    QStringList calendarIds;
    if (const AccountRuntime *rt = runtime(accountId)) {
        for (const CalendarBinding &binding : rt->calendars()) calendarIds.append(binding.id);
    }
    // Writes accounts.json, then removes every keyring secret of the account
    // (AccountStore::remove → MailSecrets::removeAccount), then accountRemoved
    // stops the runtime.
    QVariantMap result = m_accountStore->remove(accountId);
    if (!result.value(QStringLiteral("ok")).toBool()) return result;

    // Its cached records go with it; drafts are the user's words and stay
    // (they fall back to the default account).
    const QString prefix = accountId + QLatin1Char(':');
    int purged = 0;
    for (const QVariant &value : m_store->messages()) {
        const QVariantMap record = value.toMap();
        if (record.value(QStringLiteral("remoteAccount")).toString() != accountId) continue;
        removeRecordAttachments(record);
        purged += m_store->remove(QStringLiteral("messages"), record.value(QStringLiteral("id")).toString())
                      .value(QStringLiteral("ok")).toBool();
    }
    for (const QString &collection : {QStringLiteral("events"), QStringLiteral("tasks")}) {
        const QVariantList rows = collection == QLatin1String("events") ? m_store->events() : m_store->tasks();
        for (const QVariant &value : rows) {
            const QVariantMap record = value.toMap();
            const QString calendar = record.value(QStringLiteral("calendarId")).toString();
            if (record.value(QStringLiteral("remoteAccount")).toString() == accountId || calendarIds.contains(calendar)
                || calendar.startsWith(prefix)) {
                purged += m_store->remove(collection, record.value(QStringLiteral("id")).toString())
                              .value(QStringLiteral("ok")).toBool();
            }
        }
    }
    for (const QVariant &value : m_store->drafts()) {
        QVariantMap draft = value.toMap();
        if (draft.value(QStringLiteral("accountId")).toString() != accountId) continue;
        draft.insert(QStringLiteral("accountId"), QString());
        static_cast<void>(m_store->upsert(QStringLiteral("drafts"), draft));
    }
    QString error;
    if (!saveStore(&error)) {
        emit notify(tr("The account's mail could not be removed from this computer"), error, QStringLiteral("warning"));
    }
    result.insert(QStringLiteral("purgedRecords"), purged);
    m_folderCache.remove(accountId);
    saveFolderCache();
    for (const QString &id : std::as_const(calendarIds)) m_hiddenCalendars.remove(id);
    if (m_currentFolderKey.startsWith(QStringLiteral("acct:") + prefix)) m_currentFolderKey.clear();
    refreshDerived();
    emit calendarsChanged();
    return result;
}

QString MailService::signIn(const QString &accountId)
{
    AccountRuntime *rt = runtime(accountId);
    const QString batch = beginBatch(QStringLiteral("signin"), 1);
    if (rt == nullptr || rt->oauth() == nullptr || !rt->oauthConfigureError().isEmpty()) {
        batchDone(batch, ProviderCalls::result({}, QStringLiteral("rejected"),
                                               rt != nullptr && !rt->oauthConfigureError().isEmpty()
                                                   ? rt->oauthConfigureError()
                                                   : tr("This account does not sign in with a browser")));
        return batch;
    }
    const QString rid = ProviderCalls::newRequestId(QStringLiteral("authorize"));
    QPointer<AccountRuntime> guard(rt);
    m_calls->expect(rid, [this, guard, batch, accountId](const QVariantMap &result) {
        if (guard != nullptr && result.value(QStringLiteral("ok")).toBool()) {
            guard->setStatus(QStringLiteral("idle"));
            static_cast<void>(syncAccount(accountId));
        }
        batchDone(batch, result);
    });
    rt->oauth()->authorize(rid);
    return batch;
}

void MailService::testAccount(const QString &requestId, const QVariantMap &account, const QString &password)
{
    MailAccount candidate = QindaOffice::Mail::AccountStore::normalized(MailAccount::fromVariantMap(account));
    if (candidate.id.isEmpty()) candidate.id = QStringLiteral("test-") + QUuid::createUuid().toString(QUuid::Id128);
    const QStringList errors = candidate.validate();
    if (!errors.isEmpty()) {
        QVariantMap result = ProviderCalls::result(requestId, QStringLiteral("rejected"), errors.join(QLatin1Char('\n')));
        result.insert(QStringLiteral("errors"), errors);
        QTimer::singleShot(0, this, [this, requestId, result] { emit accountTestFinished(requestId, result); });
        return;
    }
    // A throw-away store and runtime: a test never saves anything, never
    // deletes server copies (autoSave off) and never stores the password.
    auto *scratch = new QindaTK::LocalProductivityStore(this);
    auto *rt = new AccountRuntime(candidate, {scratch, m_attachments, m_secrets, m_calls}, this);
    if (!password.isEmpty()) rt->setPasswordOverride(password);
    struct Test {
        int pending = 1;
        QVariantMap result;
        QVariantList steps;
    };
    auto test = std::make_shared<Test>();
    test->result = ProviderCalls::result(requestId, QStringLiteral("accepted"));
    const auto step = [this, test, rt, scratch, requestId](const QString &service, const QVariantMap &outcome) {
        if (!service.isEmpty()) {
            test->steps.append(QVariantMap{{QStringLiteral("service"), service},
                                           {QStringLiteral("state"), outcome.value(QStringLiteral("state"))},
                                           {QStringLiteral("error"), outcome.value(QStringLiteral("error"))}});
            const QString state = outcome.value(QStringLiteral("state")).toString();
            if (severity(state) > severity(test->result.value(QStringLiteral("state")).toString())) {
                test->result.insert(QStringLiteral("state"), state);
                test->result.insert(QStringLiteral("ok"), false);
                test->result.insert(QStringLiteral("error"), outcome.value(QStringLiteral("error")));
                test->result.insert(QStringLiteral("needsSignIn"), outcome.value(QStringLiteral("needsSignIn")));
            }
            if (outcome.contains(QStringLiteral("mailboxes")))
                test->result.insert(QStringLiteral("mailboxes"), outcome.value(QStringLiteral("mailboxes")));
            if (outcome.contains(QStringLiteral("calendars")))
                test->result.insert(QStringLiteral("calendars"), outcome.value(QStringLiteral("calendars")));
        }
        if (--test->pending > 0) return;
        test->result.insert(QStringLiteral("steps"), test->steps);
        rt->deleteLater();
        scratch->deleteLater();
        emit accountTestFinished(requestId, test->result);
    };
    if (rt->isImap()) {
        ++test->pending;
        QindaTK::ImapMailProvider *imap = rt->imap(QStringLiteral("INBOX"));
        rt->run(QStringLiteral("imap"), imap, [imap](const QString &rid) { return imap->listMailboxes(rid, false); },
                [step](const QVariantMap &r) { step(QStringLiteral("imap"), r); });
    } else if (rt->isPop3()) {
        ++test->pending;
        QindaTK::Pop3MailProvider *pop3 = rt->pop3();
        pop3->setProperty("autoSave", false);
        pop3->setProperty("fetchLimit", 1);
        rt->run(QStringLiteral("pop3"), pop3, [pop3](const QString &rid) { return pop3->sync(rid); },
                [step](const QVariantMap &r) { step(QStringLiteral("pop3"), r); });
    }
    for (const CalendarBinding &binding : rt->calendars()) {
        QObject *provider = binding.provider;
        if (provider == nullptr) continue;
        provider->setProperty("autoSave", false);
        const QString service = binding.service;
        const char *method = service == QLatin1String("caldav") ? "discoverCalendars"
            : service == QLatin1String("ics")                    ? "sync"
                                                                 : "listCalendars";
        if (service == QLatin1String("ics") && provider->property("feedUrl").toString().isEmpty()) continue;
        ++test->pending;
        rt->run(service, provider,
                [provider, method, service](const QString &rid) {
                    bool started = false;
                    if (service == QLatin1String("caldav")) {
                        QMetaObject::invokeMethod(provider, method, Q_RETURN_ARG(bool, started), Q_ARG(QString, rid),
                                                  Q_ARG(QString, QString()));
                    } else {
                        QMetaObject::invokeMethod(provider, method, Q_RETURN_ARG(bool, started), Q_ARG(QString, rid));
                    }
                    return started;
                },
                [step, service](const QVariantMap &r) { step(service, r); });
        break; // one calendar answers for the account's credentials
    }
    step(QString(), {}); // release the guard
}

void MailService::discoverCalendars(const QString &requestId, const QString &url, const QString &userName,
                                    const QString &password)
{
    auto *provider = new QindaTK::CalDavProvider(this);
    provider->setProperty("userName", userName);
    if (!password.isEmpty()) provider->setSecret(password, QStringLiteral("basic"));
    m_calls->watch(provider);
    const QString rid = ProviderCalls::newRequestId(QStringLiteral("discover"));
    m_calls->expect(rid, [this, provider, requestId](const QVariantMap &result) {
        QVariantMap answer = result;
        answer.insert(QStringLiteral("requestId"), requestId);
        provider->deleteLater();
        emit calendarsDiscovered(requestId, answer);
    });
    static_cast<void>(provider->discoverCalendars(rid, url));
}

} // namespace QindaOffice::Apps::Mail
