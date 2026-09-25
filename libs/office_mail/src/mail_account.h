// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// One mail/calendar account as configuration: servers, sign-in method,
// calendar collection and folders. NEVER a password, token or other secret
// (those live in the desktop keyring through MailSecrets, D-043).
//
// AGENT-CONTRACT: the JSON shape here is the on-disk schema
// `qindaoffice.mail.accounts/1` (docs/mail.md#accounts-json) and the
// QVariantMap shape QML sees through AccountStore. Both sides: AccountStore
// (persistence, QML) and the app lane's account dialog. Every struct keeps
// the JSON object it was read from in `extra`, and toJson() writes the known
// keys OVER that object, so fields a newer build (or a hand edit) added
// survive a load/save round trip.

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace QindaOffice::Mail {

// Account kinds (docs/plans/ribbon-and-mail.md, "Account kinds").
inline constexpr const char *kKindGmail = "gmail";
inline constexpr const char *kKindImap = "imap";
inline constexpr const char *kKindPop3 = "pop3";
inline constexpr const char *kKindCalDav = "caldav";
inline constexpr const char *kKindIcs = "ics";
inline constexpr const char *kKindExchange = "exchange";
inline constexpr const char *kKindMicrosoft365 = "microsoft365";

[[nodiscard]] QStringList accountKinds();

struct IncomingServer {
    QString protocol;           // imap | pop3 ("" when the kind has no mail)
    QString host;
    int port = 0;
    QString security = QStringLiteral("tls");       // tls | starttls | none
    QString userName;
    QString authMechanism = QStringLiteral("auto"); // auto | plain | login | xoauth2
    bool leaveOnServer = true;  // POP3 only
    int deleteAfterDays = 0;    // POP3 only; 0 = never
    QJsonObject extra;

    [[nodiscard]] bool isEmpty() const { return protocol.isEmpty() && host.isEmpty(); }
};

struct OutgoingServer {
    QString host;
    int port = 0;
    QString security = QStringLiteral("starttls");
    QString userName;
    QString authMechanism = QStringLiteral("auto");
    bool sameCredentialsAsIncoming = true;
    QJsonObject extra;

    [[nodiscard]] bool isEmpty() const { return host.isEmpty(); }
};

struct CalendarEntry {
    QString id;
    QString url;
    QString name;
    QString color;      // "#rrggbb" or empty
    bool enabled = true;
    QJsonObject extra;
};

struct CalendarSettings {
    QString protocol = QStringLiteral("none");       // caldav | ics | ews | graph | none
    QString url;
    QString userName;
    QString authMechanism = QStringLiteral("basic"); // basic | bearer
    QList<CalendarEntry> calendars;
    QJsonObject extra;
};

struct OAuthSettings {
    QString provider;   // google | microsoft | ""
    QString clientId;   // optional override; see OAuthClientRegistry
    QString tenant;     // Microsoft only: common | organizations | consumers | <tenant id>
    QStringList scopes; // optional override of the mail scope set
    QJsonObject extra;
};

struct SpecialFolders {
    QString sent;
    QString drafts;
    QString trash;
    QString archive;
    QString junk;
    QJsonObject extra;
};

struct MailAccount {
    QString id;
    QString kind = QString::fromLatin1(kKindImap);
    QString displayName;
    QString emailAddress;
    QString fullName;
    IncomingServer incoming;
    OutgoingServer outgoing;
    CalendarSettings calendar;
    OAuthSettings oauth;
    QString signature;
    int syncIntervalMinutes = 15;   // 0 = manual only
    bool enabled = true;
    SpecialFolders specialFolders;
    QJsonObject extra;

    // A fresh stable id (UUID without braces). Ids are never reused.
    [[nodiscard]] static QString generateId();

    [[nodiscard]] static MailAccount fromJson(const QJsonObject &object);
    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static MailAccount fromVariantMap(const QVariantMap &map);
    [[nodiscard]] QVariantMap toVariantMap() const;

    // Every problem, one human-readable line each, prefixed with the field
    // path ("incoming.host: …"). Empty means valid. Implemented in
    // mail_account_validation.cpp.
    [[nodiscard]] QStringList validate() const;

    // Kind-dependent capabilities (the "Account kinds" table).
    [[nodiscard]] bool hasMail() const;
    [[nodiscard]] bool hasCalendar() const { return calendar.protocol != QLatin1String("none"); }
};

// Keys that name a secret. They are refused anywhere in an account object,
// so a host that hands a password to AccountStore by mistake gets an error
// instead of a plaintext password in accounts.json.
// AGENT-GUARD: unknown-field preservation would otherwise write such a key
// straight to disk. Keep this list in step with docs/mail.md#secrets.
[[nodiscard]] QStringList secretLookingKeys(const QJsonObject &object, const QString &path = {});

} // namespace QindaOffice::Mail
