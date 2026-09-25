// SPDX-License-Identifier: GPL-3.0-or-later
// OAuth2Session: configuration, the authorization-code flow over the
// loopback redirect, and token hand-over. The token endpoint side (code
// exchange, refresh, keyring persistence) is in oauth2_tokens.cpp.

#include "oauth2_session.h"

#include "bounded_http.h"
#include "mail_account.h"
#include "mail_result.h"
#include "oauth2_session_p.h"
#include "pkce.h"

#include <QDesktopServices>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QUrlQuery>

namespace QindaOffice::Mail {

OAuth2Session::OAuth2Session(QObject *parent)
    : QObject(parent)
    , m_opener([](const QUrl &url) { return QDesktopServices::openUrl(url); })
{
}

OAuth2Session::~OAuth2Session()
{
    if (m_auth) BoundedHttp::cancel(m_auth->exchange);
}

void OAuth2Session::setProvider(const QString &provider)
{
    if (provider == m_provider) return;
    m_provider = provider;
    clearTokens(); // tokens belong to one provider + client + account
    emit configurationChanged();
}

void OAuth2Session::setClientId(const QString &clientId)
{
    if (clientId == m_clientId) return;
    m_clientId = clientId;
    clearTokens();
    emit configurationChanged();
}

void OAuth2Session::setTenant(const QString &tenant)
{
    if (tenant == m_tenant) return;
    m_tenant = tenant;
    emit configurationChanged();
}

void OAuth2Session::setLoginHint(const QString &hint)
{
    if (hint == m_loginHint) return;
    m_loginHint = hint;
    emit configurationChanged();
}

void OAuth2Session::setResources(const QStringList &resources)
{
    if (resources == m_resources || resources.isEmpty()) return;
    m_resources = resources;
    emit configurationChanged();
}

void OAuth2Session::setAccountId(const QString &accountId)
{
    if (accountId == m_accountId) return;
    m_accountId = accountId;
    clearTokens();
    emit configurationChanged();
}

void OAuth2Session::setSecrets(MailSecrets *secrets)
{
    if (secrets == m_secrets) return;
    m_secrets = secrets;
    emit configurationChanged();
}

void OAuth2Session::setTimeoutMs(int ms)
{
    ms = qMax(1, ms);
    if (ms == m_timeoutMs) return;
    m_timeoutMs = ms;
    emit configurationChanged();
}

void OAuth2Session::setEndpoints(const QUrl &authorization, const QUrl &token)
{
    m_authorizationOverride = authorization;
    m_tokenOverride = token;
}

void OAuth2Session::setNetworkAccessManager(QNetworkAccessManager *manager)
{
    if (m_ownsManager) delete m_manager;
    m_manager = manager;
    m_ownsManager = false;
}

QNetworkAccessManager *OAuth2Session::manager()
{
    if (!m_manager) {
        m_manager = new QNetworkAccessManager(this);
        m_ownsManager = true;
    }
    return m_manager;
}

void OAuth2Session::setResourceScopes(const QString &resource, const QStringList &scopes)
{
    if (scopes.isEmpty()) m_scopeOverrides.remove(resource);
    else m_scopeOverrides.insert(resource, scopes);
}

std::optional<OAuthProfile> OAuth2Session::profile() const
{
    return OAuthProfile::forProvider(m_provider);
}

QStringList OAuth2Session::scopesFor(const QString &resource) const
{
    if (m_scopeOverrides.contains(resource)) return m_scopeOverrides.value(resource);
    const std::optional<OAuthProfile> p = profile();
    return p ? p->resourceScopes.value(resource) : QStringList();
}

QUrl OAuth2Session::authorizationUrl() const
{
    return m_auth ? m_auth->url : QUrl();
}

QString OAuth2Session::accessToken(const QString &resource) const
{
    return m_tokens.value(resource).value;
}

QDateTime OAuth2Session::expiresAt(const QString &resource) const
{
    return m_tokens.value(resource).expiresAt;
}

bool OAuth2Session::isTokenValid(const QString &resource) const
{
    const auto it = m_tokens.constFind(resource);
    if (it == m_tokens.constEnd() || it->value.isEmpty()) return false;
    return QDateTime::currentDateTimeUtc().addSecs(kExpirySkewSeconds) < it->expiresAt;
}

bool OAuth2Session::applyAccessToken(QObject *target, const QString &mechanism, const QString &resource) const
{
    if (!target || !isTokenValid(resource)) return false;
    return QMetaObject::invokeMethod(target, "setSecret", Qt::DirectConnection,
                                     Q_ARG(QString, m_tokens.value(resource).value), Q_ARG(QString, mechanism));
}

void OAuth2Session::signOut()
{
    clearTokens();
}

void OAuth2Session::clearTokens()
{
    const bool had = !m_tokens.isEmpty() || !m_refreshToken.isEmpty();
    m_tokens.clear();
    m_refreshToken.clear();
    if (had) emit tokensChanged();
}

void OAuth2Session::setRefreshToken(const QString &token)
{
    if (token == m_refreshToken) return;
    m_refreshToken = token;
    emit tokensChanged();
}

void OAuth2Session::setLastError(const QString &error)
{
    if (error == m_lastError) return;
    m_lastError = error;
    emit lastErrorChanged();
}

QVariantMap OAuth2Session::baseResult(const QString &requestId, const char *state, const QString &resource,
                                      const QString &error, bool retryable) const
{
    QVariantMap result = makeResult(requestId, state, error, retryable);
    const Token token = m_tokens.value(resource);
    result.insert(QStringLiteral("provider"), m_provider);
    result.insert(QStringLiteral("resource"), resource);
    result.insert(QStringLiteral("expiresAt"),
                  token.expiresAt.isValid() ? token.expiresAt.toUTC().toString(Qt::ISODate) : QString());
    result.insert(QStringLiteral("scope"), token.scope);
    result.insert(QStringLiteral("hasRefreshToken"), hasRefreshToken());
    result.insert(QStringLiteral("reauthorizeRequired"), false);
    result.insert(QStringLiteral("refreshTokenStored"), false);
    result.insert(QStringLiteral("cancelled"), false);
    result.insert(QStringLiteral("cached"), false);
    return result;
}

QVariantMap OAuth2Session::configure(const QVariantMap &accountMap)
{
    const MailAccount account = MailAccount::fromVariantMap(accountMap);
    const auto fail = [this](const QString &error) {
        setLastError(error);
        return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error},
                           {QStringLiteral("source"), QString()}};
    };
    if (account.oauth.provider.isEmpty()) return fail(QStringLiteral("This account does not sign in with OAuth."));
    const OAuthClientRegistry::Resolution resolution =
        m_registry.resolve(account.oauth.provider, account.oauth.clientId, account.oauth.tenant);
    if (!resolution.ok) return fail(resolution.error);

    setProvider(account.oauth.provider);
    setClientId(resolution.client.clientId);
    m_clientSecret = resolution.client.clientSecret;
    setTenant(resolution.client.tenant);
    setAccountId(account.id);
    setLoginHint(account.emailAddress);
    QStringList resources;
    if (account.hasMail()) resources << QStringLiteral("mail");
    const bool bearer = account.calendar.authMechanism == QLatin1String("bearer");
    if (account.calendar.protocol == QLatin1String("graph")
        || (account.calendar.protocol == QLatin1String("caldav") && bearer)) {
        resources << QStringLiteral("calendar");
    } else if (account.calendar.protocol == QLatin1String("ews") && bearer) {
        resources << QStringLiteral("ews");
    }
    if (resources.isEmpty()) resources << QStringLiteral("mail");
    setResources(resources);
    setResourceScopes(QStringLiteral("mail"), account.oauth.scopes);
    setLastError({});
    return {{QStringLiteral("ok"), true}, {QStringLiteral("error"), QString()},
            {QStringLiteral("source"), resolution.client.source}, {QStringLiteral("clientId"), m_clientId}};
}

