// SPDX-License-Identifier: GPL-3.0-or-later
#include "client_config_parser.h"

#include "net_policy.h"
#include "provider_presets.h"

#include <QXmlStreamReader>

namespace QindaOffice::Mail {
namespace {

QString securityFor(const QString &socketType)
{
    const QString s = socketType.trimmed().toUpper();
    if (s == QLatin1String("SSL") || s == QLatin1String("TLS")) return QStringLiteral("tls");
    if (s == QLatin1String("STARTTLS")) return QStringLiteral("starttls");
    if (s == QLatin1String("PLAIN")) return QStringLiteral("none");
    return {};
}

ClientConfigServer parseServer(QXmlStreamReader &r, const QString &emailAddress)
{
    ClientConfigServer server;
    server.type = r.attributes().value(QLatin1String("type")).toString().toLower();
    while (r.readNextStartElement()) {
        const auto name = r.name();
        if (name == QLatin1String("hostname")) {
            server.host = substituteAddress(r.readElementText().trimmed(), emailAddress).toLower();
        } else if (name == QLatin1String("port")) {
            bool ok = false;
            const int port = r.readElementText().trimmed().toInt(&ok);
            server.port = ok && port > 0 && port < 65536 ? port : 0;
        } else if (name == QLatin1String("socketType")) {
            server.security = securityFor(r.readElementText());
        } else if (name == QLatin1String("username")) {
            server.userName = substituteAddress(r.readElementText().trimmed(), emailAddress);
        } else if (name == QLatin1String("authentication")) {
            const QString method = r.readElementText().trimmed();
            if (!method.isEmpty() && server.authentication.size() < 16) server.authentication.append(method);
        } else {
            r.skipCurrentElement();
        }
    }
    return server;
}

bool usable(const ClientConfigServer &s)
{
    if (s.host.isEmpty() || s.port == 0 || s.security.isEmpty() || s.mechanism().isEmpty()) return false;
    return s.security != QLatin1String("none") || isLoopbackHost(s.host);
}

} // namespace

QVariantMap ClientConfigServer::toVariantMap() const
{
    return {{QStringLiteral("type"), type},         {QStringLiteral("host"), host},
            {QStringLiteral("port"), port},         {QStringLiteral("security"), security},
            {QStringLiteral("userName"), userName}, {QStringLiteral("authentication"), authentication},
            {QStringLiteral("authMechanism"), mechanism()}};
}

QString ClientConfigServer::mechanism() const
{
    bool oauth = false;
    for (const QString &method : authentication) {
        const QString m = method.toLower();
        // "plain" is the pre-1.1 spelling of password-cleartext.
        if (m == QLatin1String("password-cleartext") || m == QLatin1String("plain")) return QStringLiteral("auto");
        if (m == QLatin1String("oauth2")) oauth = true;
    }
    if (authentication.isEmpty()) return QStringLiteral("auto");
    return oauth ? QStringLiteral("xoauth2") : QString();
}

QString oauthProviderForHost(const QString &host)
{
    const QString h = host.toLower();
    if (h == QLatin1String("imap.gmail.com") || h == QLatin1String("smtp.gmail.com")
        || h == QLatin1String("pop.gmail.com") || h.endsWith(QLatin1String(".googlemail.com"))) {
        return QStringLiteral("google");
    }
    if (h == QLatin1String("outlook.office365.com") || h == QLatin1String("smtp.office365.com")
        || h == QLatin1String("smtp-mail.outlook.com") || h == QLatin1String("imap-mail.outlook.com")) {
        return QStringLiteral("microsoft");
    }
    return {};
}

ClientConfig ClientConfigParser::parse(const QByteArray &xml, const QString &emailAddress)
{
    ClientConfig config;
    if (xml.size() > kMaxBytes) {
        config.error = QStringLiteral("The configuration document is larger than %1 bytes.").arg(kMaxBytes);
        return config;
    }
    QXmlStreamReader r(xml);
    bool sawRoot = false;
    bool sawProvider = false;
    if (r.readNextStartElement()) {
        if (r.name() != QLatin1String("clientConfig")) {
            config.error = QStringLiteral("Not a mail configuration document (root <%1>).").arg(r.name().toString());
            return config;
        }
        sawRoot = true;
        while (r.readNextStartElement()) {
            if (r.name() != QLatin1String("emailProvider") || sawProvider) {
                r.skipCurrentElement();
                continue;
            }
            sawProvider = true;
            config.providerId = r.attributes().value(QLatin1String("id")).toString();
            while (r.readNextStartElement()) {
                const auto name = r.name();
                if (name == QLatin1String("domain")) {
                    config.domains.append(r.readElementText().trimmed().toLower());
                } else if (name == QLatin1String("displayName")) {
                    config.displayName = r.readElementText().trimmed();
                } else if (name == QLatin1String("incomingServer") || name == QLatin1String("outgoingServer")) {
                    const bool incoming = name == QLatin1String("incomingServer");
                    const ClientConfigServer server = parseServer(r, emailAddress);
                    QList<ClientConfigServer> &list = incoming ? config.incoming : config.outgoing;
                    const bool known = incoming
                        ? (server.type == QLatin1String("imap") || server.type == QLatin1String("pop3"))
                        : server.type == QLatin1String("smtp");
                    // Other types (exchange, ews, jmap, …) are not ours.
                    if (known && list.size() < kMaxServers) list.append(server);
                } else if (name == QLatin1String("documentation")) {
                    if (config.documentationUrl.isEmpty()) {
                        config.documentationUrl = r.attributes().value(QLatin1String("url")).toString();
                    }
                    r.skipCurrentElement();
                } else {
                    r.skipCurrentElement();
                }
            }
        }
    }
    if (r.hasError()) {
        config.error = QStringLiteral("The configuration document is not well-formed XML: %1").arg(r.errorString());
        return config;
    }
    if (!sawRoot || !sawProvider) {
        config.error = QStringLiteral("The configuration document has no <emailProvider>.");
        return config;
    }
    if (config.incoming.isEmpty()) {
        config.error = QStringLiteral("The configuration lists no IMAP or POP3 server.");
        return config;
    }
    config.ok = true;
    return config;
}

MailAccount ClientConfig::toAccount(const QString &emailAddress, bool *ok) const
{
    const ClientConfigServer *in = nullptr;
    const ClientConfigServer *out = nullptr;
    for (const ClientConfigServer &s : incoming) {
        if (usable(s)) { in = &s; break; }
    }
    for (const ClientConfigServer &s : outgoing) {
        if (usable(s)) { out = &s; break; }
    }
    if (ok) *ok = in != nullptr && out != nullptr;
    if (!in || !out) return {};

    // Hosted Google/Microsoft domains discovered here (e.g. an ISPDB entry
    // for a Google Workspace domain) get the full preset: kind, OAuth,
    // calendar and folders.
    const QString oauth = oauthProviderForHost(in->host);
    MailAccount account;
    if (!oauth.isEmpty()) {
        const QString presetId = oauth == QLatin1String("google") ? QStringLiteral("gmail") : QStringLiteral("microsoft365");
        account = ProviderPresets::accountFor(*ProviderPresets::forId(presetId), emailAddress);
    } else {
        account.kind = in->type == QLatin1String("pop3") ? QString::fromLatin1(kKindPop3) : QString::fromLatin1(kKindImap);
        account.emailAddress = emailAddress.trimmed();
        account.displayName = account.emailAddress;
        account.incoming.authMechanism = in->mechanism();
        account.outgoing.authMechanism = out->mechanism();
    }
    account.incoming.protocol = in->type;
    account.incoming.host = in->host;
    account.incoming.port = in->port;
    account.incoming.security = in->security;
    account.incoming.userName = in->userName.isEmpty() ? account.emailAddress : in->userName;
    account.outgoing.host = out->host;
    account.outgoing.port = out->port;
    account.outgoing.security = out->security;
    account.outgoing.userName = out->userName.isEmpty() ? account.incoming.userName : out->userName;
    account.outgoing.sameCredentialsAsIncoming = account.outgoing.userName == account.incoming.userName;
    return account;
}

} // namespace QindaOffice::Mail
