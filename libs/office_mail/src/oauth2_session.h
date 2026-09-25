// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// OAuth 2.0 sign-in for one account: Authorization Code + PKCE (RFC 7636,
// S256) with a loopback redirect (RFC 8252 §7.3), token exchange, refresh,
// and per-resource access tokens (Microsoft issues one per resource).
//
// Flow: configure(account) resolves the client (OAuthClientRegistry) →
// authorize(requestId) opens the browser and waits on 127.0.0.1 → the
// redirect's `state` is checked → the code is exchanged → the refresh token
// goes to the keyring (MailSecrets, `<accountId>/oauth-refresh-<provider>`)
// → authorizeFinished. Later: ensureToken(requestId, resource) before each
// provider request → tokenFinished → applyAccessToken(provider, "xoauth2"
// or "bearer", resource).
//
// AGENT-GUARD: tokens are never logged, never in a result map and never a
// QML property. accessToken() is C++ only; QML hands a token to a toolkit
// provider with applyAccessToken(). The refresh token is write-only here
// and persisted only through MailSecrets. tst_mail_oauth captures every
// qDebug/qWarning of a full flow and greps for the tokens.
//
// Results (mail_result.h contract) add: provider, resource, expiresAt (ISO
// 8601 UTC, "" when none), hasRefreshToken, scope (granted, space
// separated), reauthorizeRequired (the refresh token is dead or missing:
// run authorize() again), refreshTokenStored (the keyring accepted it),
// cancelled, cached (ensureToken answered from memory).

#include "loopback_receiver.h"
#include "mail_secrets.h"
#include "oauth2_profiles.h"
#include "oauth_client_registry.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <memory>
#include <optional>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace QindaOffice::Mail {

struct HttpOutcome;

