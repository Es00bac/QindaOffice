// SPDX-License-Identifier: GPL-3.0-or-later
#include "account_store.h"

#include "mail_secrets.h"
#include "provider_presets.h"

#include <atomic_file_store.h>
#include <paths.h>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QUrl>

namespace QindaOffice::Mail {
namespace {

int defaultPort(const QString &protocol, const QString &security)
{
    const bool tls = security == QLatin1String("tls");
    if (protocol == QLatin1String("imap")) return tls ? 993 : 143;
    if (protocol == QLatin1String("pop3")) return tls ? 995 : 110;
    if (protocol == QLatin1String("smtp")) return tls ? 465 : 587;
    return 0;
}

} // namespace

AccountStore::AccountStore(QObject *parent)
    : QObject(parent)
{
}

AccountStore::AccountStore(const QString &path, QObject *parent)
    : QObject(parent)
    , m_path(path)
{
}

QString AccountStore::defaultPath()
{
    return Core::configDirFor(QStringLiteral("mail")) + QStringLiteral("/accounts.json");
}

QString AccountStore::path() const
{
    return m_path.isEmpty() ? defaultPath() : m_path;
}

void AccountStore::setPath(const QString &path)
{
    if (path == m_path) return;
    m_path = path;
    emit pathChanged();
    reload();
}

void AccountStore::ensureLoaded() const
{
    if (m_loaded) return;
    m_loaded = true;
    // Lazy so that setting `path` from QML before first use never reads
    // (or quarantines) the default file.
    const_cast<AccountStore *>(this)->loadFile();
}

bool AccountStore::reload()
{
    m_loaded = true;
    const bool ok = loadFile();
    emit accountsChanged();
    return ok;
}

bool AccountStore::loadFile()
{
    m_accounts.clear();
    m_rootExtra = QJsonObject();
    setReadOnly(false);
    setError({});
    const QString file = path();
    QFile in(file);
    if (!in.exists()) return true;
    if (!in.open(QIODevice::ReadOnly)) {
        setError(QStringLiteral("Cannot read %1: %2").arg(file, in.errorString()));
        setReadOnly(true); // never overwrite a file we could not read
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(in.readAll(), &parseError);
    in.close();
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        // AGENT-GUARD: same rule as Core::JsonSettings — an unparseable file
        // is moved aside, never overwritten, so a hand edit gone wrong costs
        // the user nothing.
        const QString aside = file + QStringLiteral(".corrupt");
        QFile::remove(aside);
        if (QFile::rename(file, aside)) {
            setError(QStringLiteral("%1 could not be read (%2); it was moved to %3.")
                         .arg(file, parseError.errorString(), aside));
        } else {
            setError(QStringLiteral("%1 could not be read (%2) and could not be moved aside.")
                         .arg(file, parseError.errorString()));
            setReadOnly(true);
        }
        qWarning().noquote() << "mail accounts:" << m_lastError;
        return false;
    }
    QJsonObject root = doc.object();
    const QString schema = root.value(QLatin1String("schema")).toString();
    if (!schema.isEmpty() && schema != QLatin1String(kSchema)) {
        // A newer build's file: show it, but never write it back in an
        // older shape.
        setError(QStringLiteral("%1 was written by a newer QindaOffice (%2); accounts are read-only.")
                     .arg(file, schema));
        setReadOnly(true);
    }
    QSet<QString> seen;
    for (const QJsonValue &value : root.value(QLatin1String("accounts")).toArray()) {
        if (!value.isObject()) continue;
        const MailAccount account = MailAccount::fromJson(value.toObject());
        if (account.id.isEmpty() || seen.contains(account.id)) {
            qWarning().noquote() << "mail accounts: skipping an entry without a unique id in" << file;
            continue;
        }
        seen.insert(account.id);
        m_accounts.append(account);
    }
    root.remove(QStringLiteral("schema"));
    root.remove(QStringLiteral("accounts"));
    m_rootExtra = root;
    return m_lastError.isEmpty();
}

bool AccountStore::save(const QList<MailAccount> &accounts, QString *error)
{
    if (m_readOnly) {
        *error = m_lastError.isEmpty() ? QStringLiteral("The account file is read-only.") : m_lastError;
        return false;
    }
    QJsonObject root = m_rootExtra;
    root.insert(QStringLiteral("schema"), QString::fromLatin1(kSchema));
    QJsonArray list;
    for (const MailAccount &account : accounts) list.append(account.toJson());
    root.insert(QStringLiteral("accounts"), list);
    const QString file = path();
    QDir().mkpath(QFileInfo(file).absolutePath());
    const Core::AtomicFileStore::Result written =
        Core::AtomicFileStore::write(file, QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!written.ok) {
        *error = QStringLiteral("Cannot write %1: %2").arg(file, written.error);
        return false;
    }
    return true;
}

QVariantList AccountStore::accounts() const
{
    ensureLoaded();
    QVariantList out;
    for (const MailAccount &account : m_accounts) out.append(account.toVariantMap());
    return out;
}

int AccountStore::count() const
{
    ensureLoaded();
    return static_cast<int>(m_accounts.size());
}

QList<MailAccount> AccountStore::list() const
{
    ensureLoaded();
    return m_accounts;
}

std::optional<MailAccount> AccountStore::find(const QString &id) const
{
    ensureLoaded();
    for (const MailAccount &account : m_accounts) {
        if (account.id == id) return account;
    }
    return std::nullopt;
}

QVariantMap AccountStore::accountById(const QString &id) const
{
    const std::optional<MailAccount> account = find(id);
    return account ? account->toVariantMap() : QVariantMap();
}

QVariantList AccountStore::presets() const
{
    QVariantList out;
    for (const ProviderPreset &preset : ProviderPresets::all()) out.append(preset.toVariantMap());
    return out;
}

void AccountStore::setSecrets(MailSecrets *secrets)
{
    if (m_secrets == secrets) return;
    m_secrets = secrets;
    emit secretsChanged();
}

MailAccount AccountStore::normalized(MailAccount a)
{
    a.id = a.id.trimmed();
    a.kind = a.kind.trimmed().toLower();
    a.emailAddress = a.emailAddress.trimmed();
    a.displayName = a.displayName.trimmed();
    a.incoming.host = a.incoming.host.trimmed();
    a.outgoing.host = a.outgoing.host.trimmed();
    a.calendar.url = a.calendar.url.trimmed();
    if (a.displayName.isEmpty()) {
        a.displayName = !a.emailAddress.isEmpty() ? a.emailAddress
            : !a.calendar.url.isEmpty()           ? QUrl(a.calendar.url).host()
                                                  : QString();
    }
    if (a.kind == QLatin1String(kKindPop3) && a.incoming.protocol.isEmpty()) a.incoming.protocol = QStringLiteral("pop3");
    if (a.hasMail() && a.incoming.protocol.isEmpty()) a.incoming.protocol = QStringLiteral("imap");
    if (!a.incoming.isEmpty()) {
        if (a.incoming.userName.isEmpty()) a.incoming.userName = a.emailAddress;
        if (a.incoming.port == 0) a.incoming.port = defaultPort(a.incoming.protocol, a.incoming.security);
    }
    if (!a.outgoing.isEmpty()) {
        if (a.outgoing.sameCredentialsAsIncoming) a.outgoing.userName = a.incoming.userName;
        if (a.outgoing.userName.isEmpty()) a.outgoing.userName = a.emailAddress;
        if (a.outgoing.port == 0) a.outgoing.port = defaultPort(QStringLiteral("smtp"), a.outgoing.security);
    }
    if (a.calendar.protocol.isEmpty()) a.calendar.protocol = QStringLiteral("none");
    // M76: CardDAV.
    a.contacts.url = a.contacts.url.trimmed();
    if (a.kind == QLatin1String(kKindCardDav)) a.contacts.protocol = QStringLiteral("carddav");
    if (a.contacts.protocol.isEmpty()) a.contacts.protocol = QStringLiteral("none");
    if (a.hasContacts() && a.contacts.userName.isEmpty())
        a.contacts.userName = !a.incoming.userName.isEmpty() ? a.incoming.userName : a.emailAddress;
    if (a.displayName.isEmpty() && a.hasContacts()) a.displayName = QUrl(a.contacts.url).host();
    if ((a.calendar.protocol == QLatin1String("caldav") || a.calendar.protocol == QLatin1String("ews"))
        && a.calendar.userName.isEmpty()) {
        a.calendar.userName = !a.incoming.userName.isEmpty() ? a.incoming.userName : a.emailAddress;
    }
    return a;
}

QStringList AccountStore::validate(const QVariantMap &account) const
{
    MailAccount a = normalized(MailAccount::fromVariantMap(account));
    if (a.id.isEmpty()) a.id = QStringLiteral("draft"); // add() will generate one
    return a.validate();
}

QString AccountStore::contactsUrlFor(const QString &emailAddress) const
{
    return ProviderPresets::contactsUrlFor(emailAddress);
}

QVariantList AccountStore::addressBooks() const
{
    QVariantList out;
    for (const MailAccount &account : list()) {
        if (!account.enabled || !account.hasContacts()) continue;
        for (const AddressBookEntry &book : account.contacts.addressBooks) {
            if (!book.enabled) continue;
            out.append(QVariantMap{{QStringLiteral("accountId"), account.id}, {QStringLiteral("accountName"), account.displayName},
                                   {QStringLiteral("id"), book.id}, {QStringLiteral("url"), book.url},
                                   {QStringLiteral("name"), book.name}});
        }
    }
    return out;
}

QVariantMap AccountStore::draftFor(const QString &kind, const QString &emailAddress, const QString &fullName) const
{
    MailAccount a;
    if (kind == QLatin1String(kKindCardDav)) {
        // M76: a contacts-only account; mail presets do not apply.
        a.kind = kind;
        a.emailAddress = emailAddress.trimmed();
        a.fullName = fullName;
        a.contacts.protocol = QStringLiteral("carddav");
        a.contacts.url = ProviderPresets::contactsUrlFor(emailAddress);
        return normalized(a).toVariantMap();
    }
    if (const std::optional<ProviderPreset> preset = ProviderPresets::forAddress(emailAddress)) {
        a = ProviderPresets::accountFor(*preset, emailAddress, fullName);
    } else if (kind == QLatin1String(kKindGmail) || kind == QLatin1String(kKindMicrosoft365)) {
        // Google Workspace / Microsoft 365 on the organization's own domain.
        const std::optional<ProviderPreset> preset =
            ProviderPresets::forId(kind == QLatin1String(kKindGmail) ? QStringLiteral("gmail")
                                                                     : QStringLiteral("microsoft365"));
        a = ProviderPresets::accountFor(*preset, emailAddress, fullName);
    } else {
        a.kind = kind.isEmpty() ? QString::fromLatin1(kKindImap) : kind;
        a.emailAddress = emailAddress.trimmed();
        a.fullName = fullName;
        if (a.kind == QLatin1String(kKindPop3)) a.incoming.protocol = QStringLiteral("pop3");
        if (a.kind == QLatin1String(kKindCalDav)) a.calendar.protocol = QStringLiteral("caldav");
        if (a.kind == QLatin1String(kKindIcs)) a.calendar.protocol = QStringLiteral("ics");
        if (a.kind == QLatin1String(kKindExchange)) a.calendar.protocol = QStringLiteral("ews");
    }
    return normalized(a).toVariantMap();
}

QVariantMap AccountStore::failure(const QString &id, const QString &error, const QStringList &errors)
{
    setError(error);
    return {{QStringLiteral("ok"), false}, {QStringLiteral("id"), id},
            {QStringLiteral("error"), error}, {QStringLiteral("errors"), errors}};
}

QVariantMap AccountStore::add(const QVariantMap &map)
{
    ensureLoaded();
    MailAccount account = normalized(MailAccount::fromVariantMap(map));
    if (account.id.isEmpty()) account.id = MailAccount::generateId();
    if (find(account.id)) return failure(account.id, QStringLiteral("An account with this id already exists."));
    const QStringList errors = account.validate();
    if (!errors.isEmpty()) return failure(account.id, errors.constFirst(), errors);
    QList<MailAccount> next = m_accounts;
    next.append(account);
    QString error;
    if (!save(next, &error)) return failure(account.id, error);
    m_accounts = next;
    setError({});
    emit accountsChanged();
    emit accountAdded(account.id);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("id"), account.id},
            {QStringLiteral("error"), QString()}, {QStringLiteral("errors"), QStringList()}};
}

