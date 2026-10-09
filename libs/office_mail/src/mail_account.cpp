// SPDX-License-Identifier: GPL-3.0-or-later
#include "mail_account.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QUuid>

namespace QindaOffice::Mail {
namespace {

QString str(const QJsonObject &o, const char *key, const QString &fallback = {})
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isString() ? v.toString() : fallback;
}

int integer(const QJsonObject &o, const char *key, int fallback)
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isDouble() ? v.toInt(fallback) : fallback;
}

bool boolean(const QJsonObject &o, const char *key, bool fallback)
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isBool() ? v.toBool() : fallback;
}

QJsonObject obj(const QJsonObject &o, const char *key)
{
    return o.value(QLatin1String(key)).toObject();
}

IncomingServer incomingFrom(const QJsonObject &o)
{
    IncomingServer s;
    s.extra = o;
    s.protocol = str(o, "protocol");
    s.host = str(o, "host");
    s.port = integer(o, "port", 0);
    s.security = str(o, "security", s.security);
    s.userName = str(o, "userName");
    s.authMechanism = str(o, "authMechanism", s.authMechanism);
    s.leaveOnServer = boolean(o, "leaveOnServer", s.leaveOnServer);
    s.deleteAfterDays = integer(o, "deleteAfterDays", 0);
    return s;
}

QJsonObject incomingTo(const IncomingServer &s)
{
    QJsonObject o = s.extra;
    o.insert(QStringLiteral("protocol"), s.protocol);
    o.insert(QStringLiteral("host"), s.host);
    o.insert(QStringLiteral("port"), s.port);
    o.insert(QStringLiteral("security"), s.security);
    o.insert(QStringLiteral("userName"), s.userName);
    o.insert(QStringLiteral("authMechanism"), s.authMechanism);
    o.insert(QStringLiteral("leaveOnServer"), s.leaveOnServer);
    o.insert(QStringLiteral("deleteAfterDays"), s.deleteAfterDays);
    return o;
}

OutgoingServer outgoingFrom(const QJsonObject &o)
{
    OutgoingServer s;
    s.extra = o;
    s.host = str(o, "host");
    s.port = integer(o, "port", 0);
    s.security = str(o, "security", s.security);
    s.userName = str(o, "userName");
    s.authMechanism = str(o, "authMechanism", s.authMechanism);
    s.sameCredentialsAsIncoming = boolean(o, "sameCredentialsAsIncoming", true);
    return s;
}

QJsonObject outgoingTo(const OutgoingServer &s)
{
    QJsonObject o = s.extra;
    o.insert(QStringLiteral("host"), s.host);
    o.insert(QStringLiteral("port"), s.port);
    o.insert(QStringLiteral("security"), s.security);
    o.insert(QStringLiteral("userName"), s.userName);
    o.insert(QStringLiteral("authMechanism"), s.authMechanism);
    o.insert(QStringLiteral("sameCredentialsAsIncoming"), s.sameCredentialsAsIncoming);
    return o;
}

CalendarSettings calendarFrom(const QJsonObject &o)
{
    CalendarSettings c;
    c.extra = o;
    c.protocol = str(o, "protocol", c.protocol);
    c.url = str(o, "url");
    c.userName = str(o, "userName");
    c.authMechanism = str(o, "authMechanism", c.authMechanism);
    const QJsonArray list = o.value(QLatin1String("calendars")).toArray();
    for (const QJsonValue &value : list) {
        const QJsonObject e = value.toObject();
        CalendarEntry entry;
        entry.extra = e;
        entry.id = str(e, "id");
        entry.url = str(e, "url");
        entry.name = str(e, "name");
        entry.color = str(e, "color");
        entry.enabled = boolean(e, "enabled", true);
        c.calendars.append(entry);
    }
    return c;
}

QJsonObject calendarTo(const CalendarSettings &c)
{
    QJsonObject o = c.extra;
    o.insert(QStringLiteral("protocol"), c.protocol);
    o.insert(QStringLiteral("url"), c.url);
    o.insert(QStringLiteral("userName"), c.userName);
    o.insert(QStringLiteral("authMechanism"), c.authMechanism);
    QJsonArray list;
    for (const CalendarEntry &entry : c.calendars) {
        QJsonObject e = entry.extra;
        e.insert(QStringLiteral("id"), entry.id);
        e.insert(QStringLiteral("url"), entry.url);
        e.insert(QStringLiteral("name"), entry.name);
        e.insert(QStringLiteral("color"), entry.color);
        e.insert(QStringLiteral("enabled"), entry.enabled);
        list.append(e);
    }
    o.insert(QStringLiteral("calendars"), list);
    return o;
}

