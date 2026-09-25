// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Parser for Thunderbird's autoconfiguration format ("clientConfig" v1.1,
// https://wiki.mozilla.org/Thunderbird:Autoconfiguration:ConfigFileFormat),
// the format of autoconfig.<domain>, /.well-known/autoconfig and the
// Mozilla ISPDB. Only what a mail account needs is read: IMAP/POP3 incoming
// servers and SMTP outgoing servers, in the file's order of preference.

#include "mail_account.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace QindaOffice::Mail {

struct ClientConfigServer {
    QString type;           // imap | pop3 | smtp
    QString host;
    int port = 0;
    QString security;       // tls | starttls | none (from socketType SSL | STARTTLS | plain)
    QString userName;       // placeholders substituted
    QStringList authentication;   // as written: password-cleartext, OAuth2, …

    [[nodiscard]] QVariantMap toVariantMap() const;
    // Our mechanism for the listed methods: "auto" when a password method is
    // offered, "xoauth2" when only OAuth2 is, "" when none is usable.
    [[nodiscard]] QString mechanism() const;
};

struct ClientConfig {
    bool ok = false;
    QString error;
    QString providerId;     // <emailProvider id="…">
    QString displayName;
    QStringList domains;
    QList<ClientConfigServer> incoming;
    QList<ClientConfigServer> outgoing;
    QString documentationUrl;

    // The first usable incoming (IMAP before POP3 only by file order) and
    // outgoing server as a candidate account; unencrypted servers are
    // skipped unless they are loopback (validation would refuse them).
    // `ok` false when no usable incoming or outgoing server exists.
    [[nodiscard]] MailAccount toAccount(const QString &emailAddress, bool *ok = nullptr) const;
};

class ClientConfigParser {
public:
    // Hard cap on the document the parser accepts (bounded fetches already
    // stop at the HTTP layer; this keeps the parser safe on its own).
    static constexpr qsizetype kMaxBytes = 256 * 1024;
    static constexpr int kMaxServers = 32;

    [[nodiscard]] static ClientConfig parse(const QByteArray &xml, const QString &emailAddress);
};

// google | microsoft | "" — which OAuth provider serves a mail host name.
[[nodiscard]] QString oauthProviderForHost(const QString &host);

} // namespace QindaOffice::Mail
