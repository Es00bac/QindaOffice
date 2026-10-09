// SPDX-License-Identifier: GPL-3.0-or-later
// OAuth2Session, token endpoint side: code redemption, refresh-token grants
// (per resource, coalesced), error mapping and keyring persistence.

#include "bounded_http.h"
#include "mail_result.h"
#include "mail_secrets.h"
#include "oauth2_session.h"
#include "oauth2_session_p.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTimer>

namespace QindaOffice::Mail {
namespace {

QByteArray formBody(const QList<std::pair<QString, QString>> &form)
{
    // AGENT-GUARD: encode by hand. QUrlQuery leaves '+' as is, and a form
    // decoder reads '+' as a space — a refresh token containing '+' would
    // arrive corrupted and look like invalid_grant.
    QByteArray body;
    for (const auto &[key, value] : form) {
        if (!body.isEmpty()) body += '&';
        body += QUrl::toPercentEncoding(key) + '=' + QUrl::toPercentEncoding(value);
    }
    return body;
}

} // namespace

QPointer<QNetworkReply> OAuth2Session::postToken(const QList<std::pair<QString, QString>> &form,
                                                 std::function<void(const TokenGrant &)> done)
{
    const std::optional<OAuthProfile> p = profile();
    const QUrl url = m_tokenOverride.isValid() ? m_tokenOverride : (p ? p->tokenUrl(m_tenant) : QUrl());
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/x-www-form-urlencoded"));
    request.setRawHeader("Accept", "application/json");
    HttpLimits limits;
    limits.timeoutMs = m_tokenTimeoutMs;
    limits.maxBytes = 64 * 1024;
    limits.followRedirects = false;
    return BoundedHttp::send(manager(), request, "POST", formBody(form), limits, this, [done](const HttpOutcome &o) {
        TokenGrant grant;
        using K = HttpOutcome::Kind;
        if (o.kind == K::Aborted) {
            grant.aborted = true;
            grant.error = o.error;
            return done(grant);
        }
        if (o.offline()) {
            grant.state = kStateOffline;
            grant.retryable = true;
            grant.error = QStringLiteral("The sign-in service could not be reached: %1").arg(o.error);
            return done(grant);
        }
        if (o.kind == K::Refused || o.kind == K::TooLarge) {
            grant.error = o.error;
            return done(grant);
        }
        const QJsonObject json = QJsonDocument::fromJson(o.body).object();
        const QString error = json.value(QLatin1String("error")).toString();
        if (!error.isEmpty()) {
            const QString description = json.value(QLatin1String("error_description")).toString().left(300);
            const QString detail = description.isEmpty() ? error : error + QStringLiteral(": ") + description;
            if (error == QLatin1String("invalid_grant")) {
                grant.invalidGrant = true;
                grant.error = QStringLiteral("The saved sign-in is no longer valid (%1). Sign in again.").arg(detail);
            } else if (error == QLatin1String("invalid_client") || error == QLatin1String("unauthorized_client")) {
                grant.error = QStringLiteral("The OAuth client was refused (%1). Check the client ID (and secret) in "
                                             "oauth-clients.json or the account settings.")
                                  .arg(detail);
            } else if (error == QLatin1String("temporarily_unavailable") || error == QLatin1String("server_error")) {
                grant.state = kStateOffline;
                grant.retryable = true;
                grant.error = QStringLiteral("The sign-in service is unavailable (%1).").arg(detail);
            } else {
                grant.error = QStringLiteral("The sign-in service refused: %1").arg(detail);
            }
            return done(grant);
        }
        const QString accessToken = json.value(QLatin1String("access_token")).toString();
        const QString tokenType = json.value(QLatin1String("token_type")).toString();
        if (o.kind != K::Ok || accessToken.isEmpty()) {
            if (o.status >= 500) {
                grant.state = kStateOffline;
                grant.retryable = true;
            }
            grant.error = QStringLiteral("The sign-in service gave an unexpected answer (HTTP %1).").arg(o.status);
            return done(grant);
        }
        if (!tokenType.isEmpty() && tokenType.compare(QLatin1String("bearer"), Qt::CaseInsensitive) != 0) {
            grant.error = QStringLiteral("The sign-in service issued a “%1” token; only bearer tokens are supported.")
                              .arg(tokenType.left(32));
            return done(grant);
        }
        grant.ok = true;
        grant.state = kStateAccepted;
        grant.accessToken = accessToken;
        grant.refreshToken = json.value(QLatin1String("refresh_token")).toString();
        grant.scope = json.value(QLatin1String("scope")).toString();
        const QJsonValue expires = json.value(QLatin1String("expires_in"));
        // Some servers send the number as a string.
        grant.expiresIn = expires.isString() ? expires.toString().toLongLong() : expires.toInteger(3600);
        if (grant.expiresIn <= 0) grant.expiresIn = 3600;
        done(grant);
    });
}

void OAuth2Session::storeGrant(const TokenGrant &grant, const QString &resource)
{
    const Token token{grant.accessToken, QDateTime::currentDateTimeUtc().addSecs(grant.expiresIn), grant.scope};
    const std::optional<OAuthProfile> p = profile();
    if (p && p->oneTokenForAllResources) {
        // Google: one token carries every consented scope.
        for (const QString &r : p->resourceScopes.keys()) m_tokens.insert(r, token);
    }
    m_tokens.insert(resource, token);
    if (!grant.refreshToken.isEmpty()) m_refreshToken = grant.refreshToken;
    emit tokensChanged();
}

void OAuth2Session::persistRefreshToken(std::function<void(bool, const QString &)> done)
{
    if (!m_secrets || m_accountId.isEmpty() || m_refreshToken.isEmpty()) {
        QTimer::singleShot(0, this, [done] {
            done(false, QStringLiteral("The sign-in is kept only until QindaMail closes (no keyring connection)."));
        });
        return;
    }
    // AGENT-GUARD: MailSecrets only guards its own lifetime; the keyring may
    // answer after this session is gone.
    QPointer<OAuth2Session> self(this);
    m_secrets->store(m_accountId, MailSecrets::refreshServiceFor(m_provider), m_refreshToken,
                     [self, done](const SecretOutcome &outcome) {
                         if (self) done(outcome.status == SecretOutcome::Status::Ok, outcome.error);
                     });
}

void OAuth2Session::forgetRefreshToken()
{
    m_refreshToken.clear();
    m_tokens.clear();
    emit tokensChanged();
    if (m_secrets && !m_accountId.isEmpty()) {
        m_secrets->erase(m_accountId, MailSecrets::refreshServiceFor(m_provider), [](const SecretOutcome &) {});
    }
}

void OAuth2Session::exchangeCode(const QString &code)
{
    const std::optional<OAuthProfile> p = profile();
    const QString resource = primaryResource();
    QList<std::pair<QString, QString>> form = {
        {QStringLiteral("grant_type"), QStringLiteral("authorization_code")},
        {QStringLiteral("code"), code},
        {QStringLiteral("redirect_uri"), m_auth->redirectUri.toString()},
        {QStringLiteral("client_id"), m_clientId},
        {QStringLiteral("code_verifier"), QString::fromLatin1(m_auth->verifier)},
    };
    if (!m_clientSecret.isEmpty()) form.append({QStringLiteral("client_secret"), m_clientSecret});
    if (p && !p->oneTokenForAllResources) form.append({QStringLiteral("scope"), scopesFor(resource).join(QLatin1Char(' '))});
    m_auth->exchange = postToken(form, [this, resource](const TokenGrant &grant) {
        if (!m_auth || grant.aborted) return;
        m_auth->exchange = nullptr;
        if (!grant.ok) {
            failAuthorize(grant.state, grant.error, grant.retryable);
            return;
        }
        storeGrant(grant, resource);
        persistRefreshToken([this, resource](bool stored, const QString &error) {
            if (!m_auth) return;
            QVariantMap result = baseResult(m_auth->requestId, kStateAccepted, resource);
            result.insert(QStringLiteral("refreshTokenStored"), stored);
            if (!hasRefreshToken()) {
                result.insert(QStringLiteral("error"),
                              QStringLiteral("The service returned no refresh token; sign in again when this one expires."));
            } else if (!stored) {
                result.insert(QStringLiteral("error"), error);
            }
            finishAuthorize(result);
        });
    });
}

void OAuth2Session::ensureToken(const QString &requestId, const QString &resource)
{
    if (!isTokenValid(resource)) {
        refresh(requestId, resource);
        return;
    }
    QVariantMap result = baseResult(requestId, kStateAccepted, resource);
    result.insert(QStringLiteral("cached"), true);
    QTimer::singleShot(0, this, [this, requestId, result] { emit tokenFinished(requestId, result); });
}

void OAuth2Session::refresh(const QString &requestId, const QString &resource)
{
    QString invalid;
    if (!profile()) invalid = QStringLiteral("No OAuth provider is set (google or microsoft).");
    else if (scopesFor(resource).isEmpty()) invalid = QStringLiteral("Unknown token resource “%1”.").arg(resource);
    else if (m_clientId.isEmpty()) invalid = m_registry.resolve(m_provider).error;
    if (!invalid.isEmpty()) {
        const QVariantMap result = baseResult(requestId, kStateRejected, resource, invalid);
        QTimer::singleShot(0, this, [this, requestId, result] { emit tokenFinished(requestId, result); });
        return;
    }
    // One grant per resource at a time; later callers share its answer
    // (Microsoft rotates refresh tokens, so parallel grants would race).
    QStringList &waiting = m_waiting[resource];
    waiting.append(requestId);
    if (waiting.size() > 1) return;

    if (!m_refreshToken.isEmpty()) {
        startRefresh(resource);
        return;
    }
    if (!m_secrets || m_accountId.isEmpty()) {
        QVariantMap result = baseResult({}, kStateRejected, resource, QStringLiteral("Not signed in. Sign in first."));
        result.insert(QStringLiteral("reauthorizeRequired"), true);
        QTimer::singleShot(0, this, [this, resource, result] { finishRefresh(resource, result); });
        return;
    }
    QPointer<OAuth2Session> self(this);
    m_secrets->read(m_accountId, MailSecrets::refreshServiceFor(m_provider),
                    [this, self, resource](const SecretOutcome &outcome) {
                        if (!self) return;
                        if (outcome.status == SecretOutcome::Status::Ok && !outcome.value.isEmpty()) {
                            setRefreshToken(outcome.value);
                            startRefresh(resource);
                            return;
                        }
                        const bool missing = outcome.status == SecretOutcome::Status::NotFound;
                        QVariantMap result = baseResult(
                            {}, kStateRejected, resource,
                            missing ? QStringLiteral("No saved sign-in for this account. Sign in again.") : outcome.error);
                        result.insert(QStringLiteral("reauthorizeRequired"), missing);
                        result.insert(QStringLiteral("unavailable"),
                                      outcome.status == SecretOutcome::Status::Unavailable);
                        finishRefresh(resource, result);
                    });
}

void OAuth2Session::startRefresh(const QString &resource)
{
    const std::optional<OAuthProfile> p = profile();
    const QString previous = m_refreshToken;
    QList<std::pair<QString, QString>> form = {
        {QStringLiteral("grant_type"), QStringLiteral("refresh_token")},
        {QStringLiteral("refresh_token"), m_refreshToken},
        {QStringLiteral("client_id"), m_clientId},
    };
    if (!m_clientSecret.isEmpty()) form.append({QStringLiteral("client_secret"), m_clientSecret});
    // Microsoft: the scope set selects the resource the token is for.
    if (p && !p->oneTokenForAllResources) form.append({QStringLiteral("scope"), scopesFor(resource).join(QLatin1Char(' '))});
    postToken(form, [this, resource, previous](const TokenGrant &grant) {
        if (grant.aborted) return;
        if (!grant.ok) {
            // AGENT-GUARD (M81, D-090): Microsoft answers invalid_grant with
            // AADSTS65001 when the user never consented to a resource's
            // scopes (a resource added after they signed in). That refresh
            // token still works for the resources they did grant: forgetting
            // it would sign the whole account out for one optional feature.
            const bool consent = grant.invalidGrant && resource != primaryResource()
                && grant.error.contains(QStringLiteral("AADSTS65001"));
            if (grant.invalidGrant && !consent) forgetRefreshToken();
            QVariantMap result = baseResult({}, grant.state, resource, grant.error, grant.retryable);
            result.insert(QStringLiteral("reauthorizeRequired"), grant.invalidGrant);
            result.insert(QStringLiteral("consentRequired"), consent);
            finishRefresh(resource, result);
            return;
        }
        storeGrant(grant, resource);
        if (m_refreshToken == previous) {
            finishRefresh(resource, baseResult({}, kStateAccepted, resource));
            return;
        }
        // A rotated refresh token replaces the stored one.
        persistRefreshToken([this, resource](bool stored, const QString &error) {
            QVariantMap result = baseResult({}, kStateAccepted, resource);
            result.insert(QStringLiteral("refreshTokenStored"), stored);
            if (!stored) result.insert(QStringLiteral("error"), error);
            finishRefresh(resource, result);
        });
    });
}

void OAuth2Session::finishRefresh(const QString &resource, const QVariantMap &resultTemplate)
{
    const QStringList waiting = m_waiting.take(resource);
    if (!resultTemplate.value(QStringLiteral("ok")).toBool()) {
        setLastError(resultTemplate.value(QStringLiteral("error")).toString());
    }
    for (const QString &requestId : waiting) {
        QVariantMap result = resultTemplate;
        result.insert(QStringLiteral("requestId"), requestId);
        emit tokenFinished(requestId, result);
    }
}

} // namespace QindaOffice::Mail
