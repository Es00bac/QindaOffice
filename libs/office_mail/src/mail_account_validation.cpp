// SPDX-License-Identifier: GPL-3.0-or-later
// MailAccount::validate(): every rule an account must satisfy before
// AccountStore persists it. Each failure is one line "field.path: reason"
// the account dialog can show next to the field.

#include "mail_account.h"
#include "net_policy.h"

#include <QRegularExpression>
#include <QSet>
#include <QUrl>

namespace QindaOffice::Mail {
namespace {

bool oneOf(const QString &value, std::initializer_list<const char *> allowed)
{
    for (const char *a : allowed) {
        if (value == QLatin1String(a)) return true;
    }
    return false;
}

bool isAscii(const QString &s)
{
    for (const QChar c : s) {
        if (c.unicode() < 0x20 || c.unicode() > 0x7e) return false;
    }
    return true;
}

QStringList checkServer(const QString &path, const QString &host, int port, const QString &security,
                        const QString &mechanism, const QString &oauthProvider)
{
    QStringList e;
    if (host.isEmpty()) {
        e << path + QStringLiteral(".host: a server name is required");
    } else if (host.contains(QLatin1Char(' ')) || host.contains(QLatin1Char('/')) || !isAscii(host)) {
        e << path + QStringLiteral(".host: “%1” is not a server name").arg(host);
    }
    if (port < 1 || port > 65535) e << path + QStringLiteral(".port: must be 1–65535");
    if (!oneOf(security, {"tls", "starttls", "none"})) {
        e << path + QStringLiteral(".security: must be tls, starttls or none");
    } else if (security == QLatin1String("none") && !host.isEmpty() && !isLoopbackHost(host)) {
        // AGENT-GUARD: D-187/D-043 — credentials never cross the network in
        // clear text. Loopback stays allowed for local bridges (Proton).
        e << path + QStringLiteral(".security: an unencrypted connection is only allowed to this computer "
                                   "(a local bridge); use tls or starttls");
    }
    if (!oneOf(mechanism, {"auto", "plain", "login", "xoauth2"})) {
        e << path + QStringLiteral(".authMechanism: must be auto, plain, login or xoauth2");
    } else if (mechanism == QLatin1String("xoauth2") && oauthProvider.isEmpty()) {
        e << path + QStringLiteral(".authMechanism: xoauth2 needs oauth.provider (google or microsoft)");
    }
    return e;
}

bool calendarUrlOk(const QString &protocol, const QString &text, QString *why)
{
    QUrl url(text);
    const QString scheme = url.scheme().toLower();
    if (protocol == QLatin1String("ics")) {
        if (scheme == QLatin1String("file") && !url.toLocalFile().isEmpty()) return true;
        if (scheme == QLatin1String("webcal")) url.setScheme(QStringLiteral("https"));
    }
    return isSecureOrLoopbackUrl(url, why);
}

} // namespace

QStringList MailAccount::validate() const
{
    QStringList e;
    static const QRegularExpression idPattern(QStringLiteral("^[A-Za-z0-9._-]{1,128}$"));
    if (!idPattern.match(id).hasMatch()) {
        e << QStringLiteral("id: must be 1–128 characters of A–Z, a–z, 0–9, '.', '_' or '-'");
    }
    if (!accountKinds().contains(kind)) {
        e << QStringLiteral("kind: “%1” is not one of %2").arg(kind, accountKinds().join(QStringLiteral(", ")));
        return e; // every other rule depends on the kind
    }
    if (displayName.trimmed().isEmpty()) e << QStringLiteral("displayName: a name for the account is required");

    const bool isGmail = kind == QLatin1String(kKindGmail);
    const bool isM365 = kind == QLatin1String(kKindMicrosoft365);
    const bool isExchange = kind == QLatin1String(kKindExchange);
    const bool mailRequired = isGmail || isM365 || kind == QLatin1String(kKindImap)
        || kind == QLatin1String(kKindPop3);

    // Address: ASCII only, because the toolkit's SMTP/IMAP have no SMTPUTF8.
    static const QRegularExpression address(QStringLiteral("^[^@\\s]+@[^@\\s]+\\.[^@\\s]+$"));
    if (!emailAddress.isEmpty()) {
        if (!address.match(emailAddress).hasMatch()) {
            e << QStringLiteral("emailAddress: “%1” is not an e-mail address").arg(emailAddress);
        } else if (!isAscii(emailAddress)) {
            e << QStringLiteral("emailAddress: only ASCII addresses are supported");
        }
    } else if (mailRequired) {
        e << QStringLiteral("emailAddress: an e-mail address is required");
    }

    // OAuth provider and the kinds that need it.
    if (!oneOf(oauth.provider, {"", "google", "microsoft"})) {
        e << QStringLiteral("oauth.provider: must be google, microsoft or empty");
    }
    if (isM365 && oauth.provider != QLatin1String("microsoft")) {
        e << QStringLiteral("oauth.provider: Microsoft 365 and Outlook.com accounts sign in with microsoft");
    }
    if (isGmail && !oauth.provider.isEmpty() && oauth.provider != QLatin1String("google")) {
        e << QStringLiteral("oauth.provider: a Gmail account signs in with google (or an app password)");
    }
    static const QRegularExpression tenantPattern(QStringLiteral("^[A-Za-z0-9.-]{1,128}$"));
    if (!oauth.tenant.isEmpty() && !tenantPattern.match(oauth.tenant).hasMatch()) {
        e << QStringLiteral("oauth.tenant: must be common, organizations, consumers, a tenant id or domain");
    }

    // Mail servers.
    const bool validateMail = mailRequired || (isExchange && (!incoming.isEmpty() || !outgoing.isEmpty()));
    if (validateMail) {
        const QString wantProtocol = kind == QLatin1String(kKindPop3) ? QStringLiteral("pop3") : QStringLiteral("imap");
        if (!incoming.isEmpty() || mailRequired) {
            if (incoming.protocol != wantProtocol) {
                e << QStringLiteral("incoming.protocol: a %1 account reads mail over %2").arg(kind, wantProtocol);
            }
            e += checkServer(QStringLiteral("incoming"), incoming.host, incoming.port, incoming.security,
                             incoming.authMechanism, oauth.provider);
            if (incoming.userName.isEmpty()) e << QStringLiteral("incoming.userName: a user name is required");
            if (incoming.deleteAfterDays < 0 || incoming.deleteAfterDays > 3650) {
                e << QStringLiteral("incoming.deleteAfterDays: must be 0 (never) to 3650");
            }
            if (isM365 && incoming.authMechanism != QLatin1String("xoauth2")) {
                e << QStringLiteral("incoming.authMechanism: Microsoft accepts only OAuth (xoauth2) for IMAP");
            }
        }
        if (!outgoing.isEmpty() || mailRequired) {
            e += checkServer(QStringLiteral("outgoing"), outgoing.host, outgoing.port, outgoing.security,
                             outgoing.authMechanism, oauth.provider);
            if (!outgoing.sameCredentialsAsIncoming && outgoing.userName.isEmpty()) {
                e << QStringLiteral("outgoing.userName: a user name is required");
            }
            if (isM365 && outgoing.authMechanism != QLatin1String("xoauth2")) {
                e << QStringLiteral("outgoing.authMechanism: Microsoft accepts only OAuth (xoauth2) for SMTP");
            }
        }
    } else if (!incoming.isEmpty() || !outgoing.isEmpty()) {
        e << QStringLiteral("incoming: a %1 account has no mail servers").arg(kind);
    }

    // Calendar: which protocols each kind may use (the "Account kinds" table).
    QStringList allowed;
    if (isGmail || kind == QLatin1String(kKindImap)) allowed = {QStringLiteral("caldav"), QStringLiteral("none")};
    else if (kind == QLatin1String(kKindPop3)) allowed = {QStringLiteral("none")};
    else if (kind == QLatin1String(kKindCalDav)) allowed = {QStringLiteral("caldav")};
    else if (kind == QLatin1String(kKindIcs)) allowed = {QStringLiteral("ics")};
    else if (isExchange) allowed = {QStringLiteral("ews")};
    else if (isM365) allowed = {QStringLiteral("graph"), QStringLiteral("none")};
    if (!allowed.contains(calendar.protocol)) {
        e << QStringLiteral("calendar.protocol: a %1 account uses %2").arg(kind, allowed.join(QStringLiteral(" or ")));
    }
    if (calendar.protocol != QLatin1String("none")) {
        const bool urlNeeded = calendar.protocol != QLatin1String("graph");
        QString why;
        if (calendar.url.isEmpty()) {
            if (urlNeeded) e << QStringLiteral("calendar.url: a calendar address is required");
        } else if (!calendarUrlOk(calendar.protocol, calendar.url, &why)) {
            e << QStringLiteral("calendar.url: ") + why;
        }
        if (!oneOf(calendar.authMechanism, {"basic", "bearer"})) {
            e << QStringLiteral("calendar.authMechanism: must be basic or bearer");
        } else if (calendar.authMechanism == QLatin1String("bearer") && oauth.provider.isEmpty()) {
            e << QStringLiteral("calendar.authMechanism: bearer needs oauth.provider (google or microsoft)");
        }
        if (calendar.protocol == QLatin1String("graph") && calendar.authMechanism != QLatin1String("bearer")) {
            e << QStringLiteral("calendar.authMechanism: Microsoft Graph needs bearer");
        }
        QSet<QString> seen;
        static const QRegularExpression colorPattern(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
        for (qsizetype i = 0; i < calendar.calendars.size(); ++i) {
            const CalendarEntry &c = calendar.calendars.at(i);
            const QString path = QStringLiteral("calendar.calendars[%1]").arg(i);
            if (c.id.isEmpty()) e << path + QStringLiteral(".id: required");
            else if (seen.contains(c.id)) e << path + QStringLiteral(".id: “%1” is used twice").arg(c.id);
            seen.insert(c.id);
            if (!c.color.isEmpty() && !colorPattern.match(c.color).hasMatch()) {
                e << path + QStringLiteral(".color: must be #rrggbb");
            }
            QString entryWhy;
            if (!c.url.isEmpty() && !c.url.startsWith(QLatin1Char('/'))
                && !calendarUrlOk(calendar.protocol, c.url, &entryWhy)) {
                e << path + QStringLiteral(".url: ") + entryWhy;
            }
        }
    }

    if (syncIntervalMinutes < 0 || syncIntervalMinutes > 1440) {
        e << QStringLiteral("syncIntervalMinutes: must be 0 (manual) to 1440");
    }
    if (signature.size() > 64 * 1024) e << QStringLiteral("signature: longer than 64 KiB");

    for (const QString &key : secretLookingKeys(toJson())) {
        e << key + QStringLiteral(": secrets are never stored in the account file; "
                                  "passwords and tokens go to the keyring (MailSecrets)");
    }
    return e;
}

} // namespace QindaOffice::Mail
