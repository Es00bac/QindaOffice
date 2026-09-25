// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Built-in server settings for the hosted mail services people actually use,
// keyed by the address's domain. The first step of AutoConfig, and the
// source of the account dialog's hints ("iCloud needs an app password").
//
// AGENT-NOTE: the values are the providers' published client settings as of
// 2026-09 (docs/mail.md#presets carries the same table). They are data, not
// a promise: AutoConfig still lets the user edit every field. Adding a
// provider = one entry in provider_presets.cpp + one docs row + a test row.

#include "mail_account.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <optional>

namespace QindaOffice::Mail {

struct ServerPreset {
    QString host;
    int port = 0;
    QString security;   // tls | starttls | none
};

struct ProviderPreset {
    QString id;                 // stable: gmail, outlook, microsoft365, yahoo, icloud, …
    QString name;               // shown in the setup UI
    QStringList domains;        // lower-case; empty for "choose me by kind" presets
    QString kind;               // the MailAccount kind it implies
    QString incomingProtocol;   // imap | pop3
    ServerPreset incoming;
    ServerPreset outgoing;
    QString calendarProtocol = QStringLiteral("none");  // caldav | graph | none
    QString calendarUrl;        // may contain %EMAILADDRESS%
    QString oauthProvider;      // google | microsoft | ""
    // How the user signs in:
    //   oauth-required  — OAuth only (Microsoft)
    //   oauth-preferred — OAuth, or an app password for mail (Gmail)
    //   app-password    — a generated app-specific password (iCloud, Yahoo, …)
    //   password        — the account password
    //   bridge          — the password a local bridge shows (Proton)
    QString auth;
    QString note;               // one or two sentences for the setup UI
    SpecialFolders folders;

    [[nodiscard]] QVariantMap toVariantMap() const;
};

class ProviderPresets {
public:
    [[nodiscard]] static const QList<ProviderPreset> &all();
    [[nodiscard]] static std::optional<ProviderPreset> forId(const QString &id);
    // Case-insensitive; exact domain match (no subdomain wildcards).
    [[nodiscard]] static std::optional<ProviderPreset> forDomain(const QString &domain);
    [[nodiscard]] static std::optional<ProviderPreset> forAddress(const QString &emailAddress);

    // A complete candidate account for `emailAddress` from `preset`: servers,
    // user names, calendar, OAuth provider, folders. The caller still sets
    // the id (AccountStore does) and may change anything.
    [[nodiscard]] static MailAccount accountFor(const ProviderPreset &preset, const QString &emailAddress,
                                                const QString &fullName = {});
};

// "%EMAILADDRESS%", "%EMAILLOCALPART%", "%EMAILDOMAIN%" substitution, shared
// with the Thunderbird clientConfig parser.
[[nodiscard]] QString substituteAddress(QString text, const QString &emailAddress);
[[nodiscard]] QString domainOf(const QString &emailAddress);

} // namespace QindaOffice::Mail
