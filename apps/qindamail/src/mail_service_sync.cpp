// SPDX-License-Identifier: GPL-3.0-or-later
// MailService: sync of mail and calendars, IMAP mailbox lists (cached in
// folders.json so the pane is complete offline) and the per-account timers.
#include "mail_service.h"

#include "carddav_provider.h"
#include "contact_store.h"

#include "provider_calls.h"

#include <qindatk/imap_mail_provider.h>
#include <qindatk/local_productivity_store.h>
#include <qindatk/pop3_mail_provider.h>

#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>

#include <memory>

namespace QindaOffice::Apps::Mail {

namespace {

int severity(const QString &state)
{
    if (state == QLatin1String("rejected")) return 3;
    if (state == QLatin1String("conflict")) return 2;
    if (state == QLatin1String("offline")) return 1;
    return 0;
}

// Several requests of one account folded into one result (the worst state
// wins); `done` runs once, after the last.
struct Fold {
    int pending = 1; // released by finish() once every request is started
    QVariantMap worst = ProviderCalls::result({}, QStringLiteral("accepted"));
    std::function<void(const QVariantMap &)> done;

    void add(const QVariantMap &result)
    {
        if (severity(result.value(QStringLiteral("state")).toString())
            > severity(worst.value(QStringLiteral("state")).toString())) {
            worst = result;
        }
        if (--pending == 0 && done) done(worst);
    }
};

} // namespace

void MailService::syncRuntime(AccountRuntime *runtime, const QString &batchId)
{
    auto fold = std::make_shared<Fold>();
    QPointer<AccountRuntime> rt(runtime);
    fold->done = [this, rt, batchId](const QVariantMap &worst) {
        if (rt != nullptr) {
            rt->noteResult(worst);
            if (worst.value(QStringLiteral("state")).toString() == QLatin1String("accepted")) rt->markSynced();
        }
        batchDone(batchId, worst);
    };
    runtime->setStatus(QStringLiteral("syncing"));
    const auto one = [fold](const QVariantMap &result) { fold->add(result); };

    if (runtime->isImap()) {
        ++fold->pending;
        QindaTK::ImapMailProvider *inbox = runtime->imap(QStringLiteral("INBOX"));
        runtime->run(QStringLiteral("imap"), inbox,
                     [inbox](const QString &rid) { return inbox->listMailboxes(rid, false); },
                     [this, rt, fold, one](const QVariantMap &listed) {
                         if (rt == nullptr) {
                             fold->add(listed);
                             return;
                         }
                         if (listed.value(QStringLiteral("ok")).toBool()) {
                             rt->setMailboxes(listed.value(QStringLiteral("mailboxes")).toList());
                             m_folderCache.insert(rt->id(), rt->mailboxes());
                             saveFolderCache();
                             scheduleRefresh();
                         }
                         if (listed.value(QStringLiteral("state")).toString() != QLatin1String("accepted")) {
                             fold->add(listed);
                             return;
                         }
                         // The inbox, and the folder on screen when it is this account's.
                         QStringList mailboxes{QStringLiteral("INBOX")};
                         const QString prefix = QStringLiteral("acct:") + rt->id() + QLatin1Char(':');
                         if (m_currentFolderKey.startsWith(prefix)) {
                             const QString current = m_currentFolderKey.mid(prefix.size());
                             if (!mailboxes.contains(current)) mailboxes.append(current);
                         }
                         for (const QString &mailbox : std::as_const(mailboxes)) {
                             QindaTK::ImapMailProvider *provider = rt->imap(mailbox);
                             ++fold->pending;
                             rt->run(QStringLiteral("imap"), provider,
                                     [provider](const QString &rid) { return provider->sync(rid); }, one);
                         }
                         fold->add(listed);
                     });
    } else if (runtime->isPop3()) {
        ++fold->pending;
        QindaTK::Pop3MailProvider *pop3 = runtime->pop3();
        runtime->run(QStringLiteral("pop3"), pop3, [pop3](const QString &rid) { return pop3->sync(rid); }, one);
    }
    for (const CalendarBinding &binding : runtime->calendars()) {
        QObject *provider = binding.provider;
        if (provider == nullptr) continue;
        if (binding.service == QLatin1String("ics") && provider->property("feedUrl").toString().isEmpty()) continue;
        ++fold->pending;
        runtime->run(binding.service, provider,
                     [provider](const QString &rid) {
                         bool started = false;
                         QMetaObject::invokeMethod(provider, "sync", Q_RETURN_ARG(bool, started), Q_ARG(QString, rid));
                         return started;
                     },
                     one);
    }
    if (QindaOffice::Mail::CardDavProvider *carddav = runtime->carddav()) {
        // M76: the account's address books, two ways; a conflict keeps the
        // local version as a copy and says so.
        ++fold->pending;
        runtime->run(QStringLiteral("carddav"), carddav, [carddav](const QString &rid) { return carddav->sync(rid); },
                     [this, fold](const QVariantMap &result) {
                         const QStringList names = result.value(QStringLiteral("conflictNames")).toStringList();
                         if (!names.isEmpty()) {
                             emit notify(tr("Contacts changed in two places"),
                                         tr("The server's version of %1 was kept; your changes are saved as copies in the "
                                            "category “%2”.").arg(names.join(QStringLiteral(", ")),
                                                                  QLatin1String(QindaOffice::Mail::ContactStore::kConflictCategory)),
                                         QStringLiteral("warning"));
                         }
                         fold->add(result);
                     });
    }
    fold->add(ProviderCalls::result({}, QStringLiteral("accepted"))); // release the guard
}

QString MailService::syncAll()
{
    QList<AccountRuntime *> targets;
    for (AccountRuntime *rt : std::as_const(m_runtimes)) {
        if (rt->pending() == 0) targets.append(rt);
    }
    const QString batch = beginBatch(QStringLiteral("sync"), int(targets.size()));
    for (AccountRuntime *rt : std::as_const(targets)) syncRuntime(rt, batch);
    flushOutbox();
    return batch;
}

QString MailService::syncAccount(const QString &accountId)
{
    AccountRuntime *rt = runtime(accountId);
    const QString batch = beginBatch(QStringLiteral("sync"), rt != nullptr ? 1 : 0);
    if (rt != nullptr) syncRuntime(rt, batch);
    return batch;
}

QString MailService::syncFolder(const QString &folderKey)
{
    QList<QPair<AccountRuntime *, QString>> targets;
    if (folderKey == QLatin1String("all:inbox")) {
        for (AccountRuntime *rt : std::as_const(m_runtimes)) {
            if (rt->isImap() || rt->isPop3()) targets.append({rt, QStringLiteral("INBOX")});
        }
    } else {
        const QString account = folderKey.section(QLatin1Char(':'), 1, 1);
        const QString prefix = QStringLiteral("acct:") + account + QLatin1Char(':');
        AccountRuntime *rt = runtime(account);
        if (rt != nullptr && folderKey.startsWith(prefix) && (rt->isImap() || rt->isPop3()))
            targets.append({rt, folderKey.mid(prefix.size())});
    }
    const QString batch = beginBatch(QStringLiteral("sync"), int(targets.size()));
    for (const auto &[rt, mailbox] : std::as_const(targets)) {
        const auto done = [this, batch](const QVariantMap &result) { batchDone(batch, result); };
        if (rt->isPop3()) {
            QindaTK::Pop3MailProvider *pop3 = rt->pop3();
            rt->run(QStringLiteral("pop3"), pop3, [pop3](const QString &rid) { return pop3->sync(rid); }, done);
        } else {
            QindaTK::ImapMailProvider *provider = rt->imap(mailbox);
            rt->run(QStringLiteral("imap"), provider, [provider](const QString &rid) { return provider->sync(rid); },
                    done);
        }
    }
    return batch;
}

void MailService::restartTimer(AccountRuntime *runtime)
{
    delete m_timers.take(runtime->id());
    const int minutes = runtime->account().syncIntervalMinutes;
    if (!m_autoSync || minutes <= 0) return;
    auto *timer = new QTimer(this);
    timer->setInterval(minutes * 60 * 1000);
    const QString id = runtime->id();
    connect(timer, &QTimer::timeout, this, [this, id] {
        AccountRuntime *rt = this->runtime(id);
        if (rt != nullptr && rt->pending() == 0 && m_online) static_cast<void>(syncAccount(id));
    });
    timer->start();
    m_timers.insert(id, timer);
}

void MailService::loadFolderCache()
{
    QFile file(m_dataDir + QStringLiteral("/folders.json"));
    if (!file.open(QIODevice::ReadOnly)) return;
    const QJsonObject root = QJsonDocument::fromJson(file.read(4 * 1024 * 1024)).object();
    if (root.value(QStringLiteral("schema")).toString() != QLatin1String("qindaoffice.mail.folders/1")) return;
    const QJsonObject accounts = root.value(QStringLiteral("accounts")).toObject();
    for (auto it = accounts.begin(); it != accounts.end(); ++it) {
        m_folderCache.insert(it.key(), it.value().toArray().toVariantList());
    }
}

void MailService::saveFolderCache()
{
    QJsonObject accounts;
    for (auto it = m_folderCache.cbegin(); it != m_folderCache.cend(); ++it) {
        if (runtime(it.key()) != nullptr) accounts.insert(it.key(), QJsonArray::fromVariantList(it.value()));
    }
    const QJsonObject root{{QStringLiteral("schema"), QStringLiteral("qindaoffice.mail.folders/1")},
                           {QStringLiteral("accounts"), accounts}};
    QSaveFile file(m_dataDir + QStringLiteral("/folders.json"));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        static_cast<void>(file.commit());
    }
}

void MailService::flushOutbox()
{
    if (!m_online) return;
    for (const QVariant &value : m_store->drafts()) {
        const QVariantMap draft = value.toMap();
        if (draft.value(QStringLiteral("queued")).toBool()
            && !m_sending.contains(draft.value(QStringLiteral("id")).toString())) {
            deliver(ProviderCalls::newRequestId(QStringLiteral("outbox")), draft);
        }
    }
}

} // namespace QindaOffice::Apps::Mail
