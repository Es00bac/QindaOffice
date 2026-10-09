// SPDX-License-Identifier: GPL-3.0-or-later
// MailService: lifecycle, the accounts list, status and request batches.
#include "mail_service.h"

#include "account_store.h"
#include "autoconfig.h"
#include "contact_store.h"
#include "folder_model.h"
#include "json_settings.h"
#include "mail_secrets.h"
#include "message_list_model.h"
#include "paths.h"
#include "provider_calls.h"

#include <qindatk/ics_calendar_provider.h>
#include <qindatk/local_attachment_store.h>
#include <qindatk/local_calendar_projection_model.h>
#include <qindatk/local_productivity_store.h>
#include <qindatk/recurrence_service.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QNetworkInformation>
#include <QStandardPaths>
#include <QTimer>

namespace QindaOffice::Apps::Mail {

using QindaOffice::Mail::MailAccount;

namespace {

int severity(const QString &state)
{
    if (state == QLatin1String("rejected")) return 3;
    if (state == QLatin1String("conflict")) return 2;
    if (state == QLatin1String("offline")) return 1;
    return 0;
}

} // namespace

MailService::MailService(QObject *parent)
    : MailService(Options{}, parent)
{
}

MailService::MailService(const Options &options, QObject *parent)
    : QObject(parent)
{
    initialize(options);
}

MailService::~MailService()
{
    // Runtimes (and their providers) go before the store they point at.
    qDeleteAll(m_runtimes);
    m_runtimes.clear();
}

MailService &MailService::shared()
{
    static QPointer<MailService> instance;
    if (instance == nullptr) {
        instance = new MailService(QCoreApplication::instance());
    }
    return *instance;
}

void MailService::initialize(const Options &options)
{
    m_calls = new ProviderCalls(this);
    m_autoSync = options.autoSync;
    m_dataDir = options.dataDir.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/qindaoffice/mail")
        : options.dataDir;
    QDir().mkpath(m_dataDir);

    m_store = new QindaTK::LocalProductivityStore(this);
    m_store->setFilePath(m_dataDir + QStringLiteral("/store.json"));
    if (QFileInfo::exists(m_store->filePath())) {
        static_cast<void>(m_store->load());
    }
    m_attachments = new QindaTK::LocalAttachmentStore(this);
    m_attachments->setRootPath(m_dataDir + QStringLiteral("/attachments"));
    m_recurrence = new QindaTK::RecurrenceService(this);
    m_projection = new QindaTK::LocalCalendarProjectionModel(this);
    m_projection->setStore(m_store);
    m_projection->setRecurrenceService(m_recurrence);
    connect(m_projection, &QindaTK::LocalCalendarProjectionModel::occurrencesChanged, this,
            &MailService::rebuildEvents);
    connect(m_projection, &QindaTK::LocalCalendarProjectionModel::errorChanged, this, &MailService::rebuildEvents);
    m_ics = new QindaTK::IcsCalendarProvider(this);
    m_ics->setStore(m_store);
    m_ics->setProperty("accountId", QString::fromLatin1(kLocalAccountId));
    m_ics->setProperty("calendarId", QString::fromLatin1(kLocalCalendarId));
    m_calls->watch(m_ics);

    m_secrets = options.secretBackend ? new QindaOffice::Mail::MailSecrets(options.secretBackend, this)
                                      : new QindaOffice::Mail::MailSecrets(this);
    m_calls->watch(m_secrets);
    m_accountStore = options.accountsPath.isEmpty() ? new QindaOffice::Mail::AccountStore(this)
                                                    : new QindaOffice::Mail::AccountStore(options.accountsPath, this);
    m_accountStore->setSecrets(m_secrets);
    m_autoConfig = new QindaOffice::Mail::AutoConfig(this);

    const QString settingsPath = options.settingsPath.isEmpty()
        ? Core::configDirFor(QStringLiteral("mail")) + QStringLiteral("/view.json")
        : options.settingsPath;
    QDir().mkpath(QFileInfo(settingsPath).absolutePath());
    m_settings = std::make_unique<Core::JsonSettings>(settingsPath);
    static_cast<void>(m_settings->load());
    for (const QString &key : m_settings->value(QStringLiteral("collapsedFolders")).toStringList()) {
        m_collapsed.insert(key);
    }
    for (const QString &id : m_settings->value(QStringLiteral("hiddenCalendars")).toStringList()) {
        m_hiddenCalendars.insert(id);
    }

    // M64: the address book next to the store, and the senders whose
    // pictures always load.
    m_contacts = new QindaOffice::Mail::ContactStore(this);
    m_contacts->setPath(options.contactsPath.isEmpty() ? m_dataDir + QStringLiteral("/contacts.json")
                                                       : options.contactsPath);
    for (const QString &address : m_settings->value(QStringLiteral("picturesSenders")).toStringList()) {
        m_picturesSenders.insert(address.toLower());
    }

    m_folderModel = new FolderModel(this);
    m_messageModel = new MessageListModel(this);
    m_messageModel->setThreaded(m_settings->value(QStringLiteral("threaded"), true).toBool());
    connect(m_messageModel, &MessageListModel::searchTextChanged, this, &MailService::searchTextChanged);
    connect(m_messageModel, &MessageListModel::filterChanged, this, &MailService::filterChanged);
    connect(m_messageModel, &MessageListModel::threadedChanged, this, &MailService::threadedChanged);

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(0);
    connect(m_refreshTimer, &QTimer::timeout, this, &MailService::refreshDerived);
    connect(m_store, &QindaTK::LocalProductivityStore::dataChanged, this, &MailService::scheduleRefresh);
    m_markReadTimer = new QTimer(this);
    m_markReadTimer->setSingleShot(true);
    connect(m_markReadTimer, &QTimer::timeout, this, [this] {
        if (m_selectedMessage.value(QStringLiteral("unread")).toBool()
            && !m_selectedMessage.value(QStringLiteral("isDraft")).toBool()) {
            static_cast<void>(markRead({m_selectedMessageId}, true));
        }
    });

    // Accounts added, changed or removed through the store (the service's
    // own calls, or QML using accountStore directly) rebuild their runtime.
    connect(m_accountStore, &QindaOffice::Mail::AccountStore::accountAdded, this, [this](const QString &id) {
        if (const auto account = m_accountStore->find(id); account && account->enabled && runtime(id) == nullptr) {
            startRuntime(*account);
        }
        emit accountsChanged();
        emit calendarsChanged();
        scheduleRefresh();
    });
    connect(m_accountStore, &QindaOffice::Mail::AccountStore::accountUpdated, this, [this](const QString &id) {
        stopRuntime(id);
        if (const auto account = m_accountStore->find(id); account && account->enabled) {
            startRuntime(*account);
        }
        emit accountsChanged();
        emit calendarsChanged();
        scheduleRefresh();
    });
    connect(m_accountStore, &QindaOffice::Mail::AccountStore::accountRemoved, this, [this](const QString &id) {
        stopRuntime(id);
        emit accountsChanged();
        emit calendarsChanged();
        scheduleRefresh();
    });
    connect(m_accountStore, &QindaOffice::Mail::AccountStore::secretsRemoved, this,
            [this](const QString &, const QVariantMap &result) {
        if (result.value(QStringLiteral("state")).toString() == QLatin1String("accepted")) return;
        // Account removal is already durable. Keyring cleanup is a separate
        // asynchronous result, including preserved imports (D-067).
        const QString error = result.value(QStringLiteral("error")).toString();
        emit notify(tr("Account removed; credential cleanup incomplete"),
                    error.isEmpty() ? tr("The keyring could not complete credential cleanup.") : error,
                    QStringLiteral("warning"));
    });

    loadFolderCache();
    for (const MailAccount &account : m_accountStore->list()) {
        if (account.enabled) {
            startRuntime(account);
        }
    }

    if (options.watchNetwork && QNetworkInformation::loadBackendByFeatures(QNetworkInformation::Feature::Reachability)) {
        QNetworkInformation *network = QNetworkInformation::instance();
        const auto apply = [this](QNetworkInformation::Reachability reachability) {
            setOnline(reachability != QNetworkInformation::Reachability::Disconnected);
        };
        apply(network->reachability());
        connect(network, &QNetworkInformation::reachabilityChanged, this, apply);
    }

    refreshDerived();
    QTimer::singleShot(0, this, &MailService::pruneSources);
    if (m_store->status() == QLatin1String("recovered")) {
        QTimer::singleShot(0, this, [this] {
            emit notify(tr("Mail data recovered"), m_store->lastError(), QStringLiteral("warning"));
        });
    }
    QTimer::singleShot(0, this, [this] {
        if (m_autoSync) {
            static_cast<void>(syncAll());
        }
    });
}

void MailService::startRuntime(const MailAccount &account)
{
    auto *runtime = new AccountRuntime(account, {m_store, m_attachments, m_secrets, m_calls}, this);
    runtime->setMailboxes(m_folderCache.value(account.id));
    connect(runtime, &AccountRuntime::statusChanged, this, [this] {
        emit accountsChanged();
        refreshBusy();
    });
    connect(runtime, &AccountRuntime::pendingChanged, this, &MailService::refreshBusy);
    m_runtimes.append(runtime);
    if (!runtime->oauthConfigureError().isEmpty()) {
        runtime->setStatus(QStringLiteral("needsSignIn"), runtime->oauthConfigureError());
    }
    restartTimer(runtime);
}

void MailService::stopRuntime(const QString &accountId)
{
    AccountRuntime *existing = runtime(accountId);
    if (existing == nullptr) {
        return;
    }
    m_runtimes.removeOne(existing);
    delete m_timers.take(accountId);
    existing->deleteLater();
    // M64: a source download of this account never answers once its
    // runtime is gone; its waiters get their answer now (IMAP record ids
    // carry the account id, "imap:<account>:…").
    const QString prefix = QStringLiteral("imap:") + accountId + QLatin1Char(':');
    for (const QString &messageId : m_sourceWaiters.keys()) {
        if (messageId.startsWith(prefix)) finishSource(messageId, {}, tr("The account was changed or removed"));
    }
    refreshBusy();
}

AccountRuntime *MailService::runtime(const QString &accountId) const
{
    for (AccountRuntime *runtime : m_runtimes) {
        if (runtime->id() == accountId) {
            return runtime;
        }
    }
    return nullptr;
}

QVariantList MailService::accounts() const
{
    QVariantList list;
    for (const MailAccount &account : m_accountStore->list()) {
        const AccountRuntime *rt = runtime(account.id);
        list.append(QVariantMap{
            {QStringLiteral("id"), account.id},
            {QStringLiteral("kind"), account.kind},
            {QStringLiteral("displayName"), account.displayName},
            {QStringLiteral("emailAddress"), account.emailAddress},
            {QStringLiteral("color"), account.extra.value(QStringLiteral("color")).toString()},
            {QStringLiteral("enabled"), account.enabled},
            {QStringLiteral("status"), rt != nullptr ? rt->status() : QStringLiteral("idle")},
            {QStringLiteral("lastError"), rt != nullptr ? rt->lastError() : QString()},
            {QStringLiteral("lastSync"), rt != nullptr && rt->lastSync().isValid()
                                             ? rt->lastSync().toString(Qt::ISODate) : QString()},
            {QStringLiteral("hasMail"), account.hasMail()},
            {QStringLiteral("hasCalendar"), account.hasCalendar()},
            {QStringLiteral("canSend"), rt != nullptr && rt->canSend()},
            {QStringLiteral("usesOAuth"), !account.oauth.provider.isEmpty()},
        });
    }
    list.append(QVariantMap{
        {QStringLiteral("id"), QString::fromLatin1(kLocalAccountId)},
        {QStringLiteral("kind"), QStringLiteral("local")},
        {QStringLiteral("displayName"), tr("On this computer")},
        {QStringLiteral("emailAddress"), QString()},
        {QStringLiteral("color"), QString()},
        {QStringLiteral("enabled"), true},
        {QStringLiteral("status"), QStringLiteral("idle")},
        {QStringLiteral("lastError"), QString()},
        {QStringLiteral("lastSync"), QString()},
        {QStringLiteral("hasMail"), true},
        {QStringLiteral("hasCalendar"), true},
        {QStringLiteral("canSend"), false},
        {QStringLiteral("usesOAuth"), false},
    });
    return list;
}

QVariantMap MailService::accountById(const QString &accountId) const
{
    for (const QVariant &value : accounts()) {
        if (value.toMap().value(QStringLiteral("id")).toString() == accountId) {
            return value.toMap();
        }
    }
    return {};
}

QString MailService::accountName(const QString &accountId) const
{
    return accountById(accountId).value(QStringLiteral("displayName")).toString();
}

QString MailService::defaultAccountId() const
{
    for (const MailAccount &account : m_accountStore->list()) {
        const AccountRuntime *rt = runtime(account.id);
        if (rt != nullptr && rt->canSend()) {
            return account.id;
        }
    }
    return QString::fromLatin1(kLocalAccountId);
}

QStringList MailService::ownAddresses() const
{
    QStringList list;
    for (const MailAccount &account : m_accountStore->list()) {
        if (!account.emailAddress.isEmpty() && !list.contains(account.emailAddress, Qt::CaseInsensitive)) {
            list.append(account.emailAddress);
        }
    }
    return list;
}

QVariantList MailService::sendableAccounts() const
{
    QVariantList list;
    for (const MailAccount &account : m_accountStore->list()) {
        const AccountRuntime *rt = runtime(account.id);
        if (rt != nullptr && rt->canSend()) {
            list.append(QVariantMap{{QStringLiteral("id"), account.id},
                                    {QStringLiteral("displayName"), account.displayName},
                                    {QStringLiteral("emailAddress"), account.emailAddress}});
        }
    }
    return list;
}

void MailService::setAutoSync(bool enabled)
{
    if (enabled == m_autoSync) {
        return;
    }
    m_autoSync = enabled;
    for (AccountRuntime *rt : std::as_const(m_runtimes)) {
        restartTimer(rt);
    }
    emit autoSyncChanged();
}

void MailService::refreshBusy()
{
    int pending = int(m_sending.size());
    QStringList signIn;
    bool offline = false;
    QDateTime last;
    for (const AccountRuntime *rt : std::as_const(m_runtimes)) {
        pending += rt->pending();
        if (rt->status() == QLatin1String("needsSignIn")) {
            signIn.append(rt->account().displayName);
        }
        offline = offline || rt->status() == QLatin1String("offline");
        if (rt->lastSync().isValid() && (!last.isValid() || rt->lastSync() > last)) {
            last = rt->lastSync();
        }
    }
    const bool busy = pending > 0;
    if (busy != m_busy) {
        m_busy = busy;
        emit busyChanged();
    }
    if (busy) {
        setStatusText(m_sending.isEmpty() ? tr("Updating…") : tr("Sending…"));
    } else if (!m_online) {
        setStatusText(tr("Offline — showing saved mail"));
    } else if (!signIn.isEmpty()) {
        setStatusText(tr("Sign in again: %1").arg(signIn.join(QStringLiteral(", "))));
    } else if (offline) {
        setStatusText(tr("Some accounts could not be reached"));
    } else if (last.isValid()) {
        setStatusText(tr("Updated %1").arg(QLocale().toString(last.toLocalTime().time(), QLocale::ShortFormat)));
    } else {
        setStatusText(QString());
    }
}

void MailService::setStatusText(const QString &text)
{
    if (text == m_statusText) {
        return;
    }
    m_statusText = text;
    emit statusTextChanged();
}

void MailService::setOnline(bool online)
{
    if (online == m_online) {
        return;
    }
    m_online = online;
    emit onlineChanged();
    refreshBusy();
    if (online && m_autoSync) {
        static_cast<void>(syncAll());
    }
}

bool MailService::saveStore(QString *error)
{
    if (m_store->save()) {
        return true;
    }
    if (error != nullptr) {
        *error = m_store->lastError();
    }
    // AGENT-NOTE: another writer (a --new-instance process) saved first;
    // take its snapshot so the next edit starts from the file (the store
    // refuses to overwrite it, docs/productivity-store.md).
    if (m_store->status() == QLatin1String("conflict")) {
        static_cast<void>(m_store->load());
    }
    return false;
}

QVariant MailService::setting(const QString &key, const QVariant &fallback) const
{
    return m_settings->value(key, fallback);
}

void MailService::saveSetting(const QString &key, const QVariant &value)
{
    m_settings->setValue(key, value);
    static_cast<void>(m_settings->save());
}

QString MailService::beginBatch(const QString &prefix, int count)
{
    const QString id = ProviderCalls::newRequestId(prefix);
    if (count <= 0) {
        finishNow(id, QStringLiteral("accepted"), QString());
        return id;
    }
    Batch batch;
    batch.pending = count;
    m_batches.insert(id, batch);
    return id;
}

void MailService::batchDone(const QString &requestId, const QVariantMap &result)
{
    const auto it = m_batches.find(requestId);
    if (it == m_batches.end()) {
        return;
    }
    const QString state = result.value(QStringLiteral("state")).toString();
    if (severity(state) > severity(it->state)) {
        it->state = state;
        it->message = result.value(QStringLiteral("error")).toString();
    }
    if (--it->pending > 0) {
        return;
    }
    const Batch done = *it;
    m_batches.erase(it);
    // Always on a later turn: a batch that finishes inside the call that
    // started it must not answer before the caller holds its request id.
    finishNow(requestId, done.state, done.message);
}

void MailService::finishNow(const QString &requestId, const QString &state, const QString &message)
{
    QTimer::singleShot(0, this, [this, requestId, state, message] {
        emit operationFinished(requestId, state, message);
    });
}

void MailService::scheduleRefresh()
{
    m_refreshTimer->start();
}

void MailService::refreshDerived()
{
    m_seenDirty = true;
    rebuildFolders();
    rebuildMessages();
    refreshSelectedMessage();
    rebuildTasks();
}

} // namespace QindaOffice::Apps::Mail
