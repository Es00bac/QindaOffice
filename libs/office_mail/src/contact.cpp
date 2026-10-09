// SPDX-License-Identifier: GPL-3.0-or-later
#include "contact.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QUrl>

namespace QindaOffice::Mail {

using namespace Qt::StringLiterals;

namespace {

// Every key toJson() owns; contactFromJson() strips them from jsonExtra so
// that only genuinely unknown keys are carried along.
const QList<QLatin1StringView> &knownJsonKeys()
{
    static const QList<QLatin1StringView> keys = {
        "id"_L1, "uid"_L1, "displayName"_L1, "prefix"_L1, "givenName"_L1, "middleName"_L1,
        "familyName"_L1, "suffix"_L1, "nickname"_L1, "emails"_L1, "phones"_L1, "urls"_L1,
        "addresses"_L1, "organization"_L1, "department"_L1, "title"_L1, "birthday"_L1,
        "anniversary"_L1, "notes"_L1, "categories"_L1, "photoFile"_L1, "photoMimeType"_L1,
        "photoUri"_L1, "revision"_L1, "extra"_L1, "kind"_L1, "members"_L1, "sync"_L1,
    };
    return keys;
}

QString jsonString(const QJsonObject &o, QLatin1StringView key)
{
    const QJsonValue v = o.value(key);
    return v.isString() ? v.toString() : QString();
}

void putString(QJsonObject &o, QLatin1StringView key, const QString &value)
{
    if (value.isEmpty()) o.remove(key);
    else o.insert(key, value);
}

QJsonArray valuesToJson(const QList<ContactValue> &values)
{
    QJsonArray out;
    for (const ContactValue &v : values) {
        QJsonObject o{{u"value"_s, v.value}};
        if (!v.type.isEmpty()) o.insert(u"type"_s, v.type);
        if (v.preferred) o.insert(u"preferred"_s, true);
        out.append(o);
    }
    return out;
}

QList<ContactValue> valuesFromJson(const QJsonValue &json)
{
    QList<ContactValue> out;
    for (const QJsonValue &item : json.toArray()) {
        if (out.size() >= ContactLimits::kMaxMultiValues) break;
        ContactValue v;
        if (item.isString()) {
            v.value = item.toString();
        } else {
            const QJsonObject o = item.toObject();
            v.type = jsonString(o, "type"_L1);
            v.value = jsonString(o, "value"_L1);
            v.preferred = o.value("preferred"_L1).toBool();
        }
        if (!v.value.isEmpty()) out.append(v);
    }
    return out;
}

// The address fields in one table so JSON, QML maps and equality agree.
struct AddressField {
    QLatin1StringView key;
    QString ContactAddress::*member;
};
const AddressField kAddressFields[] = {
    {"type"_L1, &ContactAddress::type},       {"poBox"_L1, &ContactAddress::poBox},
    {"extended"_L1, &ContactAddress::extended}, {"street"_L1, &ContactAddress::street},
    {"locality"_L1, &ContactAddress::locality}, {"region"_L1, &ContactAddress::region},
    {"postalCode"_L1, &ContactAddress::postalCode}, {"country"_L1, &ContactAddress::country},
    {"label"_L1, &ContactAddress::label},
};

QVariantList valuesToVariant(const QList<ContactValue> &values)
{
    QVariantList out;
    for (const ContactValue &v : values) {
        out.append(QVariantMap{{u"type"_s, v.type}, {u"value"_s, v.value}, {u"preferred"_s, v.preferred}});
    }
    return out;
}

QList<ContactValue> valuesFromVariant(const QVariant &variant)
{
    QList<ContactValue> out;
    for (const QVariant &item : variant.toList()) {
        if (out.size() >= ContactLimits::kMaxMultiValues) break;
        ContactValue v;
        const QVariantMap map = item.toMap();
        if (map.isEmpty()) {
            v.value = item.toString();
        } else {
            v.type = map.value(u"type"_s).toString();
            v.value = map.value(u"value"_s).toString();
            v.preferred = map.value(u"preferred"_s).toBool();
        }
        out.append(v);
    }
    return out;
}

QList<ContactAddress> addressesFromVariant(const QVariant &variant)
{
    QList<ContactAddress> out;
    for (const QVariant &item : variant.toList()) {
        if (out.size() >= ContactLimits::kMaxMultiValues) break;
        const QVariantMap map = item.toMap();
        ContactAddress a;
        for (const AddressField &f : kAddressFields) a.*f.member = map.value(QString(f.key)).toString();
        a.preferred = map.value(u"preferred"_s).toBool();
        out.append(a);
    }
    return out;
}

QList<QPair<QString, QString>> paramsFromVariant(const QVariant &variant)
{
    QList<QPair<QString, QString>> out;
    for (const QVariant &item : variant.toList()) {
        const QVariantMap map = item.toMap();
        if (!map.isEmpty()) {
            out.append({map.value(u"name"_s).toString(), map.value(u"value"_s).toString()});
        } else if (const QVariantList pair = item.toList(); pair.size() == 2) {
            out.append({pair.at(0).toString(), pair.at(1).toString()});
        }
    }
    return out;
}

QList<VCardProperty> extrasFromVariant(const QVariant &variant)
{
    QList<VCardProperty> out;
    for (const QVariant &item : variant.toList()) {
        if (out.size() >= ContactLimits::kMaxExtraProperties) break;
        const QVariantMap map = item.toMap();
        VCardProperty p;
        p.group = map.value(u"group"_s).toString();
        p.name = map.value(u"name"_s).toString();
        p.params = paramsFromVariant(map.value(u"params"_s));
        p.value = map.value(u"value"_s).toString();
        if (!p.name.isEmpty()) out.append(p);
    }
    return out;
}

QVariantList membersToVariant(const QList<ContactMember> &members)
{
    QVariantList out;
    for (const ContactMember &m : members)
        out.append(QVariantMap{{u"uri"_s, m.uri}, {u"name"_s, m.name}, {u"address"_s, m.address()}});
    return out;
}

// QML gives {uri, name} or {address, name} (ContactStore turns {contactId}
// into a uri before this runs); a plain string is a uri or an address.
QList<ContactMember> membersFromVariant(const QVariant &variant)
{
    QList<ContactMember> out;
    for (const QVariant &item : variant.toList()) {
        if (out.size() >= ContactLimits::kMaxGroupMembers) break;
        const QVariantMap map = item.toMap();
        ContactMember m;
        if (map.isEmpty()) {
            const QString text = item.toString().trimmed();
            const qsizetype open = text.lastIndexOf(u'<');
            if (open >= 0 && text.endsWith(u'>')) {
                QString name = text.left(open).trimmed();
                if (name.size() >= 2 && name.startsWith(u'"') && name.endsWith(u'"')) name = name.mid(1, name.size() - 2);
                m = ContactMember::forAddress(text.mid(open + 1, text.size() - open - 2), name);
            } else {
                m = text.contains(u':') ? ContactMember{text, {}} : ContactMember::forAddress(text);
            }
        } else if (!map.value(u"uri"_s).toString().isEmpty()) {
            m = {map.value(u"uri"_s).toString(), map.value(u"name"_s).toString()};
        } else {
            m = ContactMember::forAddress(map.value(u"address"_s).toString(), map.value(u"name"_s).toString());
        }
        if (!m.uri.isEmpty()) out.append(m);
    }
    return out;
}

QStringList stringListFromVariant(const QVariant &variant)
{
    if (variant.typeId() == QMetaType::QString) {
        return variant.toString().split(u',', Qt::SkipEmptyParts);
    }
    return variant.toStringList();
}

} // namespace

bool ContactMember::isAddress() const
{
    return uri.startsWith("mailto:"_L1, Qt::CaseInsensitive);
}

QString ContactMember::address() const
{
    return isAddress() ? QUrl::fromPercentEncoding(uri.mid(7).toUtf8()).trimmed() : QString();
}

ContactMember ContactMember::forAddress(const QString &address, const QString &name)
{
    QString trimmed = address.trimmed();
    if (trimmed.isEmpty()) return {};
    trimmed.replace(u'%', "%25"_L1);   // RFC 6068: '%' starts an escape
    return {u"mailto:"_s + trimmed, name.trimmed()};
}

ContactMember ContactMember::forCard(const QString &uid)
{
    const QString trimmed = uid.trimmed();
    if (trimmed.isEmpty()) return {};
    return {trimmed.contains(u':') ? trimmed : u"urn:uuid:"_s + trimmed, {}};
}

bool ContactAddress::isEmpty() const
{
    for (const AddressField &f : kAddressFields) {
        if (f.member != &ContactAddress::type && !(this->*f.member).isEmpty()) return false;
    }
    return true;
}

QString Contact::effectiveDisplayName() const
{
    if (!displayName.trimmed().isEmpty()) return displayName.trimmed();
    QStringList parts;
    for (const QString &part : {givenName.trimmed(), familyName.trimmed()}) {
        if (!part.isEmpty()) parts.append(part);
    }
    if (!parts.isEmpty()) return parts.join(u' ');
    for (const QString &field : {middleName, nickname, organization}) {
        if (!field.trimmed().isEmpty()) return field.trimmed();
    }
    return primaryEmail();
}

QString Contact::primaryEmail() const
{
    for (const ContactValue &e : emails) {
        if (e.preferred && !e.value.isEmpty()) return e.value;
    }
    for (const ContactValue &e : emails) {
        if (!e.value.isEmpty()) return e.value;
    }
    return {};
}

bool Contact::matches(const QString &query) const
{
    return searchKeyMatches(contactSearchKey(*this), searchWords(query));
}

QVariantMap toVariantMap(const Contact &c)
{
    QVariantList addresses;
    for (const ContactAddress &a : c.addresses) {
        QVariantMap map;
        for (const AddressField &f : kAddressFields) map.insert(QString(f.key), a.*f.member);
        map.insert(u"preferred"_s, a.preferred);
        addresses.append(map);
    }
    QVariantList extras;
    for (const VCardProperty &p : c.extra) {
        QVariantList params;
        for (const auto &param : p.params) {
            params.append(QVariantMap{{u"name"_s, param.first}, {u"value"_s, param.second}});
        }
        extras.append(QVariantMap{{u"group"_s, p.group}, {u"name"_s, p.name},
                                  {u"params"_s, params}, {u"value"_s, p.value}});
    }
    return {
        {u"id"_s, c.id}, {u"uid"_s, c.uid},
        {u"displayName"_s, c.effectiveDisplayName()}, {u"formattedName"_s, c.displayName},
        {u"prefix"_s, c.prefix}, {u"givenName"_s, c.givenName}, {u"middleName"_s, c.middleName},
        {u"familyName"_s, c.familyName}, {u"suffix"_s, c.suffix}, {u"nickname"_s, c.nickname},
        {u"emails"_s, valuesToVariant(c.emails)}, {u"phones"_s, valuesToVariant(c.phones)},
        {u"urls"_s, valuesToVariant(c.urls)}, {u"addresses"_s, addresses},
        {u"organization"_s, c.organization}, {u"department"_s, c.department}, {u"title"_s, c.title},
        {u"birthday"_s, c.birthday}, {u"anniversary"_s, c.anniversary}, {u"notes"_s, c.notes},
        {u"categories"_s, c.categories}, {u"hasPhoto"_s, c.hasPhoto()},
        {u"photoMimeType"_s, c.photoMimeType}, {u"photoRemoteUri"_s, c.photoUrl},
        {u"revision"_s, c.revision}, {u"primaryEmail"_s, c.primaryEmail()}, {u"extra"_s, extras},
        {u"kind"_s, c.kind}, {u"isGroup"_s, c.isGroup()}, {u"members"_s, membersToVariant(c.members)},
        {u"memberCount"_s, int(c.members.size())}, {u"syncAccountId"_s, c.sync.account},
    };
}

void applyVariantMap(Contact &c, const QVariantMap &map)
{
    const QList<QPair<QLatin1StringView, QString Contact::*>> texts = {
        {"id"_L1, &Contact::id}, {"uid"_L1, &Contact::uid}, {"displayName"_L1, &Contact::displayName},
        {"prefix"_L1, &Contact::prefix}, {"givenName"_L1, &Contact::givenName},
        {"middleName"_L1, &Contact::middleName}, {"familyName"_L1, &Contact::familyName},
        {"suffix"_L1, &Contact::suffix}, {"nickname"_L1, &Contact::nickname},
        {"organization"_L1, &Contact::organization}, {"department"_L1, &Contact::department},
        {"title"_L1, &Contact::title}, {"birthday"_L1, &Contact::birthday},
        {"anniversary"_L1, &Contact::anniversary}, {"notes"_L1, &Contact::notes},
        {"photoRemoteUri"_L1, &Contact::photoUrl}, {"revision"_L1, &Contact::revision},
        {"kind"_L1, &Contact::kind},
    };
    for (const auto &[key, member] : texts) {
        const auto it = map.constFind(QString(key));
        if (it != map.cend()) c.*member = it->toString();
    }
    if (map.contains(u"members"_s)) c.members = membersFromVariant(map.value(u"members"_s));
    auto has = [&map](const char *key) { return map.contains(QString::fromLatin1(key)); };
    if (has("emails")) c.emails = valuesFromVariant(map.value(u"emails"_s));
    if (has("phones")) c.phones = valuesFromVariant(map.value(u"phones"_s));
    if (has("urls")) c.urls = valuesFromVariant(map.value(u"urls"_s));
    if (has("addresses")) c.addresses = addressesFromVariant(map.value(u"addresses"_s));
    if (has("categories")) c.categories = stringListFromVariant(map.value(u"categories"_s));
    if (has("extra")) c.extra = extrasFromVariant(map.value(u"extra"_s));
}

Contact contactFromVariantMap(const QVariantMap &map)
{
    Contact c;
    applyVariantMap(c, map);
    return c;
}

QJsonObject toJson(const Contact &c)
{
    QJsonObject o = c.jsonExtra;
    o.insert("id"_L1, c.id);
    const QList<QPair<QLatin1StringView, QString>> texts = {
        {"uid"_L1, c.uid}, {"displayName"_L1, c.displayName}, {"prefix"_L1, c.prefix},
        {"givenName"_L1, c.givenName}, {"middleName"_L1, c.middleName}, {"familyName"_L1, c.familyName},
        {"suffix"_L1, c.suffix}, {"nickname"_L1, c.nickname}, {"organization"_L1, c.organization},
        {"department"_L1, c.department}, {"title"_L1, c.title}, {"birthday"_L1, c.birthday},
        {"anniversary"_L1, c.anniversary}, {"notes"_L1, c.notes}, {"photoFile"_L1, c.photoFile},
        {"photoMimeType"_L1, c.photoFile.isEmpty() ? QString() : c.photoMimeType},
        {"photoUri"_L1, c.photoUrl}, {"revision"_L1, c.revision}, {"kind"_L1, c.kind},
    };
    for (const auto &[key, value] : texts) putString(o, key, value);
    auto putArray = [&o](QLatin1StringView key, const QJsonArray &array) {
        if (array.isEmpty()) o.remove(key);
        else o.insert(key, array);
    };
    putArray("emails"_L1, valuesToJson(c.emails));
    putArray("phones"_L1, valuesToJson(c.phones));
    putArray("urls"_L1, valuesToJson(c.urls));
    QJsonArray addresses;
    for (const ContactAddress &a : c.addresses) {
        QJsonObject ao;
        for (const AddressField &f : kAddressFields) putString(ao, f.key, a.*f.member);
        if (a.preferred) ao.insert(u"preferred"_s, true);
        addresses.append(ao);
    }
    putArray("addresses"_L1, addresses);
    putArray("categories"_L1, QJsonArray::fromStringList(c.categories));
    QJsonArray extras;
    for (const VCardProperty &p : c.extra) {
        QJsonArray params;
        for (const auto &param : p.params) params.append(QJsonArray{param.first, param.second});
        QJsonObject po{{u"name"_s, p.name}, {u"value"_s, p.value}};
        if (!p.group.isEmpty()) po.insert(u"group"_s, p.group);
        if (!params.isEmpty()) po.insert(u"params"_s, params);
        extras.append(po);
    }
    putArray("extra"_L1, extras);
    QJsonArray members;
    for (const ContactMember &m : c.members) {
        QJsonObject mo{{u"uri"_s, m.uri}};
        if (!m.name.isEmpty()) mo.insert(u"name"_s, m.name);
        members.append(mo);
    }
    putArray("members"_L1, members);
    if (c.sync.isEmpty()) {
        o.remove("sync"_L1);
    } else {
        QJsonObject so;
        putString(so, "account"_L1, c.sync.account);
        putString(so, "book"_L1, c.sync.book);
        putString(so, "href"_L1, c.sync.href);
        putString(so, "etag"_L1, c.sync.etag);
        putString(so, "digest"_L1, c.sync.digest);
        o.insert("sync"_L1, so);
    }
    return o;
}

Contact contactFromJson(const QJsonObject &o)
{
    Contact c;
    c.jsonExtra = o;
    for (QLatin1StringView key : knownJsonKeys()) c.jsonExtra.remove(key);
    const QList<QPair<QLatin1StringView, QString Contact::*>> texts = {
        {"id"_L1, &Contact::id}, {"uid"_L1, &Contact::uid}, {"displayName"_L1, &Contact::displayName},
        {"prefix"_L1, &Contact::prefix}, {"givenName"_L1, &Contact::givenName},
        {"middleName"_L1, &Contact::middleName}, {"familyName"_L1, &Contact::familyName},
        {"suffix"_L1, &Contact::suffix}, {"nickname"_L1, &Contact::nickname},
        {"organization"_L1, &Contact::organization}, {"department"_L1, &Contact::department},
        {"title"_L1, &Contact::title}, {"birthday"_L1, &Contact::birthday},
        {"anniversary"_L1, &Contact::anniversary}, {"notes"_L1, &Contact::notes},
        {"photoFile"_L1, &Contact::photoFile}, {"photoMimeType"_L1, &Contact::photoMimeType},
        {"photoUri"_L1, &Contact::photoUrl}, {"revision"_L1, &Contact::revision},
        {"kind"_L1, &Contact::kind},
    };
    for (const auto &[key, member] : texts) c.*member = jsonString(o, key);
    for (const QJsonValue &item : o.value("members"_L1).toArray()) {
        if (c.members.size() >= ContactLimits::kMaxGroupMembers) break;
        const QJsonObject mo = item.toObject();
        const ContactMember m{jsonString(mo, "uri"_L1), jsonString(mo, "name"_L1)};
        if (!m.uri.isEmpty()) c.members.append(m);
    }
    const QJsonObject so = o.value("sync"_L1).toObject();
    c.sync = {jsonString(so, "account"_L1), jsonString(so, "book"_L1), jsonString(so, "href"_L1),
              jsonString(so, "etag"_L1), jsonString(so, "digest"_L1)};
    c.emails = valuesFromJson(o.value("emails"_L1));
    c.phones = valuesFromJson(o.value("phones"_L1));
    c.urls = valuesFromJson(o.value("urls"_L1));
    for (const QJsonValue &item : o.value("addresses"_L1).toArray()) {
        if (c.addresses.size() >= ContactLimits::kMaxMultiValues) break;
        const QJsonObject ao = item.toObject();
        ContactAddress a;
        for (const AddressField &f : kAddressFields) a.*f.member = jsonString(ao, f.key);
        a.preferred = ao.value("preferred"_L1).toBool();
        c.addresses.append(a);
    }
    for (const QJsonValue &item : o.value("categories"_L1).toArray()) {
        if (item.isString() && c.categories.size() < ContactLimits::kMaxMultiValues) c.categories.append(item.toString());
    }
    for (const QJsonValue &item : o.value("extra"_L1).toArray()) {
        if (c.extra.size() >= ContactLimits::kMaxExtraProperties) break;
        const QJsonObject po = item.toObject();
        VCardProperty p;
        p.group = jsonString(po, "group"_L1);
        p.name = jsonString(po, "name"_L1);
        p.value = jsonString(po, "value"_L1);
        for (const QJsonValue &param : po.value("params"_L1).toArray()) {
            const QJsonArray pair = param.toArray();
            if (pair.size() == 2) p.params.append({pair.at(0).toString(), pair.at(1).toString()});
        }
        if (!p.name.isEmpty()) c.extra.append(p);
    }
    return c;
}

} // namespace QindaOffice::Mail