ContactsSettings contactsFrom(const QJsonObject &o)
{
    ContactsSettings c;
    c.extra = o;
    c.protocol = str(o, "protocol", c.protocol);
    c.url = str(o, "url");
    c.userName = str(o, "userName");
    c.authMechanism = str(o, "authMechanism", c.authMechanism);
    for (const QJsonValue &value : o.value(QLatin1String("addressBooks")).toArray()) {
        const QJsonObject e = value.toObject();
        AddressBookEntry entry;
        entry.extra = e;
        entry.id = str(e, "id");
        entry.url = str(e, "url");
        entry.name = str(e, "name");
        entry.enabled = boolean(e, "enabled", true);
        c.addressBooks.append(entry);
    }
    return c;
}

QJsonObject contactsTo(const ContactsSettings &c)
{
    QJsonObject o = c.extra;
    o.insert(QStringLiteral("protocol"), c.protocol);
    o.insert(QStringLiteral("url"), c.url);
    o.insert(QStringLiteral("userName"), c.userName);
    o.insert(QStringLiteral("authMechanism"), c.authMechanism);
    QJsonArray list;
    for (const AddressBookEntry &entry : c.addressBooks) {
        QJsonObject e = entry.extra;
        e.insert(QStringLiteral("id"), entry.id);
        e.insert(QStringLiteral("url"), entry.url);
        e.insert(QStringLiteral("name"), entry.name);
        e.insert(QStringLiteral("enabled"), entry.enabled);
        list.append(e);
    }
    o.insert(QStringLiteral("addressBooks"), list);
    return o;
}

OAuthSettings oauthFrom(const QJsonObject &o)
{
    OAuthSettings s;
    s.extra = o;
    s.provider = str(o, "provider");
    s.clientId = str(o, "clientId");
    s.tenant = str(o, "tenant");
    const QJsonValue scopes = o.value(QLatin1String("scopes"));
    if (scopes.isArray()) {
        for (const QJsonValue &v : scopes.toArray()) {
            if (v.isString()) s.scopes.append(v.toString());
        }
    } else if (scopes.isString()) {
        s.scopes = scopes.toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    }
    return s;
}

QJsonObject oauthTo(const OAuthSettings &s)
{
    QJsonObject o = s.extra;
    o.insert(QStringLiteral("provider"), s.provider);
    o.insert(QStringLiteral("clientId"), s.clientId);
    o.insert(QStringLiteral("tenant"), s.tenant);
    o.insert(QStringLiteral("scopes"), QJsonArray::fromStringList(s.scopes));
    return o;
}

SpecialFolders foldersFrom(const QJsonObject &o)
{
    SpecialFolders f;
    f.extra = o;
    f.sent = str(o, "sent");
    f.drafts = str(o, "drafts");
    f.trash = str(o, "trash");
    f.archive = str(o, "archive");
    f.junk = str(o, "junk");
    return f;
}

QJsonObject foldersTo(const SpecialFolders &f)
{
    QJsonObject o = f.extra;
    o.insert(QStringLiteral("sent"), f.sent);
    o.insert(QStringLiteral("drafts"), f.drafts);
    o.insert(QStringLiteral("trash"), f.trash);
    o.insert(QStringLiteral("archive"), f.archive);
    o.insert(QStringLiteral("junk"), f.junk);
    return o;
}

bool isSecretKey(const QString &key)
{
    // Matched case-insensitively and ignoring '_'/'-', so "refresh_token",
    // "RefreshToken" and "app-password" are all caught.
    QString k = key.toLower();
    k.remove(QLatin1Char('_'));
    k.remove(QLatin1Char('-'));
    static const QStringList forbidden = {
        QStringLiteral("password"), QStringLiteral("passwd"), QStringLiteral("secret"),
        QStringLiteral("token"), QStringLiteral("accesstoken"), QStringLiteral("refreshtoken"),
        QStringLiteral("idtoken"), QStringLiteral("clientsecret"), QStringLiteral("apppassword"),
        QStringLiteral("apikey"), QStringLiteral("bearer"),
    };
    return forbidden.contains(k);
}

} // namespace

