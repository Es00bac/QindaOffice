// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Where MailSecrets keeps passwords and refresh tokens: the desktop keyring
// through QtKeychain with a native imported-Password read adapter (D-067),
// or an in-memory map for tests and headless runs. There is deliberately NO plain-file backend: a
// missing secret service is reported, never papered over (D-043).
//
// AGENT-GUARD: never add a file-backed SecretBackend and never enable
// QtKeychain's insecureFallback. Both would put credentials on disk in
// clear text, which the mail lane's gates (docs/verification.md, M43) and
// the toolkit's D-187 forbid.

#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

namespace QindaOffice::Mail {

struct SecretOutcome {
    enum class Status {
        Ok,
        NotFound,      // read: nothing stored under the key
        Unavailable,   // no secret service (or none reachable) — fail honestly
        Denied,        // the user or the service refused access
        Failed,        // anything else; `error` says what
    };
    Status status = Status::Failed;
    QString value;     // read only; empty otherwise
    QString error;     // human-readable, never contains the secret
};

class SecretBackend {
public:
    using Callback = std::function<void(const SecretOutcome &)>;

    virtual ~SecretBackend() = default;

    // All three complete asynchronously (never inside the call) on the
    // calling thread's event loop. A remove of a missing key is Ok.
    virtual void read(const QString &key, Callback done) = 0;
    virtual void write(const QString &key, const QString &value, Callback done) = 0;
    virtual void remove(const QString &key, Callback done) = 0;

    [[nodiscard]] virtual QString name() const = 0;
};

// QtKeychain (Secret Service / KWallet on Linux). Service name
// `qindaoffice-mail`, account key `<accountId>/<service>`.
class KeychainSecretBackend final : public SecretBackend {
public:
    explicit KeychainSecretBackend(QString service = QStringLiteral("qindaoffice-mail"));

    void read(const QString &key, Callback done) override;
    void write(const QString &key, const QString &value, Callback done) override;
    void remove(const QString &key, Callback done) override;
    [[nodiscard]] QString name() const override { return QStringLiteral("keyring"); }

private:
    QString m_service;
};

// Process memory only; for tests and for headless runs that must not touch
// the user's keyring (QQO_MAIL_SECRET_BACKEND=memory). `setUnavailable`
// simulates a machine without a secret service.
class InMemorySecretBackend final : public SecretBackend {
public:
    void read(const QString &key, Callback done) override;
    void write(const QString &key, const QString &value, Callback done) override;
    void remove(const QString &key, Callback done) override;
    [[nodiscard]] QString name() const override { return QStringLiteral("memory"); }

    void setUnavailable(bool unavailable) { m_unavailable = unavailable; }
    [[nodiscard]] QStringList keys() const { return m_values.keys(); }
    [[nodiscard]] bool contains(const QString &key) const { return m_values.contains(key); }

private:
    QHash<QString, QString> m_values;
    bool m_unavailable = false;
};

// The backend a default-constructed MailSecrets uses: QtKeychain with native
// imported-Password compatibility after a missing read (D-067), or — when
// the environment sets QQO_MAIL_SECRET_BACKEND=memory — one in-memory
// backend shared by the whole process (so every MailSecrets instance of a
// headless UI test sees the same secrets).
[[nodiscard]] std::shared_ptr<SecretBackend> defaultSecretBackend();

} // namespace QindaOffice::Mail
