// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Endpoint and scope profiles of the two OAuth 2.0 providers the hosted
// account kinds sign in with.
//
// Resources: "mail" (IMAP + SMTP), "calendar" (CalDAV for Google,
// Microsoft Graph for Microsoft), Microsoft only "ews" (Exchange Online
// Web Services) and "mailbox" (Graph mailbox settings and mail, M81) and,
// Google only, "contacts" (CardDAV, M76). Google issues one token that covers every
// granted scope; Microsoft issues one token PER RESOURCE, so the Graph token
// is a second refresh-token grant with the calendar scopes
// (OAuth2Session::ensureToken(…, "calendar")).

#include <QList>
#include <QMap>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <optional>

namespace QindaOffice::Mail {

struct OAuthProfile {
    QString provider;               // google | microsoft
    QString name;                   // for messages
    QString authorizationEndpoint;  // may contain %TENANT%
    QString tokenEndpoint;          // may contain %TENANT%
    QString defaultTenant;          // Microsoft: common
    QMap<QString, QStringList> resourceScopes;
    QList<QPair<QString, QString>> extraAuthorizeParameters;
    bool oneTokenForAllResources = false;

    [[nodiscard]] static std::optional<OAuthProfile> forProvider(const QString &provider);
    [[nodiscard]] static QStringList providers() { return {QStringLiteral("google"), QStringLiteral("microsoft")}; }

    [[nodiscard]] QUrl authorizationUrl(const QString &tenant) const;
    [[nodiscard]] QUrl tokenUrl(const QString &tenant) const;
    // Every scope of `resources`, de-duplicated, in order: what the
    // authorization request asks the user to consent to.
    [[nodiscard]] QStringList consentScopes(const QStringList &resources) const;
};

} // namespace QindaOffice::Mail