QStringList accountKinds()
{
    return {QString::fromLatin1(kKindGmail), QString::fromLatin1(kKindImap),
            QString::fromLatin1(kKindPop3), QString::fromLatin1(kKindCalDav),
            QString::fromLatin1(kKindIcs), QString::fromLatin1(kKindExchange),
            QString::fromLatin1(kKindMicrosoft365), QString::fromLatin1(kKindCardDav)};
}

QString MailAccount::generateId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool MailAccount::hasMail() const
{
    if (kind == QLatin1String(kKindCalDav) || kind == QLatin1String(kKindIcs) || kind == QLatin1String(kKindCardDav))
        return false;
    if (kind == QLatin1String(kKindExchange)) return !incoming.isEmpty();
    return true;
}

MailAccount MailAccount::fromJson(const QJsonObject &o)
{
    MailAccount a;
    a.extra = o;
    a.id = str(o, "id");
    a.kind = str(o, "kind", a.kind);
    a.displayName = str(o, "displayName");
    a.emailAddress = str(o, "emailAddress");
    a.fullName = str(o, "fullName");
    a.incoming = incomingFrom(obj(o, "incoming"));
    a.outgoing = outgoingFrom(obj(o, "outgoing"));
    a.calendar = calendarFrom(obj(o, "calendar"));
    a.contacts = contactsFrom(obj(o, "contacts"));
    a.oauth = oauthFrom(obj(o, "oauth"));
    a.signature = str(o, "signature");
    a.syncIntervalMinutes = integer(o, "syncIntervalMinutes", a.syncIntervalMinutes);
    a.enabled = boolean(o, "enabled", true);
    a.specialFolders = foldersFrom(obj(o, "specialFolders"));
    return a;
}

QJsonObject MailAccount::toJson() const
{
    QJsonObject o = extra;
    o.insert(QStringLiteral("id"), id);
    o.insert(QStringLiteral("kind"), kind);
    o.insert(QStringLiteral("displayName"), displayName);
    o.insert(QStringLiteral("emailAddress"), emailAddress);
    o.insert(QStringLiteral("fullName"), fullName);
    o.insert(QStringLiteral("incoming"), incomingTo(incoming));
    o.insert(QStringLiteral("outgoing"), outgoingTo(outgoing));
    o.insert(QStringLiteral("calendar"), calendarTo(calendar));
    // M76: written once an account has (or had) address books, so older
    // files round-trip byte for byte.
    if (contacts.protocol != QLatin1String("none") || o.contains(QLatin1String("contacts")))
        o.insert(QStringLiteral("contacts"), contactsTo(contacts));
    o.insert(QStringLiteral("oauth"), oauthTo(oauth));
    o.insert(QStringLiteral("signature"), signature);
    o.insert(QStringLiteral("syncIntervalMinutes"), syncIntervalMinutes);
    o.insert(QStringLiteral("enabled"), enabled);
    o.insert(QStringLiteral("specialFolders"), foldersTo(specialFolders));
    return o;
}

MailAccount MailAccount::fromVariantMap(const QVariantMap &map)
{
    return fromJson(QJsonObject::fromVariantMap(map));
}

QVariantMap MailAccount::toVariantMap() const
{
    return toJson().toVariantMap();
}

QStringList secretLookingKeys(const QJsonObject &object, const QString &path)
{
    QStringList found;
    for (auto it = object.begin(); it != object.end(); ++it) {
        const QString here = path.isEmpty() ? it.key() : path + QLatin1Char('.') + it.key();
        if (isSecretKey(it.key())) found.append(here);
        if (it.value().isObject()) {
            found += secretLookingKeys(it.value().toObject(), here);
        } else if (it.value().isArray()) {
            const QJsonArray array = it.value().toArray();
            for (qsizetype i = 0; i < array.size(); ++i) {
                if (array.at(i).isObject()) {
                    found += secretLookingKeys(array.at(i).toObject(),
                                               here + QLatin1Char('[') + QString::number(i) + QLatin1Char(']'));
                }
            }
        }
    }
    return found;
}

} // namespace QindaOffice::Mail
