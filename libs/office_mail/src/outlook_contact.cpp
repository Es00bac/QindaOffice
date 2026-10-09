// SPDX-License-Identifier: GPL-3.0-or-later
// Outlook contacts (IPM.Contact) and contact groups (IPM.DistList) →
// Contact (outlook_items.h). Property ids: MS-OXOCNTC and MS-OXPROPS.

#include "outlook_items.h"

#include "mapi_mail.h"
#include "mapi_named.h"
#include "msg_headers.h"

#include <algorithm>

namespace QindaOffice::Mail::OutlookDetail {
namespace {

using MsgDetail::oneLine;

enum : quint16 {
    PidSubject = 0x0037,
    PidBody = 0x1000,
    PidDisplayName = 0x3001,
    PidGeneration = 0x3A05,
    PidGivenName = 0x3A06,
    PidSurname = 0x3A11,
    PidCompanyName = 0x3A16,
    PidTitle = 0x3A17,
    PidDepartmentName = 0x3A18,
    PidWeddingAnniversary = 0x3A41,
    PidBirthday = 0x3A42,
    PidMiddleName = 0x3A44,
    PidDisplayNamePrefix = 0x3A45,
    PidNickname = 0x3A4F,
    PidPersonalHomePage = 0x3A50,
    PidBusinessHomePage = 0x3A51,
    PidPrimaryTelephoneNumber = 0x3A1A,
    PidAttachDataBinary = 0x3701,
    PidAttachMimeTag = 0x370E,
    PidAttachmentContactPhoto = 0x7FFF,
};

// PSETID_Address named properties (MS-OXOCNTC 2.2).
enum : quint32 {
    LidFileUnder = 0x8005,
    LidHtml = 0x802B,
    LidWorkAddressStreet = 0x8045,
    LidWorkAddressCity = 0x8046,
    LidWorkAddressState = 0x8047,
    LidWorkAddressPostalCode = 0x8048,
    LidWorkAddressCountry = 0x8049,
    LidWorkAddressPostOfficeBox = 0x804A,
    LidDistributionListName = 0x8053,
    LidDistributionListOneOffMembers = 0x8054,
    LidDistributionListMembers = 0x8055,
    LidInstantMessagingAddress = 0x8062,
    LidBirthdayLocal = 0x80DE,
    LidWeddingAnniversaryLocal = 0x80DF,
};

struct PhoneProperty {
    quint16 id;
    const char *type;
};
// The vocabulary of contact_csv.cpp, so Outlook's own export and its data
// file give the same contact.
constexpr PhoneProperty kPhones[] = {
    {0x3A1C, "cell"}, {0x3A08, "work"}, {0x3A1B, "work"}, {0x3A09, "home"}, {0x3A2F, "home"},
    {0x3A57, "main"}, {0x3A24, "work,fax"}, {0x3A25, "home,fax"}, {0x3A23, "other,fax"},
    {0x3A1F, "other"}, {0x3A21, "pager"}, {0x3A1E, "car"}, {0x3A2E, "assistant"}, {0x3A02, "callback"},
    {0x3A1D, "radio"}, {0x3A2D, "isdn"}, {0x3A2C, "telex"}, {0x3A4B, "textphone"},
};

struct AddressProperties {
    const char *type;
    quint16 street, city, state, postalCode, country, poBox;
};
constexpr AddressProperties kTaggedAddresses[] = {
    {"work", 0x3A29, 0x3A27, 0x3A28, 0x3A2A, 0x3A26, 0x3A2B},
    {"home", 0x3A5D, 0x3A59, 0x3A5C, 0x3A5B, 0x3A5A, 0x3A5E},
    {"other", 0x3A63, 0x3A5F, 0x3A62, 0x3A61, 0x3A60, 0x3A64},
};

// Email1–3: address, address type, original display name (MS-OXOCNTC 2.2.1.2).
constexpr quint32 kEmails[][3] = {{0x8083, 0x8082, 0x8084}, {0x8093, 0x8092, 0x8094}, {0x80A3, 0x80A2, 0x80A4}};

QString namedString(Mapi::Message &message, quint32 lid)
{
    const std::optional<quint16> id = named(message, Mapi::kPsetidAddress, lid);
    return id ? message.string(*id) : QString();
}

QString dateText(const QDateTime &utc, bool exactDate)
{
    if (!utc.isValid()) return {};
    return (exactDate ? utc.date() : localDate(utc)).toString(Qt::ISODate);
}

QByteArray oneOffProvider()
{
    return QByteArray::fromHex("812b1fa4bea310199d6e00dd010f5402");
}

QByteArray wrappedProvider()
{
    return QByteArray::fromHex("c091add3519dcf11a4a900aa0047faa4");
}

quint32 le32(const QByteArray &b, qsizetype at)
{
    const auto *p = reinterpret_cast<const uchar *>(b.constData() + at);
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

// A NUL-terminated string at `*at` (UTF-16LE or 8-bit), advancing past it.
QString takeString(const QByteArray &bytes, qsizetype *at, bool unicode)
{
    QString out;
    if (unicode) {
        while (*at + 1 < bytes.size()) {
            const char16_t unit = char16_t(uchar(bytes.at(*at)) | (uchar(bytes.at(*at + 1)) << 8));
            *at += 2;
            if (unit == 0) return out;
            if (out.size() < 1024) out.append(QChar(unit));
        }
        return out;
    }
    const qsizetype end = bytes.indexOf('\0', *at);
    const qsizetype stop = end < 0 ? bytes.size() : end;
    out = QString::fromLatin1(bytes.mid(*at, qMin<qsizetype>(stop - *at, 1024)));
    *at = stop + 1;
    return out;
}

// MS-OXCDATA 2.2.5.1 one-off EntryID: display name, address type, address.
bool parseOneOff(const QByteArray &entry, DistListMember *out)
{
    if (entry.size() < 24 || entry.mid(4, 16) != oneOffProvider()) return false;
    const quint16 flags = quint16(uchar(entry.at(22)) | (uchar(entry.at(23)) << 8));
    const bool unicode = (flags & 0x8000) != 0;
    qsizetype at = 24;
    out->name = oneLine(takeString(entry, &at, unicode));
    const QString type = takeString(entry, &at, unicode);
    const QString address = oneLine(takeString(entry, &at, unicode));
    if (type.compare(QLatin1String("SMTP"), Qt::CaseInsensitive) == 0 && Mapi::usableAddress(address)) out->address = address;
    return true;
}

// MS-OXCDATA 2.2.5.3 WrappedEntryId around a .pst message EntryID
// (rgbFlags, store uid, NID: MS-PST 2.4.3.2).
bool parseWrapped(const QByteArray &entry, DistListMember *out)
{
    if (entry.size() < 21 + 24 || entry.mid(4, 16) != wrappedProvider()) return false;
    const QByteArray embedded = entry.mid(21);
    out->storeUid = embedded.mid(4, 16);
    out->contactNid = le32(embedded, 20);
    return true;
}

} // namespace

std::optional<quint16> named(const Mapi::Message &message, const QUuid &set, quint32 lid)
{
    return message.namedProperties().find(set, lid);
}

QDate localDate(const QDateTime &utc)
{
    return utc.toUTC().addSecs(12 * 3600).date();
}

bool readContact(Mapi::Message &message, Contact *out, QString *error)
{
    Contact c;
    c.displayName = oneLine(message.string(PidDisplayName));
    if (c.displayName.isEmpty()) c.displayName = oneLine(namedString(message, LidFileUnder));
    c.givenName = oneLine(message.string(PidGivenName));
    c.familyName = oneLine(message.string(PidSurname));
    c.middleName = oneLine(message.string(PidMiddleName));
    c.prefix = oneLine(message.string(PidDisplayNamePrefix));
    c.suffix = oneLine(message.string(PidGeneration));
    c.nickname = oneLine(message.string(PidNickname));
    c.organization = oneLine(message.string(PidCompanyName));
    c.department = oneLine(message.string(PidDepartmentName));
    c.title = oneLine(message.string(PidTitle));
    c.notes = message.string(PidBody);
    for (const auto &email : kEmails) {
        QString address = oneLine(namedString(message, email[0]));
        if (!Mapi::usableAddress(address)) address = oneLine(namedString(message, email[2]));
        if (Mapi::usableAddress(address)) c.emails.append({QString(), address, c.emails.isEmpty()});
    }
    const QString primary = message.string(PidPrimaryTelephoneNumber).trimmed();
    for (const PhoneProperty &phone : kPhones) {
        const QString number = oneLine(message.string(phone.id));
        if (!number.isEmpty()) c.phones.append({QString::fromLatin1(phone.type), number, !primary.isEmpty() && number == primary});
    }
    if (!primary.isEmpty() && std::none_of(c.phones.cbegin(), c.phones.cend(), [](const ContactValue &v) { return v.preferred; }))
        c.phones.prepend({QString(), primary, true});
    for (const AddressProperties &spec : kTaggedAddresses) {
        ContactAddress a;
        a.type = QString::fromLatin1(spec.type);
        a.street = message.string(spec.street).trimmed();
        a.locality = oneLine(message.string(spec.city));
        a.region = oneLine(message.string(spec.state));
        a.postalCode = oneLine(message.string(spec.postalCode));
        a.country = oneLine(message.string(spec.country));
        a.poBox = oneLine(message.string(spec.poBox));
        if (a.type == QLatin1String("work") && a.isEmpty()) {
            a.street = namedString(message, LidWorkAddressStreet).trimmed();
            a.locality = oneLine(namedString(message, LidWorkAddressCity));
            a.region = oneLine(namedString(message, LidWorkAddressState));
            a.postalCode = oneLine(namedString(message, LidWorkAddressPostalCode));
            a.country = oneLine(namedString(message, LidWorkAddressCountry));
            a.poBox = oneLine(namedString(message, LidWorkAddressPostOfficeBox));
        }
        if (!a.isEmpty()) c.addresses.append(a);
    }
    for (const auto &[id, type] : {QPair<quint16, const char *>(PidBusinessHomePage, "work"),
                                   QPair<quint16, const char *>(PidPersonalHomePage, "home")}) {
        const QString url = oneLine(message.string(id));
        if (!url.isEmpty()) c.urls.append({QString::fromLatin1(type), url, false});
    }
    if (const QString page = oneLine(namedString(message, LidHtml)); !page.isEmpty()
        && std::none_of(c.urls.cbegin(), c.urls.cend(), [&page](const ContactValue &v) { return v.value == page; }))
        c.urls.append({QString(), page, false});
    const auto localTime = [&message](quint32 lid) {
        const std::optional<quint16> id = named(message, Mapi::kPsetidAddress, lid);
        return id ? message.timeValue(*id) : QDateTime();
    };
    // AGENT-NOTE: PidTagBirthday is local midnight converted to UTC (its UTC
    // date can be the day before); the …Local named properties are midnight
    // UTC of the right date when Outlook 2010+ wrote them.
    const QDateTime birthdayLocal = localTime(LidBirthdayLocal);
    c.birthday = birthdayLocal.isValid() ? dateText(birthdayLocal, true) : dateText(message.timeValue(PidBirthday), false);
    const QDateTime anniversaryLocal = localTime(LidWeddingAnniversaryLocal);
    c.anniversary = anniversaryLocal.isValid() ? dateText(anniversaryLocal, true)
                                               : dateText(message.timeValue(PidWeddingAnniversary), false);
    if (const std::optional<quint16> keywords = message.namedProperties().find(Mapi::kPsPublicStrings, QStringLiteral("Keywords")))
        c.categories = message.strings(*keywords);
    if (const QString im = oneLine(namedString(message, LidInstantMessagingAddress)); !im.isEmpty())
        c.extra.append({QString(), QStringLiteral("IMPP"), {}, im});

    // The contact's picture: the attachment Outlook marks as such.
    std::vector<std::unique_ptr<Mapi::Attachment>> attachments;
    QString why;
    if (!message.attachments(&attachments, &why)) {
        if (error) *error = why;
        return false;
    }
    for (const std::unique_ptr<Mapi::Attachment> &attachment : attachments) {
        if (!attachment->boolValue(PidAttachmentContactPhoto)) continue;
        const QByteArray photo = attachment->binary(PidAttachDataBinary);
        if (photo.isEmpty() || photo.size() > ContactLimits::kMaxPhotoBytes || sniffImageMimeType(photo).isEmpty()) continue;
        c.photo = photo;
        c.photoMimeType = sniffImageMimeType(photo);
        break;
    }
    if (message.failed()) {
        if (error) *error = message.error();
        return false;
    }
    *out = c;
    return true;
}

bool readDistList(Mapi::Message &message, Contact *group, QList<DistListMember> *members, QString *error)
{
    Contact c;
    c.kind = QStringLiteral("group");
    c.displayName = oneLine(namedString(message, LidDistributionListName));
    if (c.displayName.isEmpty()) c.displayName = oneLine(message.string(PidDisplayName));
    if (c.displayName.isEmpty()) c.displayName = oneLine(message.string(PidSubject));
    c.notes = message.string(PidBody);
    if (const std::optional<quint16> keywords = message.namedProperties().find(Mapi::kPsPublicStrings, QStringLiteral("Keywords")))
        c.categories = message.strings(*keywords);
    // MS-OXOCNTC 2.2.2.2: the member EntryIDs and, parallel to them, one-off
    // EntryIDs carrying each member's name and address.
    const std::optional<quint16> membersId = named(message, Mapi::kPsetidAddress, LidDistributionListMembers);
    const std::optional<quint16> oneOffsId = named(message, Mapi::kPsetidAddress, LidDistributionListOneOffMembers);
    const QList<QByteArray> entries = membersId ? message.binaries(*membersId) : QList<QByteArray>();
    const QList<QByteArray> oneOffs = oneOffsId ? message.binaries(*oneOffsId) : QList<QByteArray>();
    QList<DistListMember> out;
    const qsizetype count = qMin<qsizetype>(qMax(entries.size(), oneOffs.size()), ContactLimits::kMaxGroupMembers);
    for (qsizetype i = 0; i < count; ++i) {
        DistListMember member;
        const QByteArray entry = entries.value(i);
        if (!parseWrapped(entry, &member)) parseOneOff(entry, &member);
        DistListMember oneOff;
        if (parseOneOff(oneOffs.value(i), &oneOff)) {
            if (member.name.isEmpty()) member.name = oneOff.name;
            if (member.address.isEmpty()) member.address = oneOff.address;
        }
        if (member.address.isEmpty() && member.contactNid == 0) continue;   // nothing to reach
        out.append(member);
    }
    if (message.failed()) {
        if (error) *error = message.error();
        return false;
    }
    *group = c;
    *members = out;
    return true;
}

} // namespace QindaOffice::Mail::OutlookDetail
