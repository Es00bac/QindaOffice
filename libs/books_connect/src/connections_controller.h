// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// ConnectionsController: one company window's bank and payment connections
// (M48, D-048) — connect (validate a token, keep it in the keyring, remember
// the settings), link Mercury accounts to bank accounts, Stripe settings,
// sync, payment links and disconnect. BooksService wraps it for QML; the
// tests drive it directly against fake servers.
//
// AGENT-CONTRACT:
//  * A token reaches the keyring only after the provider accepted it; a
//    refused token is never stored. Tokens never appear in an outcome,
//    the company file, a log or an error (connect_http.h redacts).
//  * The controller never caches a CompanyFile pointer: it asks the
//    FileProvider after every wait (Save As replaces the file).
//  * One sync at a time; a second request fails fast with a message.
//  * Endpoints default to the real APIs; setEndpoints() is for tests
//    (loopback http is allowed, anything else must be https).
//
// Files: connections_controller.cpp (connect, link, settings, disconnect,
// secret state), connections_controller_sync.cpp (sync, payment links).

#include "books_types.h"
#include "connect_secrets.h"
#include "connect_settings.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QUrl>

#include <functional>
#include <memory>

class QNetworkAccessManager;

namespace QindaOffice::Books::Connect {

struct MercuryConnectOutcome {
    bool ok = false;
    QString error;
    QList<MercuryAccount> accounts;
};

struct StripeConnectOutcome {
    bool ok = false;
    QString error;
    QString accountName;
    bool livemode = false;
};

struct PaymentLinkOutcome {
    bool ok = false;
    QString url;
    QString error;
    bool reused = false; // an existing link for the same amount
};

class ConnectionsController : public QObject {
    Q_OBJECT
public:
    enum class SecretState { Unknown, Present, Missing };

    explicit ConnectionsController(FileProvider file, std::shared_ptr<SecretBackend> secrets = defaultSecretBackend(),
                                   QObject *parent = nullptr);
    ~ConnectionsController() override;

    void setEndpoints(const QUrl &mercuryBase, const QUrl &stripeBase);
    void setToday(std::function<QDate()> today) { m_today = std::move(today); }
    [[nodiscard]] std::shared_ptr<SecretBackend> secrets() const { return m_secrets; }

    // ---- connect / disconnect (connections_controller.cpp) ----
    void connectMercury(const QString &token, std::function<void(const MercuryConnectOutcome &)> done);
    void connectStripe(const QString &key, std::function<void(const StripeConnectOutcome &)> done);
    // booksAccountId 0 creates "Mercury Checking ••1234"; < 0 unlinks.
    // Result.id = the linked bank account.
    Result linkMercuryAccount(const QString &mercuryAccountId, qint64 booksAccountId);
    Result saveStripeSettings(const StripeSettings &settings);
    // Removes the token and the connection row; imported history stays.
    void disconnect(const QString &provider, std::function<void(bool ok, const QString &error)> done);

    // What the keyring holds for the provider (Unknown until checked).
    [[nodiscard]] SecretState secretState(const QString &provider) const;
    // Re-reads the keyring for every connected provider (async).
    void refreshSecretStates();
    [[nodiscard]] bool syncing() const { return m_syncing; }

    // ---- sync and payment links (connections_controller_sync.cpp) ----
    void sync(std::function<void(const SyncReport &)> done);
    void createPaymentLink(qint64 invoiceId, std::function<void(const PaymentLinkOutcome &)> done);

signals:
    // Status, secret state or settings changed: views re-query.
    void stateChanged();

private:
    void readSecret(const QString &provider, std::function<void(const SecretOutcome &)> done);
    void setSecretState(const QString &provider, SecretState state);
    void syncProviders(QStringList providers, std::shared_ptr<SyncReport> total,
                       std::function<void(const SyncReport &)> done);
    [[nodiscard]] QDate today() const { return m_today ? m_today() : QDate::currentDate(); }

    FileProvider m_file;
    std::shared_ptr<SecretBackend> m_secrets;
    QNetworkAccessManager *m_network = nullptr;
    QUrl m_mercuryBase;
    QUrl m_stripeBase;
    std::function<QDate()> m_today;
    QHash<QString, SecretState> m_secretStates;
    bool m_syncing = false;
};

// Helpers shared by the controller files and the tests.
// The bank account named "Stripe (<CUR>)", created when missing.
qint64 ensureStripeClearingAccount(CompanyFile &file, QString *error = nullptr);
// The "Merchant Fees" expense account, created when missing.
qint64 ensureMerchantFeesAccount(CompanyFile &file, QString *error = nullptr);

} // namespace QindaOffice::Books::Connect
