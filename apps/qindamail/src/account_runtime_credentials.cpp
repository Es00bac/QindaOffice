// SPDX-License-Identifier: GPL-3.0-or-later
// AccountRuntime: credential application and the one-retry rule
// (account_runtime.h, "AGENT-CONTRACT").
#include "account_runtime.h"

#include "mail_secrets.h"
#include "oauth2_session.h"
#include "provider_calls.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QRegularExpression>

namespace QindaOffice::Apps::Mail {

namespace {

bool isMailService(const QString &service)
{
    return service == QLatin1String("imap") || service == QLatin1String("pop3") || service == QLatin1String("smtp");
}

} // namespace

QString AccountRuntime::secretServiceFor(const QString &service) const
{
    if (service == QLatin1String("smtp") && m_account.outgoing.sameCredentialsAsIncoming
        && !m_account.incoming.isEmpty()) {
        return m_account.incoming.protocol;
    }
    if (service == QLatin1String("graph")) {
        return {};
    }
    return service;
}

QString AccountRuntime::mechanismFor(const QString &service) const
{
    if (service == QLatin1String("imap")) {
        return m_account.incoming.authMechanism.isEmpty() ? QStringLiteral("auto") : m_account.incoming.authMechanism;
    }
    if (service == QLatin1String("pop3")) {
        const QString m = m_account.incoming.authMechanism;
        if (m.isEmpty() || m == QLatin1String("auto")) return QStringLiteral("auto");
        return m == QLatin1String("login") ? QStringLiteral("user") : m;
    }
    if (service == QLatin1String("smtp")) {
        const bool shared = m_account.outgoing.sameCredentialsAsIncoming && !m_account.incoming.isEmpty();
        const QString incoming = m_account.incoming.authMechanism;
        // Shared credentials that are an OAuth token stay a token for SMTP.
        if (shared && incoming == QLatin1String("xoauth2")) return incoming;
        const QString m = m_account.outgoing.authMechanism;
        return m.isEmpty() ? QStringLiteral("auto") : m;
    }
    if (service == QLatin1String("graph")) {
        return QStringLiteral("bearer");
    }
    if (service == QLatin1String("carddav")) {   // M76
        const QString m = m_account.contacts.authMechanism;
        return m.isEmpty() ? QStringLiteral("basic") : m;
    }
    const QString m = m_account.calendar.authMechanism;
    return m.isEmpty() ? QStringLiteral("basic") : m;
}

bool AccountRuntime::usesOAuth(const QString &service) const
{
    if (m_account.oauth.provider.isEmpty()) {
        return false;
    }
    const QString mechanism = mechanismFor(service);
    return mechanism == QLatin1String("xoauth2") || mechanism == QLatin1String("bearer");
}

QString AccountRuntime::oauthResourceFor(const QString &service)
{
    if (isMailService(service)) return QStringLiteral("mail");
    if (service == QLatin1String("ews")) return QStringLiteral("ews");
    if (service == QLatin1String("carddav")) return QStringLiteral("contacts");
    return QStringLiteral("calendar");
}

bool AccountRuntime::isAuthRejection(const QVariantMap &result)
{
    if (result.value(QStringLiteral("authRejected")).toBool() || result.value(QStringLiteral("needsSignIn")).toBool()) {
        return true;
    }
    if (result.value(QStringLiteral("state")).toString() != QLatin1String("rejected")) {
        return false;
    }
    // AGENT-NOTE: the toolkit providers report an authentication failure as
    // a plain `rejected` with the server's text (no flag), so the text is
    // classified: IMAP "LOGIN/AUTHENTICATE failed: … AUTHENTICATIONFAILED",
    // POP3 "PASS/AUTH failed: -ERR …", SMTP "Authentication refused: 535",
    // CalDAV/ICS "refused the credentials (HTTP 401)".
    static const QRegularExpression auth(
        QStringLiteral("authenticat|credential|password|\\blogin\\b|\\bpass failed|\\bauth failed|"
                       "sign.?in|invalid_grant|\\b53[45]\\b|HTTP 40[13]|\\[AUTH\\]|has no credentials"),
        QRegularExpression::CaseInsensitiveOption);
    const QString error = result.value(QStringLiteral("error")).toString();
    // A TLS-policy refusal names credentials too, but no sign-in fixes it.
    if (error.contains(QStringLiteral("unencrypted"), Qt::CaseInsensitive)) {
        return false;
    }
    return auth.match(error).hasMatch();
}

void AccountRuntime::applyCredentials(const QString &service, QObject *provider, bool forceRefresh,
                                      std::function<void(const QVariantMap &failure)> ready)
{
    QPointer<QObject> target(provider);
    if (usesOAuth(service)) {
        if (m_oauth == nullptr || !m_oauthError.isEmpty()) {
            QVariantMap failure = ProviderCalls::result({}, QStringLiteral("rejected"),
                                                        m_oauthError.isEmpty() ? tr("Sign-in is not configured")
                                                                               : m_oauthError);
            failure.insert(QStringLiteral("needsSignIn"), true);
            ready(failure);
            return;
        }
        const QString resource = oauthResourceFor(service);
        const QString mechanism = mechanismFor(service);
        const QString requestId = ProviderCalls::newRequestId(QStringLiteral("token"));
        QPointer<QindaOffice::Mail::OAuth2Session> session(m_oauth);
        m_context.calls->expect(requestId, [session, target, resource, mechanism, ready](const QVariantMap &token) {
            if (!token.value(QStringLiteral("ok")).toBool()) {
                QVariantMap failure = token;
                if (token.value(QStringLiteral("reauthorizeRequired")).toBool()
                    || token.value(QStringLiteral("state")).toString() == QLatin1String("rejected")) {
                    failure.insert(QStringLiteral("needsSignIn"), true);
                }
                ready(failure);
                return;
            }
            if (session == nullptr || target == nullptr || !session->applyAccessToken(target, mechanism, resource)) {
                ready(ProviderCalls::result({}, QStringLiteral("rejected"), tr("The access token could not be applied")));
                return;
            }
            ready({});
        });
        if (forceRefresh) {
            m_oauth->refresh(requestId, resource);
        } else {
            m_oauth->ensureToken(requestId, resource);
        }
        return;
    }

    const QString secretService = secretServiceFor(service);
    const QString mechanism = mechanismFor(service);
    if (!m_passwordOverride.isEmpty()) {
        QMetaObject::invokeMethod(provider, "setSecret", Q_ARG(QString, m_passwordOverride), Q_ARG(QString, mechanism));
        ready({});
        return;
    }
    if (secretService.isEmpty() || m_context.secrets == nullptr) {
        ready({});
        return;
    }
    const bool optional = service == QLatin1String("ics");
    // M76: an address book on the account's own server usually takes the
    // account's password (an app password): without a "carddav" secret the
    // incoming (or calendar) one is used.
    QString fallback;
    if (service == QLatin1String("carddav")) {
        if (!m_account.incoming.isEmpty() && m_account.incoming.authMechanism != QLatin1String("xoauth2"))
            fallback = m_account.incoming.protocol;
        else if (m_account.calendar.protocol == QLatin1String("caldav") && m_account.calendar.authMechanism == QLatin1String("basic"))
            fallback = QStringLiteral("caldav");
    }
    if (!fallback.isEmpty()) {
        QPointer<AccountRuntime> self(this);
        m_context.secrets->read(m_account.id, secretService,
                                [self, target, mechanism, fallback, ready](const QindaOffice::Mail::SecretOutcome &outcome) {
                                    using Status = QindaOffice::Mail::SecretOutcome::Status;
                                    if (outcome.status == Status::Ok) {
                                        if (target != nullptr)
                                            QMetaObject::invokeMethod(target, "setSecret", Q_ARG(QString, outcome.value),
                                                                      Q_ARG(QString, mechanism));
                                        ready({});
                                        return;
                                    }
                                    if (self == nullptr || outcome.status != Status::NotFound) {
                                        QVariantMap failure = ProviderCalls::result({}, QStringLiteral("rejected"), outcome.error);
                                        failure.insert(QStringLiteral("unavailable"), outcome.status == Status::Unavailable);
                                        ready(failure);
                                        return;
                                    }
                                    self->readSecretInto(fallback, target, mechanism, ready);
                                });
        return;
    }
    readSecretInto(secretService, target, mechanism, ready, optional);
}

void AccountRuntime::readSecretInto(const QString &secretService, QPointer<QObject> target, const QString &mechanism,
                                    std::function<void(const QVariantMap &failure)> ready, bool optional)
{
    m_context.secrets->read(m_account.id, secretService,
                            [target, mechanism, optional, ready](const QindaOffice::Mail::SecretOutcome &outcome) {
                                using Status = QindaOffice::Mail::SecretOutcome::Status;
                                if (outcome.status == Status::Ok) {
                                    if (target != nullptr) {
                                        QMetaObject::invokeMethod(target, "setSecret", Q_ARG(QString, outcome.value),
                                                                  Q_ARG(QString, mechanism));
                                    }
                                    ready({});
                                    return;
                                }
                                if (outcome.status == Status::NotFound && optional) {
                                    ready({}); // a public feed needs no secret
                                    return;
                                }
                                QVariantMap failure = ProviderCalls::result(
                                    {}, QStringLiteral("rejected"),
                                    outcome.status == Status::NotFound ? tr("No password is stored for this account; "
                                                                            "enter it in Account Settings")
                                                                       : outcome.error);
                                failure.insert(QStringLiteral("needsSignIn"), outcome.status == Status::NotFound);
                                failure.insert(QStringLiteral("unavailable"), outcome.status == Status::Unavailable);
                                ready(failure);
                            });
}

void AccountRuntime::run(const QString &service, QObject *provider, Starter start, Done done)
{
    if (provider == nullptr) {
        done(ProviderCalls::result({}, QStringLiteral("rejected"), tr("This account has no %1 server").arg(service)));
        return;
    }
    adjustPending(+1);
    QPointer<AccountRuntime> self(this);
    Done finish = [self, done = std::move(done)](const QVariantMap &result) {
        if (self != nullptr) {
            self->adjustPending(-1);
            self->noteResult(result);
        }
        done(result);
    };
    startAttempt(service, provider, start, finish, 0);
}

void AccountRuntime::startAttempt(const QString &service, QObject *provider, const Starter &start, const Done &done,
                                  int attempt)
{
    QPointer<QObject> target(provider);
    QPointer<AccountRuntime> self(this);
    // AGENT-GUARD: every continuation checks `self`: a keyring read or token
    // refresh can complete after the account was removed (runtime deleted).
    applyCredentials(service, provider, attempt > 0, [=](const QVariantMap &failure) {
        if (!failure.isEmpty()) {
            done(failure);
            return;
        }
        if (self == nullptr || target == nullptr) {
            done(ProviderCalls::result({}, QStringLiteral("rejected"), tr("The account was removed")));
            return;
        }
        const QString requestId = ProviderCalls::newRequestId(service);
        self->m_context.calls->expect(requestId, [=](const QVariantMap &result) {
            if (attempt == 0 && isAuthRejection(result) && target != nullptr && self != nullptr) {
                // One retry with a refreshed token / a fresh keyring read.
                self->startAttempt(service, target, start, done, 1);
                return;
            }
            QVariantMap final = result;
            if (isAuthRejection(result)) {
                final.insert(QStringLiteral("needsSignIn"), true);
            }
            done(final);
        });
        // The provider answers through its signal even when it refuses the
        // request up front (next event-loop turn), so the return is advisory.
        static_cast<void>(start(requestId));
    });
}

} // namespace QindaOffice::Apps::Mail
