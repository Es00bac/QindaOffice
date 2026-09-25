// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Where QindaBooks keeps bank and payment API tokens: the desktop keyring
// through QtKeychain, service `qindaoffice-books`, key
// `<companyUuid>/<provider>` (D-048). The same backend pattern as
// office_mail's SecretBackend (D-043), kept separate so books_connect does
// not pull the mail stack (QML, QindaTK) into the bookkeeping libraries.
//
// AGENT-GUARD: there is deliberately NO plain-file backend and QtKeychain's
// insecureFallback stays off — a missing secret service is reported, never
// papered over. A token is never written to the company file, a log, a
// result map or an error string (connections.h, connect_http.h).
//
// QQO_BOOKS_SECRET_BACKEND=memory selects one in-memory backend shared by
// the whole process (tests, headless grabs) so the user's keyring is never
// touched.

#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

namespace QindaOffice::Books::Connect {

struct SecretOutcome {
    enum class Status { Ok, NotFound, Unavailable, Denied, Failed };
    Status status = Status::Failed;
    QString value; // read only
    QString error; // for people; never contains the secret
    [[nodiscard]] bool ok() const { return status == Status::Ok; }
};

class SecretBackend {
public:
    using Callback = std::function<void(const SecretOutcome &)>;
    virtual ~SecretBackend() = default;
    // Complete asynchronously on the calling thread's event loop. Removing
    // a missing key is Ok.
    virtual void read(const QString &key, Callback done) = 0;
    virtual void write(const QString &key, const QString &value, Callback done) = 0;
    virtual void remove(const QString &key, Callback done) = 0;
    [[nodiscard]] virtual QString name() const = 0;
};

class KeychainSecretBackend final : public SecretBackend {
public:
    explicit KeychainSecretBackend(QString service = QStringLiteral("qindaoffice-books"));
    void read(const QString &key, Callback done) override;
    void write(const QString &key, const QString &value, Callback done) override;
    void remove(const QString &key, Callback done) override;
    [[nodiscard]] QString name() const override { return QStringLiteral("keyring"); }

private:
    QString m_service;
};

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

// QtKeychain, or the process-wide in-memory backend when
// QQO_BOOKS_SECRET_BACKEND=memory.
[[nodiscard]] std::shared_ptr<SecretBackend> defaultSecretBackend();

// `<companyUuid>/<provider>`.
[[nodiscard]] QString secretKey(const QString &companyUuid, const QString &provider);

} // namespace QindaOffice::Books::Connect
