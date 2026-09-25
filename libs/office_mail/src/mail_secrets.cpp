// SPDX-License-Identifier: GPL-3.0-or-later
#include "mail_secrets.h"

#include "mail_result.h"

#include <QMetaObject>
#include <QRegularExpression>
#include <QTimer>

namespace QindaOffice::Mail {

MailSecrets::MailSecrets(QObject *parent)
    : MailSecrets(defaultSecretBackend(), parent)
{
}

MailSecrets::MailSecrets(std::shared_ptr<SecretBackend> backend, QObject *parent)
    : QObject(parent)
    , m_backend(std::move(backend))
{
}

QStringList MailSecrets::services()
{
    // AGENT-CONTRACT: the keyring key namespace, shared with the app lane
    // and the Exchange/Graph adapters (docs/mail.md#secrets). Append only;
    // renaming a service orphans every secret stored under the old name.
    return {QStringLiteral("imap"), QStringLiteral("smtp"), QStringLiteral("pop3"),
            QStringLiteral("caldav"), QStringLiteral("ews"), QStringLiteral("ics"),
            QStringLiteral("oauth-refresh-google"), QStringLiteral("oauth-refresh-microsoft")};
}

QString MailSecrets::keyFor(const QString &accountId, const QString &service)
{
    return accountId + QLatin1Char('/') + service;
}

QString MailSecrets::refreshServiceFor(const QString &oauthProvider)
{
    return QStringLiteral("oauth-refresh-") + oauthProvider;
}

void MailSecrets::setBackend(std::shared_ptr<SecretBackend> backend)
{
    m_backend = std::move(backend);
    emit backendChanged();
}

QString MailSecrets::backendName() const
{
    return m_backend ? m_backend->name() : QString();
}

QString MailSecrets::validateKey(const QString &accountId, const QString &service)
{
    static const QRegularExpression idPattern(QStringLiteral("^[A-Za-z0-9._-]{1,128}$"));
    if (!idPattern.match(accountId).hasMatch()) return QStringLiteral("Invalid account id.");
    if (!services().contains(service)) {
        return QStringLiteral("Unknown secret service “%1” (expected one of %2).")
            .arg(service, services().join(QStringLiteral(", ")));
    }
    return {};
}

QVariantMap MailSecrets::resultFor(const QString &requestId, const char *operation, const QString &accountId,
                                   const QString &service, const SecretOutcome &outcome) const
{
    using S = SecretOutcome::Status;
    const char *state = outcome.status == S::Ok ? kStateAccepted : kStateRejected;
    QVariantMap result = makeResult(requestId, state, outcome.error, outcome.status == S::Failed);
    result.insert(QStringLiteral("operation"), QString::fromLatin1(operation));
    result.insert(QStringLiteral("accountId"), accountId);
    result.insert(QStringLiteral("service"), service);
    result.insert(QStringLiteral("notFound"), outcome.status == S::NotFound);
    result.insert(QStringLiteral("unavailable"), outcome.status == S::Unavailable);
    result.insert(QStringLiteral("backend"), backendName());
    return result;
}

void MailSecrets::finishLater(const QString &requestId, const QVariantMap &result)
{
    QTimer::singleShot(0, this, [this, requestId, result] { emit finished(requestId, result); });
}

void MailSecrets::read(const QString &accountId, const QString &service,
                       std::function<void(const SecretOutcome &)> done)
{
    const QString invalid = validateKey(accountId, service);
    if (!invalid.isEmpty() || !m_backend) {
        const SecretOutcome outcome{SecretOutcome::Status::Failed, {},
                                    invalid.isEmpty() ? QStringLiteral("No secret backend.") : invalid};
        QTimer::singleShot(0, this, [done, outcome] { done(outcome); });
        return;
    }
    QPointer<MailSecrets> self(this);
    m_backend->read(keyFor(accountId, service), [self, done](const SecretOutcome &outcome) {
        if (self) done(outcome);
    });
}

void MailSecrets::store(const QString &accountId, const QString &service, const QString &secret,
                        std::function<void(const SecretOutcome &)> done)
{
    const QString invalid = validateKey(accountId, service);
    if (!invalid.isEmpty() || !m_backend || secret.isEmpty()) {
        const QString why = !invalid.isEmpty() ? invalid
            : secret.isEmpty() ? QStringLiteral("An empty secret is not stored; remove it instead.")
                               : QStringLiteral("No secret backend.");
        const SecretOutcome outcome{SecretOutcome::Status::Failed, {}, why};
        QTimer::singleShot(0, this, [done, outcome] { done(outcome); });
        return;
    }
    QPointer<MailSecrets> self(this);
    m_backend->write(keyFor(accountId, service), secret, [self, done](const SecretOutcome &outcome) {
        if (self) done(outcome);
    });
}

void MailSecrets::erase(const QString &accountId, const QString &service,
                        std::function<void(const SecretOutcome &)> done)
{
    const QString invalid = validateKey(accountId, service);
    if (!invalid.isEmpty() || !m_backend) {
        const SecretOutcome outcome{SecretOutcome::Status::Failed, {},
                                    invalid.isEmpty() ? QStringLiteral("No secret backend.") : invalid};
        QTimer::singleShot(0, this, [done, outcome] { done(outcome); });
        return;
    }
    QPointer<MailSecrets> self(this);
    m_backend->remove(keyFor(accountId, service), [self, done](const SecretOutcome &outcome) {
        if (self) done(outcome);
    });
}

void MailSecrets::write(const QString &requestId, const QString &accountId, const QString &service,
                        const QString &secret)
{
    store(accountId, service, secret, [this, requestId, accountId, service](const SecretOutcome &outcome) {
        emit finished(requestId, resultFor(requestId, "write", accountId, service, outcome));
    });
}

void MailSecrets::remove(const QString &requestId, const QString &accountId, const QString &service)
{
    erase(accountId, service, [this, requestId, accountId, service](const SecretOutcome &outcome) {
        emit finished(requestId, resultFor(requestId, "remove", accountId, service, outcome));
    });
}

void MailSecrets::removeAccount(const QString &requestId, const QString &accountId)
{
    const QString invalid = validateKey(accountId, services().constFirst());
    if (!invalid.isEmpty() || !m_backend) {
        const SecretOutcome outcome{SecretOutcome::Status::Failed, {},
                                    invalid.isEmpty() ? QStringLiteral("No secret backend.") : invalid};
        finishLater(requestId, resultFor(requestId, "removeAccount", accountId, {}, outcome));
        return;
    }
    // One shared tally; the last removal to finish emits the result.
    struct Tally {
        qsizetype pending = 0;
        qsizetype removed = 0;
        SecretOutcome worst{SecretOutcome::Status::Ok, {}, {}};
    };
    auto tally = std::make_shared<Tally>();
    const QStringList all = services();
    tally->pending = all.size();
    for (const QString &service : all) {
        erase(accountId, service, [this, tally, requestId, accountId](const SecretOutcome &outcome) {
            if (outcome.status == SecretOutcome::Status::Ok) ++tally->removed;
            else if (tally->worst.status == SecretOutcome::Status::Ok) tally->worst = outcome;
            if (--tally->pending > 0) return;
            QVariantMap result = resultFor(requestId, "removeAccount", accountId, {}, tally->worst);
            result.insert(QStringLiteral("removed"), tally->removed);
            emit finished(requestId, result);
        });
    }
}

void MailSecrets::check(const QString &requestId, const QString &accountId, const QString &service)
{
    read(accountId, service, [this, requestId, accountId, service](const SecretOutcome &outcome) {
        // A missing secret is a normal answer to "is there one?".
        SecretOutcome shown = outcome;
        shown.value.clear();
        if (outcome.status == SecretOutcome::Status::NotFound) {
            shown = SecretOutcome{SecretOutcome::Status::Ok, {}, {}};
        }
        QVariantMap result = resultFor(requestId, "check", accountId, service, shown);
        result.insert(QStringLiteral("exists"), outcome.status == SecretOutcome::Status::Ok);
        emit finished(requestId, result);
    });
}

void MailSecrets::applySecret(const QString &requestId, const QString &accountId, const QString &service,
                              QObject *target, const QString &mechanism)
{
    QPointer<QObject> guardTarget(target);
    read(accountId, service,
         [this, requestId, accountId, service, guardTarget, mechanism](const SecretOutcome &outcome) {
             SecretOutcome shown = outcome;
             shown.value.clear();
             bool applied = false;
             if (outcome.status == SecretOutcome::Status::Ok) {
                 if (!guardTarget) {
                     shown = SecretOutcome{SecretOutcome::Status::Failed, {},
                                           QStringLiteral("The connection object is gone.")};
                 } else {
                     applied = QMetaObject::invokeMethod(guardTarget.data(), "setSecret", Qt::DirectConnection,
                                                         Q_ARG(QString, outcome.value), Q_ARG(QString, mechanism));
                     if (!applied) {
                         shown = SecretOutcome{SecretOutcome::Status::Failed, {},
                                               QStringLiteral("The target has no setSecret(secret, mechanism).")};
                     }
                 }
             }
             QVariantMap result = resultFor(requestId, "applySecret", accountId, service, shown);
             result.insert(QStringLiteral("applied"), applied);
             emit finished(requestId, result);
         });
}

} // namespace QindaOffice::Mail
