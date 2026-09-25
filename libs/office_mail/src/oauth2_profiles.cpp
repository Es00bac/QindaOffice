// SPDX-License-Identifier: GPL-3.0-or-later
#include "oauth2_profiles.h"

namespace QindaOffice::Mail {

std::optional<OAuthProfile> OAuthProfile::forProvider(const QString &provider)
{
    OAuthProfile p;
    if (provider == QLatin1String("google")) {
        // AGENT-NOTE: Google "Desktop app" clients use the loopback redirect
        // and carry a client secret that Google itself documents as not
        // confidential; the token endpoint requires it even with PKCE.
        // access_type=offline + prompt=consent make Google return a refresh
        // token on every consent, not only the first.
        p.provider = provider;
        p.name = QStringLiteral("Google");
        p.authorizationEndpoint = QStringLiteral("https://accounts.google.com/o/oauth2/v2/auth");
        p.tokenEndpoint = QStringLiteral("https://oauth2.googleapis.com/token");
        const QStringList scopes = {QStringLiteral("https://mail.google.com/"),
                                    QStringLiteral("https://www.googleapis.com/auth/calendar")};
        p.resourceScopes.insert(QStringLiteral("mail"), scopes);
        p.resourceScopes.insert(QStringLiteral("calendar"), scopes);
        p.extraAuthorizeParameters = {{QStringLiteral("access_type"), QStringLiteral("offline")},
                                      {QStringLiteral("prompt"), QStringLiteral("consent")}};
        p.oneTokenForAllResources = true;
        return p;
    }
    if (provider == QLatin1String("microsoft")) {
        // AGENT-NOTE: Microsoft identity platform v2. A token is issued for
        // ONE resource: outlook.office.com (IMAP/SMTP) or Graph. The
        // authorization request consents to both scope sets; the code
        // redemption asks for the mail set, and the Graph token comes from
        // a refresh-token grant with the calendar set. offline_access is in
        // both so every grant returns a (rotated) refresh token.
        p.provider = provider;
        p.name = QStringLiteral("Microsoft");
        p.authorizationEndpoint = QStringLiteral("https://login.microsoftonline.com/%TENANT%/oauth2/v2.0/authorize");
        p.tokenEndpoint = QStringLiteral("https://login.microsoftonline.com/%TENANT%/oauth2/v2.0/token");
        p.defaultTenant = QStringLiteral("common");
        p.resourceScopes.insert(QStringLiteral("mail"),
                                {QStringLiteral("offline_access"),
                                 QStringLiteral("https://outlook.office.com/IMAP.AccessAsUser.All"),
                                 QStringLiteral("https://outlook.office.com/SMTP.Send")});
        p.resourceScopes.insert(QStringLiteral("calendar"),
                                {QStringLiteral("offline_access"), QStringLiteral("Calendars.ReadWrite")});
        // Exchange Online over EWS with a bearer token (the exchange adapter).
        p.resourceScopes.insert(QStringLiteral("ews"),
                                {QStringLiteral("offline_access"),
                                 QStringLiteral("https://outlook.office365.com/EWS.AccessAsUser.All")});
        return p;
    }
    return std::nullopt;
}

QUrl OAuthProfile::authorizationUrl(const QString &tenant) const
{
    QString text = authorizationEndpoint;
    text.replace(QLatin1String("%TENANT%"), tenant.isEmpty() ? defaultTenant : tenant);
    return QUrl(text);
}

QUrl OAuthProfile::tokenUrl(const QString &tenant) const
{
    QString text = tokenEndpoint;
    text.replace(QLatin1String("%TENANT%"), tenant.isEmpty() ? defaultTenant : tenant);
    return QUrl(text);
}

QStringList OAuthProfile::consentScopes(const QStringList &resources) const
{
    QStringList scopes;
    for (const QString &resource : resources) {
        for (const QString &scope : resourceScopes.value(resource)) {
            if (!scopes.contains(scope)) scopes.append(scope);
        }
    }
    return scopes;
}

} // namespace QindaOffice::Mail
