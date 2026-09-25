// SPDX-License-Identifier: GPL-3.0-or-later
#include "provider_presets.h"

namespace QindaOffice::Mail {
namespace {

using S = ServerPreset;

SpecialFolders folders(const char *sent, const char *drafts, const char *trash, const char *archive,
                       const char *junk)
{
    SpecialFolders f;
    f.sent = QString::fromUtf8(sent);
    f.drafts = QString::fromUtf8(drafts);
    f.trash = QString::fromUtf8(trash);
    f.archive = QString::fromUtf8(archive);
    f.junk = QString::fromUtf8(junk);
    return f;
}

QList<ProviderPreset> build()
{
    const QString tls = QStringLiteral("tls");
    const QString starttls = QStringLiteral("starttls");
    const QString imap = QStringLiteral("imap");
    QList<ProviderPreset> list;

    // AGENT-NOTE: folder names are the providers' English IMAP names. They
    // are a starting point: once the IMAP lane's folder listing (QindaTK
    // D-299) is available, SPECIAL-USE flags (RFC 6154) should win, which
    // also covers localized names such as "[Google Mail]/…".
    ProviderPreset p;
    p.id = QStringLiteral("gmail");
    p.name = QStringLiteral("Gmail");
    p.domains = {QStringLiteral("gmail.com"), QStringLiteral("googlemail.com")};
    p.kind = QString::fromLatin1(kKindGmail);
    p.incomingProtocol = imap;
    p.incoming = S{QStringLiteral("imap.gmail.com"), 993, tls};
    p.outgoing = S{QStringLiteral("smtp.gmail.com"), 465, tls};
    p.calendarProtocol = QStringLiteral("caldav");
    p.calendarUrl = QStringLiteral("https://apidata.googleusercontent.com/caldav/v2/%EMAILADDRESS%/events/");
    p.oauthProvider = QStringLiteral("google");
    p.auth = QStringLiteral("oauth-preferred");
    p.note = QStringLiteral("Sign in with Google for mail and calendar. Without an OAuth client, an app "
                            "password (Google Account ▸ Security ▸ 2-Step Verification ▸ App passwords) "
                            "works for mail only; Google Calendar needs Google sign-in.");
    p.folders = folders("[Gmail]/Sent Mail", "[Gmail]/Drafts", "[Gmail]/Trash", "[Gmail]/All Mail", "[Gmail]/Spam");
    list << p;

    p = ProviderPreset();
    p.id = QStringLiteral("outlook");
    p.name = QStringLiteral("Outlook.com");
    p.domains = {QStringLiteral("outlook.com"), QStringLiteral("hotmail.com"), QStringLiteral("live.com"),
                 QStringLiteral("msn.com")};
    p.kind = QString::fromLatin1(kKindMicrosoft365);
    p.incomingProtocol = imap;
    p.incoming = S{QStringLiteral("outlook.office365.com"), 993, tls};
    p.outgoing = S{QStringLiteral("smtp-mail.outlook.com"), 587, starttls};
    p.calendarProtocol = QStringLiteral("graph");
    p.oauthProvider = QStringLiteral("microsoft");
    p.auth = QStringLiteral("oauth-required");
    p.note = QStringLiteral("Microsoft accepts only Microsoft sign-in (OAuth) for Outlook.com mail; passwords "
                            "and app passwords are refused. The calendar uses Microsoft Graph.");
    p.folders = folders("Sent", "Drafts", "Deleted", "Archive", "Junk");
    list << p;

    p = ProviderPreset();
    p.id = QStringLiteral("microsoft365");
    p.name = QStringLiteral("Microsoft 365");
    p.kind = QString::fromLatin1(kKindMicrosoft365);
    p.incomingProtocol = imap;
    p.incoming = S{QStringLiteral("outlook.office365.com"), 993, tls};
    p.outgoing = S{QStringLiteral("smtp.office365.com"), 587, starttls};
    p.calendarProtocol = QStringLiteral("graph");
    p.oauthProvider = QStringLiteral("microsoft");
    p.auth = QStringLiteral("oauth-required");
    p.note = QStringLiteral("Work or school accounts sign in with Microsoft (OAuth). Your organization may "
                            "have to allow IMAP, SMTP AUTH and the app's client ID.");
    p.folders = folders("Sent Items", "Drafts", "Deleted Items", "Archive", "Junk Email");
    list << p;

    p = ProviderPreset();
    p.id = QStringLiteral("yahoo");
    p.name = QStringLiteral("Yahoo Mail");
    p.domains = {QStringLiteral("yahoo.com"), QStringLiteral("ymail.com"), QStringLiteral("rocketmail.com")};
    p.kind = QString::fromLatin1(kKindImap);
    p.incomingProtocol = imap;
    p.incoming = S{QStringLiteral("imap.mail.yahoo.com"), 993, tls};
    p.outgoing = S{QStringLiteral("smtp.mail.yahoo.com"), 465, tls};
    p.auth = QStringLiteral("app-password");
    p.note = QStringLiteral("Yahoo needs an app password (Account security ▸ Generate app password); "
                            "your normal password is refused.");
    list << p;

    p = ProviderPreset();
    p.id = QStringLiteral("icloud");
    p.name = QStringLiteral("iCloud Mail");
    p.domains = {QStringLiteral("icloud.com"), QStringLiteral("me.com"), QStringLiteral("mac.com")};
    p.kind = QString::fromLatin1(kKindImap);
    p.incomingProtocol = imap;
    p.incoming = S{QStringLiteral("imap.mail.me.com"), 993, tls};
    p.outgoing = S{QStringLiteral("smtp.mail.me.com"), 587, starttls};
    p.calendarProtocol = QStringLiteral("caldav");
    p.calendarUrl = QStringLiteral("https://caldav.icloud.com/");
    p.auth = QStringLiteral("app-password");
    p.note = QStringLiteral("iCloud needs an app-specific password (account.apple.com ▸ Sign-In and Security "
                            "▸ App-Specific Passwords). The same password opens iCloud Calendar.");
    p.folders = folders("Sent Messages", "Drafts", "Deleted Messages", "Archive", "Junk");
    list << p;

    p = ProviderPreset();
    p.id = QStringLiteral("fastmail");
    p.name = QStringLiteral("Fastmail");
    p.domains = {QStringLiteral("fastmail.com"), QStringLiteral("fastmail.fm")};
    p.kind = QString::fromLatin1(kKindImap);
    p.incomingProtocol = imap;
    p.incoming = S{QStringLiteral("imap.fastmail.com"), 993, tls};
    p.outgoing = S{QStringLiteral("smtp.fastmail.com"), 465, tls};
    p.calendarProtocol = QStringLiteral("caldav");
    p.calendarUrl = QStringLiteral("https://caldav.fastmail.com/dav/calendars/user/%EMAILADDRESS%/");
    p.auth = QStringLiteral("app-password");
    p.note = QStringLiteral("Fastmail needs an app password (Settings ▸ Privacy & Security ▸ Integrations) "
                            "with IMAP, SMTP and CalDAV access.");
    p.folders = folders("Sent", "Drafts", "Trash", "Archive", "Spam");
    list << p;

    p = ProviderPreset();
    p.id = QStringLiteral("aol");
    p.name = QStringLiteral("AOL Mail");
    p.domains = {QStringLiteral("aol.com"), QStringLiteral("aim.com")};
    p.kind = QString::fromLatin1(kKindImap);
    p.incomingProtocol = imap;
    p.incoming = S{QStringLiteral("imap.aol.com"), 993, tls};
    p.outgoing = S{QStringLiteral("smtp.aol.com"), 465, tls};
    p.auth = QStringLiteral("app-password");
    p.note = QStringLiteral("AOL needs an app password (Account security ▸ Generate app password).");
    list << p;

    p = ProviderPreset();
    p.id = QStringLiteral("zoho");
    p.name = QStringLiteral("Zoho Mail");
    p.domains = {QStringLiteral("zoho.com"), QStringLiteral("zohomail.com")};
    p.kind = QString::fromLatin1(kKindImap);
    p.incomingProtocol = imap;
    p.incoming = S{QStringLiteral("imap.zoho.com"), 993, tls};
    p.outgoing = S{QStringLiteral("smtp.zoho.com"), 465, tls};
    p.auth = QStringLiteral("password");
    p.note = QStringLiteral("Turn on IMAP access in Zoho Mail settings first. With two-factor sign-in, use "
                            "an application-specific password. Accounts in the EU data centre use "
                            "imap.zoho.eu and smtp.zoho.eu.");
    list << p;

    p = ProviderPreset();
    p.id = QStringLiteral("gmx-com");
    p.name = QStringLiteral("GMX");
    p.domains = {QStringLiteral("gmx.com")};
    p.kind = QString::fromLatin1(kKindImap);
    p.incomingProtocol = imap;
    p.incoming = S{QStringLiteral("imap.gmx.com"), 993, tls};
    p.outgoing = S{QStringLiteral("mail.gmx.com"), 587, starttls};
    p.auth = QStringLiteral("password");
    p.note = QStringLiteral("Allow POP3/IMAP access in the GMX settings (E-mail ▸ POP3/IMAP) first.");
    list << p;

    p.id = QStringLiteral("gmx-net");
    p.domains = {QStringLiteral("gmx.net"), QStringLiteral("gmx.de"), QStringLiteral("gmx.at"),
                 QStringLiteral("gmx.ch")};
    p.incoming = S{QStringLiteral("imap.gmx.net"), 993, tls};
    p.outgoing = S{QStringLiteral("mail.gmx.net"), 587, starttls};
    list << p;

    p = ProviderPreset();
    p.id = QStringLiteral("mailbox-org");
    p.name = QStringLiteral("mailbox.org");
    p.domains = {QStringLiteral("mailbox.org")};
    p.kind = QString::fromLatin1(kKindImap);
    p.incomingProtocol = imap;
    p.incoming = S{QStringLiteral("imap.mailbox.org"), 993, tls};
    p.outgoing = S{QStringLiteral("smtp.mailbox.org"), 465, tls};
    p.auth = QStringLiteral("password");
    p.note = QStringLiteral("Use your mailbox.org password, or an app password if two-factor sign-in is on.");
    list << p;

    p = ProviderPreset();
    p.id = QStringLiteral("proton");
    p.name = QStringLiteral("Proton Mail (Bridge)");
    p.domains = {QStringLiteral("proton.me"), QStringLiteral("protonmail.com"), QStringLiteral("protonmail.ch"),
                 QStringLiteral("pm.me")};
    p.kind = QString::fromLatin1(kKindImap);
    p.incomingProtocol = imap;
    p.incoming = S{QStringLiteral("127.0.0.1"), 1143, starttls};
    p.outgoing = S{QStringLiteral("127.0.0.1"), 1025, starttls};
    p.auth = QStringLiteral("bridge");
    p.note = QStringLiteral("Proton Mail works only through Proton Mail Bridge running on this computer (a paid "
                            "Proton plan). Use the user name and password the Bridge shows, not your Proton "
                            "password. The Bridge's certificate is self-signed: trust it in the Bridge settings.");
    list << p;
    return list;
}

} // namespace

QVariantMap ProviderPreset::toVariantMap() const
{
    const auto server = [](const ServerPreset &s) {
        return QVariantMap{{QStringLiteral("host"), s.host}, {QStringLiteral("port"), s.port},
                           {QStringLiteral("security"), s.security}};
    };
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), name},
        {QStringLiteral("domains"), domains},
        {QStringLiteral("kind"), kind},
        {QStringLiteral("incomingProtocol"), incomingProtocol},
        {QStringLiteral("incoming"), server(incoming)},
        {QStringLiteral("outgoing"), server(outgoing)},
        {QStringLiteral("calendarProtocol"), calendarProtocol},
        {QStringLiteral("calendarUrl"), calendarUrl},
        {QStringLiteral("oauthProvider"), oauthProvider},
        {QStringLiteral("auth"), auth},
        {QStringLiteral("note"), note},
    };
}