QVariantMap AccountStore::update(const QVariantMap &map)
{
    ensureLoaded();
    const MailAccount account = normalized(MailAccount::fromVariantMap(map));
    qsizetype index = -1;
    for (qsizetype i = 0; i < m_accounts.size(); ++i) {
        if (m_accounts.at(i).id == account.id) index = i;
    }
    if (index < 0) return failure(account.id, QStringLiteral("There is no account with id “%1”.").arg(account.id));
    const QStringList errors = account.validate();
    if (!errors.isEmpty()) return failure(account.id, errors.constFirst(), errors);
    QList<MailAccount> next = m_accounts;
    next[index] = account;
    QString error;
    if (!save(next, &error)) return failure(account.id, error);
    m_accounts = next;
    setError({});
    emit accountsChanged();
    emit accountUpdated(account.id);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("id"), account.id},
            {QStringLiteral("error"), QString()}, {QStringLiteral("errors"), QStringList()}};
}

MailSecrets *AccountStore::secretsForRemoval()
{
    if (m_secrets) return m_secrets;
    if (!m_ownSecrets) m_ownSecrets = new MailSecrets(this);
    return m_ownSecrets;
}

QVariantMap AccountStore::remove(const QString &id)
{
    ensureLoaded();
    QList<MailAccount> next = m_accounts;
    const qsizetype removed = next.removeIf([&id](const MailAccount &a) { return a.id == id; });
    if (removed == 0) return failure(id, QStringLiteral("There is no account with id “%1”.").arg(id));
    QString error;
    if (!save(next, &error)) return failure(id, error);
    m_accounts = next;
    setError({});
    emit accountsChanged();
    emit accountRemoved(id);

    // AGENT-CONTRACT: removing an account removes every keyring secret it
    // had (docs/mail.md#secrets); the account file is written first, so a
    // keyring failure leaves orphaned secrets, never an orphaned account.
    MailSecrets *secrets = secretsForRemoval();
    const QString requestId = QStringLiteral("account-store-remove-%1").arg(++m_removeSerial);
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(secrets, &MailSecrets::finished, this,
                          [this, connection, requestId, id](const QString &rid, const QVariantMap &result) {
                              if (rid != requestId) return;
                              disconnect(*connection);
                              emit secretsRemoved(id, result);
                          });
    secrets->removeAccount(requestId, id);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("id"), id}, {QStringLiteral("error"), QString()},
            {QStringLiteral("secretsRequestId"), requestId}};
}

void AccountStore::setError(const QString &error)
{
    if (error == m_lastError) return;
    m_lastError = error;
    emit lastErrorChanged();
}

void AccountStore::setReadOnly(bool readOnly)
{
    if (readOnly == m_readOnly) return;
    m_readOnly = readOnly;
    emit readOnlyChanged();
}

} // namespace QindaOffice::Mail