void OAuth2Session::authorize(const QString &requestId)
{
    const auto rejectLater = [this, requestId](const QString &error) {
        const QVariantMap result = baseResult(requestId, kStateRejected, primaryResource(), error);
        setLastError(error);
        QTimer::singleShot(0, this, [this, requestId, result] { emit authorizeFinished(requestId, result); });
    };
    if (m_auth) return rejectLater(QStringLiteral("A sign-in is already waiting for the browser."));
    const std::optional<OAuthProfile> p = profile();
    if (!p) return rejectLater(QStringLiteral("No OAuth provider is set (google or microsoft)."));
    if (m_clientId.isEmpty()) return rejectLater(m_registry.resolve(m_provider).error);

    auto flow = std::make_unique<AuthFlow>();
    flow->requestId = requestId;
    flow->verifier = Pkce::generateVerifier();
    flow->state = Pkce::generateState();
    flow->receiver = new LoopbackReceiver([this](const QUrlQuery &query) { return onRedirect(query); }, this);
    QString error;
    if (!flow->receiver->listen(&error)) {
        flow->receiver->deleteLater();
        return rejectLater(error);
    }
    flow->redirectUri = flow->receiver->redirectUri();

    QStringList scopes;
    for (const QString &resource : std::as_const(m_resources)) {
        for (const QString &scope : scopesFor(resource)) {
            if (!scopes.contains(scope)) scopes.append(scope);
        }
    }
    QUrl url = m_authorizationOverride.isValid() ? m_authorizationOverride : p->authorizationUrl(m_tenant);
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("client_id"), m_clientId);
    query.addQueryItem(QStringLiteral("redirect_uri"), flow->redirectUri.toString());
    query.addQueryItem(QStringLiteral("scope"), scopes.join(QLatin1Char(' ')));
    query.addQueryItem(QStringLiteral("state"), QString::fromLatin1(flow->state));
    query.addQueryItem(QStringLiteral("code_challenge"), QString::fromLatin1(Pkce::challengeS256(flow->verifier)));
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    if (!m_loginHint.isEmpty()) query.addQueryItem(QStringLiteral("login_hint"), m_loginHint);
    for (const auto &[key, value] : p->extraAuthorizeParameters) query.addQueryItem(key, value);
    // Encode every reserved character in values (scope URLs contain ':' '/').
    QUrlQuery encoded;
    for (const auto &[key, value] : query.queryItems(QUrl::FullyDecoded)) {
        encoded.addQueryItem(QString::fromLatin1(QUrl::toPercentEncoding(key)),
                             QString::fromLatin1(QUrl::toPercentEncoding(value)));
    }
    url.setQuery(encoded.query(QUrl::FullyEncoded), QUrl::StrictMode);
    flow->url = url;

    flow->timer = new QTimer(this);
    flow->timer->setSingleShot(true);
    connect(flow->timer, &QTimer::timeout, this, [this] {
        failAuthorize(kStateRejected,
                      QStringLiteral("The sign-in did not finish within %1 minutes.").arg(m_timeoutMs / 60000.0, 0, 'g', 2),
                      true);
    });
    flow->timer->start(m_timeoutMs);
    m_auth = std::move(flow);
    setLastError({});
    emit authorizingChanged();

    if (!m_opener || !m_opener(url)) {
        // Keep waiting: the dialog offers authorizationUrl to open by hand.
        setLastError(QStringLiteral("No web browser could be opened. Open the sign-in address yourself."));
    }
}