class OAuth2Session : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString provider READ provider WRITE setProvider NOTIFY configurationChanged)
    Q_PROPERTY(QString clientId READ clientId WRITE setClientId NOTIFY configurationChanged)
    Q_PROPERTY(QString tenant READ tenant WRITE setTenant NOTIFY configurationChanged)
    Q_PROPERTY(QString loginHint READ loginHint WRITE setLoginHint NOTIFY configurationChanged)
    // Resources consented to at authorize(): "mail", "calendar".
    Q_PROPERTY(QStringList resources READ resources WRITE setResources NOTIFY configurationChanged)
    Q_PROPERTY(QString accountId READ accountId WRITE setAccountId NOTIFY configurationChanged)
    Q_PROPERTY(QindaOffice::Mail::MailSecrets *secrets READ secrets WRITE setSecrets NOTIFY configurationChanged)
    // How long authorize() waits for the browser (default 5 minutes).
    Q_PROPERTY(int timeoutMs READ timeoutMs WRITE setTimeoutMs NOTIFY configurationChanged)
    Q_PROPERTY(bool authorizing READ authorizing NOTIFY authorizingChanged)
    // While authorizing: the URL the browser was sent to, for a "copy link"
    // fallback when no browser opened. Holds no secret (PKCE sends only the
    // challenge).
    Q_PROPERTY(QUrl authorizationUrl READ authorizationUrl NOTIFY authorizingChanged)
    Q_PROPERTY(bool hasRefreshToken READ hasRefreshToken NOTIFY tokensChanged)
    // Expiry / validity of the primary resource's token (the first of
    // `resources`, normally "mail"); invalid QDateTime when none.
    Q_PROPERTY(QDateTime expiresAt READ expiresAt NOTIFY tokensChanged)
    Q_PROPERTY(bool hasValidToken READ hasValidToken NOTIFY tokensChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    using BrowserOpener = std::function<bool(const QUrl &)>;
    // A token counts as valid until this long before it expires.
    static constexpr int kExpirySkewSeconds = 60;

    explicit OAuth2Session(QObject *parent = nullptr);
    ~OAuth2Session() override;

    [[nodiscard]] QString provider() const { return m_provider; }
    void setProvider(const QString &provider);
    [[nodiscard]] QString clientId() const { return m_clientId; }
    void setClientId(const QString &clientId);
    [[nodiscard]] QString tenant() const { return m_tenant; }
    void setTenant(const QString &tenant);
    [[nodiscard]] QString loginHint() const { return m_loginHint; }
    void setLoginHint(const QString &hint);
    [[nodiscard]] QStringList resources() const { return m_resources; }
    void setResources(const QStringList &resources);
    [[nodiscard]] QString accountId() const { return m_accountId; }
    void setAccountId(const QString &accountId);
    [[nodiscard]] MailSecrets *secrets() const { return m_secrets; }
    void setSecrets(MailSecrets *secrets);
    [[nodiscard]] int timeoutMs() const { return m_timeoutMs; }
    void setTimeoutMs(int ms);
    [[nodiscard]] bool authorizing() const { return m_auth != nullptr; }
    [[nodiscard]] QUrl authorizationUrl() const;
    [[nodiscard]] bool hasRefreshToken() const { return !m_refreshToken.isEmpty(); }
    [[nodiscard]] QDateTime expiresAt() const { return expiresAt(primaryResource()); }
    [[nodiscard]] bool hasValidToken() const { return isTokenValid(primaryResource()); }
    [[nodiscard]] QString primaryResource() const { return m_resources.value(0, QStringLiteral("mail")); }
    [[nodiscard]] QString lastError() const { return m_lastError; }

    // --- C++ only (tests, adapters) ---------------------------------------
    void setBrowserOpener(BrowserOpener opener) { m_opener = std::move(opener); }
    // Replace the profile's endpoints (tests: a loopback fake server).
    void setEndpoints(const QUrl &authorization, const QUrl &token);
    void setRegistry(const OAuthClientRegistry &registry) { m_registry = registry; }
    void setNetworkAccessManager(QNetworkAccessManager *manager);
    void setTokenTimeoutMs(int ms) { m_tokenTimeoutMs = ms; }
    // Write-only secrets.
    void setClientSecret(const QString &secret) { m_clientSecret = secret; }
    void setRefreshToken(const QString &token);
    // Replaces the profile's scopes for one resource (account oauth.scopes).
    void setResourceScopes(const QString &resource, const QStringList &scopes);
    [[nodiscard]] QString accessToken(const QString &resource = QStringLiteral("mail")) const;
    [[nodiscard]] QDateTime expiresAt(const QString &resource) const;

    // --- QML ----------------------------------------------------------------
    // From an account map: provider, client (registry), tenant, login hint,
    // account id, resources and scope override. {ok, error, source}.
    Q_INVOKABLE QVariantMap configure(const QVariantMap &account);
    Q_INVOKABLE void authorize(const QString &requestId);
    // Always asks the token endpoint (refresh-token grant) for `resource`.
    Q_INVOKABLE void refresh(const QString &requestId, const QString &resource = QStringLiteral("mail"));
    // Answers from memory while the token is valid, else refresh(); loads
    // the refresh token from the keyring first when none is in memory.
    Q_INVOKABLE void ensureToken(const QString &requestId, const QString &resource = QStringLiteral("mail"));
    // Ends a running authorize() with state rejected, cancelled true.
    Q_INVOKABLE void cancel();
    // Forgets every token in memory (the keyring keeps the refresh token;
    // removing the account removes it).
    Q_INVOKABLE void signOut();
    Q_INVOKABLE bool isTokenValid(const QString &resource = QStringLiteral("mail")) const;
    // Calls target->setSecret(<access token>, mechanism) when the token for
    // `resource` is valid. False otherwise (call ensureToken first).
    Q_INVOKABLE bool applyAccessToken(QObject *target, const QString &mechanism,
                                      const QString &resource = QStringLiteral("mail")) const;

signals:
    void authorizeFinished(const QString &requestId, const QVariantMap &result);
    void tokenFinished(const QString &requestId, const QVariantMap &result);
    void configurationChanged();
    void authorizingChanged();
    void tokensChanged();
    void lastErrorChanged();

private:
    struct AuthFlow;
    struct Token {
        QString value;
        QDateTime expiresAt;
        QString scope;
    };
    struct TokenGrant;

    [[nodiscard]] std::optional<OAuthProfile> profile() const;
    [[nodiscard]] QStringList scopesFor(const QString &resource) const;
    [[nodiscard]] QVariantMap baseResult(const QString &requestId, const char *state, const QString &resource,
                                         const QString &error = {}, bool retryable = false) const;
    void finishAuthorize(const QVariantMap &result);
    void failAuthorize(const char *state, const QString &error, bool retryable, const QString &extraFlag = {});
    LoopbackReceiver::Page onRedirect(const QUrlQuery &query);
    void exchangeCode(const QString &code);
    QPointer<QNetworkReply> postToken(const QList<std::pair<QString, QString>> &form,
                                      std::function<void(const TokenGrant &)> done);
    void storeGrant(const TokenGrant &grant, const QString &resource);
    void persistRefreshToken(std::function<void(bool stored, const QString &error)> done);
    void forgetRefreshToken();
    void startRefresh(const QString &resource);
    void finishRefresh(const QString &resource, const QVariantMap &resultTemplate);
    void setLastError(const QString &error);
    void clearTokens();
    QNetworkAccessManager *manager();

    QString m_provider;
    QString m_clientId;
    QString m_clientSecret;
    QString m_tenant;
    QString m_loginHint;
    QStringList m_resources{QStringLiteral("mail"), QStringLiteral("calendar")};
    QString m_accountId;
    QPointer<MailSecrets> m_secrets;
    int m_timeoutMs = 5 * 60 * 1000;
    int m_tokenTimeoutMs = 30 * 1000;
    QUrl m_authorizationOverride;
    QUrl m_tokenOverride;
    QHash<QString, QStringList> m_scopeOverrides;
    OAuthClientRegistry m_registry;
    BrowserOpener m_opener;
    QNetworkAccessManager *m_manager = nullptr;
    bool m_ownsManager = false;

    QString m_refreshToken;
    QHash<QString, Token> m_tokens;          // resource → access token
    QHash<QString, QStringList> m_waiting;   // resource → request ids of one in-flight refresh
    std::unique_ptr<AuthFlow> m_auth;
    QString m_lastError;
};

} // namespace QindaOffice::Mail
