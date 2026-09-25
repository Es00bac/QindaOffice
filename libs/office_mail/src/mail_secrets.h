// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Mail account secrets (passwords, app passwords, OAuth refresh tokens) in
// the desktop keyring: service `qindaoffice-mail`, key
// `<accountId>/<service>`. Every operation is asynchronous and ends in one
// `finished(requestId, result)` (mail_result.h contract).
//
// AGENT-GUARD: no QML-visible call returns a secret. QML hosts hand a
// stored secret to a toolkit provider with applySecret(…, provider,
// mechanism), which calls the provider's write-only setSecret() from C++;
// C++ adapters use read(). This keeps passwords out of QML property
// bindings, console.log(JSON.stringify(result)) and the QML debugger.

#include "secret_backend.h"

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace QindaOffice::Mail {

class MailSecrets : public QObject {
    Q_OBJECT
    QML_ELEMENT
    // "keyring" or "memory" (tests, QQO_MAIL_SECRET_BACKEND=memory).
    Q_PROPERTY(QString backendName READ backendName NOTIFY backendChanged)

public:
    explicit MailSecrets(QObject *parent = nullptr);
    MailSecrets(std::shared_ptr<SecretBackend> backend, QObject *parent = nullptr);

    // The services an account can hold a secret for; removeAccount() wipes
    // exactly these (a keyring cannot be enumerated per prefix).
    [[nodiscard]] static QStringList services();
    [[nodiscard]] static QString keyFor(const QString &accountId, const QString &service);
    // Secret service for a refresh token: oauth-refresh-google / -microsoft.
    [[nodiscard]] static QString refreshServiceFor(const QString &oauthProvider);

    void setBackend(std::shared_ptr<SecretBackend> backend);
    [[nodiscard]] std::shared_ptr<SecretBackend> backend() const { return m_backend; }
    [[nodiscard]] QString backendName() const;

    // Result keys (all operations): requestId, ok, state, error, retryable,
    // operation, accountId, service, notFound, unavailable.
    Q_INVOKABLE void write(const QString &requestId, const QString &accountId, const QString &service,
                           const QString &secret);
    Q_INVOKABLE void remove(const QString &requestId, const QString &accountId, const QString &service);
    // Removes the secret of every service in services(); result `removed`
    // counts the keys processed. ok only when every removal succeeded.
    Q_INVOKABLE void removeAccount(const QString &requestId, const QString &accountId);
    // Result `exists` (bool); the secret itself is not returned.
    Q_INVOKABLE void check(const QString &requestId, const QString &accountId, const QString &service);
    // Reads the secret and calls `target->setSecret(secret, mechanism)`
    // (the QindaTK providers' write-only setter). Result `applied` (bool).
    Q_INVOKABLE void applySecret(const QString &requestId, const QString &accountId, const QString &service,
                                 QObject *target, const QString &mechanism);

    // C++ only: the secret value for adapters and OAuth2Session.
    // `done` runs on this object's thread unless this object was destroyed.
    void read(const QString &accountId, const QString &service, std::function<void(const SecretOutcome &)> done);
    void store(const QString &accountId, const QString &service, const QString &secret,
               std::function<void(const SecretOutcome &)> done);
    void erase(const QString &accountId, const QString &service, std::function<void(const SecretOutcome &)> done);

    // Empty when valid; otherwise the reason (for the rejected result).
    [[nodiscard]] static QString validateKey(const QString &accountId, const QString &service);

signals:
    void finished(const QString &requestId, const QVariantMap &result);
    void backendChanged();

private:
    QVariantMap resultFor(const QString &requestId, const char *operation, const QString &accountId,
                          const QString &service, const SecretOutcome &outcome) const;
    void finishLater(const QString &requestId, const QVariantMap &result);

    std::shared_ptr<SecretBackend> m_backend;
};

} // namespace QindaOffice::Mail