LoopbackReceiver::Page OAuth2Session::onRedirect(const QUrlQuery &query)
{
    const auto failed = [](const QString &text) {
        return LoopbackReceiver::Page{false, QStringLiteral("Sign-in was not completed"), text};
    };
    if (!m_auth) return failed(QStringLiteral("This sign-in is no longer active. Return to QindaMail and try again."));
    // AGENT-GUARD: RFC 6749 §10.12 — a callback whose `state` is not the one
    // this flow generated is a forged or stale redirect. Reject it and end
    // the flow; never exchange its code.
    const QByteArray state = query.queryItemValue(QStringLiteral("state"), QUrl::FullyDecoded).toLatin1();
    if (state.isEmpty() || state != m_auth->state) {
        failAuthorize(kStateRejected,
                      QStringLiteral("The browser returned a sign-in answer that does not belong to this request "
                                     "(state mismatch); nothing was accepted. Try again."),
                      false);
        return failed(QStringLiteral("The answer did not match QindaMail's request. Return to QindaMail and try again."));
    }
    const QString error = query.queryItemValue(QStringLiteral("error"), QUrl::FullyDecoded);
    if (!error.isEmpty()) {
        const QString description =
            query.queryItemValue(QStringLiteral("error_description"), QUrl::FullyDecoded).left(300);
        const QString message = error == QLatin1String("access_denied")
            ? QStringLiteral("Sign-in was declined in the browser.")
            : QStringLiteral("The sign-in service refused: %1%2")
                  .arg(error.left(64), description.isEmpty() ? QString() : QStringLiteral(" — ") + description);
        failAuthorize(kStateRejected, message, false);
        return failed(message);
    }
    const QString code = query.queryItemValue(QStringLiteral("code"), QUrl::FullyDecoded);
    exchangeCode(code);
    return LoopbackReceiver::Page{true, QStringLiteral("QindaMail received your sign-in"),
                                  QStringLiteral("You can close this tab and return to QindaMail.")};
}

void OAuth2Session::cancel()
{
    if (!m_auth) return;
    BoundedHttp::cancel(m_auth->exchange);
    failAuthorize(kStateRejected, QStringLiteral("Sign-in was cancelled."), false, QStringLiteral("cancelled"));
}

void OAuth2Session::failAuthorize(const char *state, const QString &error, bool retryable, const QString &extraFlag)
{
    if (!m_auth) return;
    QVariantMap result = baseResult(m_auth->requestId, state, primaryResource(), error, retryable);
    if (!extraFlag.isEmpty()) result.insert(extraFlag, true);
    finishAuthorize(result);
}

void OAuth2Session::finishAuthorize(const QVariantMap &result)
{
    if (!m_auth) return;
    const std::unique_ptr<AuthFlow> flow = std::move(m_auth);
    flow->timer->stop();
    flow->timer->deleteLater();
    // deleteLater: this may run inside the receiver's own callback.
    flow->receiver->close();
    flow->receiver->deleteLater();
    setLastError(result.value(QStringLiteral("error")).toString());
    emit authorizingChanged();
    emit authorizeFinished(flow->requestId, result);
}

} // namespace QindaOffice::Mail
