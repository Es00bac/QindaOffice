// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// CardDAV address books of one account (M76, D-085) with the surface of
// QindaTK's providers (CalDavProvider): configuration properties, a
// write-only setSecret(), and requests answered once by a …Finished signal,
// so QindaMail's AccountRuntime runs it like any provider (keyring secret or
// OAuth token applied first, one retry after an authentication refusal).
//
// discover(requestId, url) → discoveryFinished(requestId, {ok, state, error,
//   retryable, authRejected, principalUrl, homeSetUrl, addressBooks [{url,
//   name}], transcript}).
// sync(requestId) → syncFinished(requestId, {ok, state, error, retryable,
//   authRejected, created, updated, deleted, pushed, conflicts (count),
//   conflictNames, books, transcript}): every enabled book of
//   `addressBooks` two ways with `store` (carddav_jobs.h for the protocol,
//   ContactStore::applySync for the local side).
//
// AGENT-GUARD: the secret is kept in memory only, never in a property, a
// result or a log line (results and transcripts are redacted, the
// Authorization header is never echoed). Snapshots and the store are touched
// on the GUI thread only; the network runs on a worker
// (QindaTK::Protocol::startWorker) that sees copies.

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaOffice::Mail {

class ContactStore;

namespace CardDav {
struct Credentials;
struct SyncResult;
}

Q_MOC_INCLUDE("contact_store.h")

class CardDavProvider : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString accountId MEMBER m_accountId NOTIFY configChanged)
    // Where discovery starts; its host is the site the credentials belong to.
    Q_PROPERTY(QString url MEMBER m_url NOTIFY configChanged)
    Q_PROPERTY(QString userName MEMBER m_userName NOTIFY configChanged)
    // [{id, url, name, enabled}] — the account's contacts.addressBooks.
    Q_PROPERTY(QVariantList addressBooks MEMBER m_books NOTIFY configChanged)
    Q_PROPERTY(QindaOffice::Mail::ContactStore *store READ store WRITE setStore NOTIFY configChanged)
    Q_PROPERTY(int timeoutMs MEMBER m_timeoutMs NOTIFY configChanged)
    Q_PROPERTY(bool allowInsecureAuth MEMBER m_allowInsecureAuth NOTIFY configChanged)
    Q_PROPERTY(bool hasSecret READ hasSecret NOTIFY secretChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)

public:
    explicit CardDavProvider(QObject *parent = nullptr);
    ~CardDavProvider() override;

    [[nodiscard]] ContactStore *store() const { return m_store; }
    void setStore(ContactStore *store);
    [[nodiscard]] bool hasSecret() const { return !m_secret.isEmpty(); }
    [[nodiscard]] bool busy() const { return m_pending > 0; }
    [[nodiscard]] QString lastError() const { return m_lastError; }

    // `mechanism` basic (with userName) or bearer (an OAuth access token).
    Q_INVOKABLE void setSecret(const QString &secret, const QString &mechanism = QStringLiteral("basic"));
    Q_INVOKABLE void clearSecret();
    Q_INVOKABLE bool discover(const QString &requestId, const QString &url = {});
    Q_INVOKABLE bool sync(const QString &requestId);

signals:
    void configChanged();
    void secretChanged();
    void stateChanged();
    void discoveryFinished(const QString &requestId, const QVariantMap &result);
    void syncFinished(const QString &requestId, const QVariantMap &result);

private:
    [[nodiscard]] CardDav::Credentials credentials(const QString &site) const;
    void rejectLater(bool discovery, const QString &requestId, const QString &error, bool authRejected = false);
    void finishSync(const QString &requestId, const CardDav::SyncResult &result, const QStringList &books,
                    const QList<QHash<QString, QString>> &digests);
    void setBusy(int delta, const QString &error);

    QString m_accountId, m_url, m_userName;
    QVariantList m_books;
    QPointer<ContactStore> m_store;
    QString m_secret, m_mechanism = QStringLiteral("basic");
    QString m_lastError;
    int m_timeoutMs = 15000;
    int m_pending = 0;
    bool m_allowInsecureAuth = false;
};

} // namespace QindaOffice::Mail
