// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Where an OAuth client ID comes from. QindaOffice ships NONE (D-043; the
// toolkit ships none either, QindaTK D-189). Resolution order:
//   1. the account's own oauth.clientId (+ oauth.tenant),
//   2. $XDG_CONFIG_HOME/qindaoffice/mail/oauth-clients.json   (the user's),
//   3. /usr/share/qindaoffice/oauth-clients.json              (a distributor's).
// File format (docs/mail.md#oauth-client-ids):
//   {"schema": "qindaoffice.mail.oauth-clients/1",
//    "google":    {"clientId": "…apps.googleusercontent.com", "clientSecret": "…"},
//    "microsoft": {"clientId": "<GUID>", "tenant": "common"}}
// A client secret is taken from a file only; an account-level client ID
// borrows the secret of a file entry with the same client ID.

#include <QString>

namespace QindaOffice::Mail {

struct OAuthClient {
    QString provider;
    QString clientId;
    QString clientSecret;   // Google desktop clients only; not confidential
    QString tenant;         // Microsoft; empty = the profile default
    QString source;         // account | user | system
};

class OAuthClientRegistry {
public:
    struct Resolution {
        bool ok = false;
        OAuthClient client;
        QString error;      // shown in the account dialog when !ok
    };

    OAuthClientRegistry();

    [[nodiscard]] static QString defaultUserPath();
    [[nodiscard]] static QString defaultSystemPath() { return QStringLiteral("/usr/share/qindaoffice/oauth-clients.json"); }

    void setUserPath(const QString &path) { m_userPath = path; }
    void setSystemPath(const QString &path) { m_systemPath = path; }
    [[nodiscard]] QString userPath() const { return m_userPath; }
    [[nodiscard]] QString systemPath() const { return m_systemPath; }

    // A file that exists but cannot be parsed stops resolution with an error
    // naming it (a silent fall-through would hide the user's mistake).
    [[nodiscard]] Resolution resolve(const QString &provider, const QString &accountClientId = {},
                                     const QString &accountTenant = {}) const;

private:
    QString m_userPath;
    QString m_systemPath;
};

} // namespace QindaOffice::Mail