const QList<ProviderPreset> &ProviderPresets::all()
{
    static const QList<ProviderPreset> presets = build();
    return presets;
}

std::optional<ProviderPreset> ProviderPresets::forId(const QString &id)
{
    for (const ProviderPreset &p : all()) {
        if (p.id == id) return p;
    }
    return std::nullopt;
}

std::optional<ProviderPreset> ProviderPresets::forDomain(const QString &domain)
{
    const QString d = domain.trimmed().toLower();
    if (d.isEmpty()) return std::nullopt;
    for (const ProviderPreset &p : all()) {
        if (p.domains.contains(d)) return p;
    }
    return std::nullopt;
}

std::optional<ProviderPreset> ProviderPresets::forAddress(const QString &emailAddress)
{
    return forDomain(domainOf(emailAddress));
}

MailAccount ProviderPresets::accountFor(const ProviderPreset &preset, const QString &emailAddress,
                                        const QString &fullName)
{
    MailAccount a;
    a.kind = preset.kind;
    a.emailAddress = emailAddress.trimmed();
    a.displayName = a.emailAddress;
    a.fullName = fullName;
    a.incoming.protocol = preset.incomingProtocol;
    a.incoming.host = preset.incoming.host;
    a.incoming.port = preset.incoming.port;
    a.incoming.security = preset.incoming.security;
    a.incoming.userName = a.emailAddress;
    a.outgoing.host = preset.outgoing.host;
    a.outgoing.port = preset.outgoing.port;
    a.outgoing.security = preset.outgoing.security;
    a.outgoing.userName = a.emailAddress;
    a.outgoing.sameCredentialsAsIncoming = true;
    a.oauth.provider = preset.oauthProvider;
    // OAuth presets sign in with a token; the app-password path for Gmail
    // is the dialog switching both mechanisms back to "auto".
    const QString mechanism = preset.oauthProvider.isEmpty() ? QStringLiteral("auto") : QStringLiteral("xoauth2");
    a.incoming.authMechanism = mechanism;
    a.outgoing.authMechanism = mechanism;
    a.calendar.protocol = preset.calendarProtocol;
    a.calendar.url = substituteAddress(preset.calendarUrl, a.emailAddress);
    a.calendar.userName = preset.calendarProtocol == QLatin1String("caldav") ? a.emailAddress : QString();
    a.calendar.authMechanism = preset.oauthProvider.isEmpty() ? QStringLiteral("basic") : QStringLiteral("bearer");
    a.specialFolders = preset.folders;
    return a;
}

QString domainOf(const QString &emailAddress)
{
    const qsizetype at = emailAddress.lastIndexOf(QLatin1Char('@'));
    return at < 0 ? QString() : emailAddress.mid(at + 1).trimmed().toLower();
}

QString substituteAddress(QString text, const QString &emailAddress)
{
    const QString address = emailAddress.trimmed();
    const qsizetype at = address.lastIndexOf(QLatin1Char('@'));
    const QString local = at < 0 ? address : address.left(at);
    text.replace(QLatin1String("%EMAILADDRESS%"), address);
    text.replace(QLatin1String("%EMAILLOCALPART%"), local);
    text.replace(QLatin1String("%EMAILDOMAIN%"), domainOf(address));
    return text;
}

} // namespace QindaOffice::Mail
