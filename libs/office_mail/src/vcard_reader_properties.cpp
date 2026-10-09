// SPDX-License-Identifier: GPL-3.0-or-later
// vCard reader (M64): one property → the Contact being read. See vcard.h
// for what is mapped; everything else is kept in Contact::extra.

#include "vcard_reader_p.h"

#include "vcard_p.h"

#include <tuple>
#include <utility>

namespace QindaOffice::Mail::VCard::Detail {

using namespace Qt::StringLiterals;

QString Reader::text(const ContentLine &cl) const
{
    const QString encoding = paramValue(cl.params, "ENCODING"_L1).toUpper();
    QByteArray bytes = cl.value;
    bool transferEncoded = true;
    if (encoding == "QUOTED-PRINTABLE"_L1) bytes = decodeQuotedPrintable(bytes);
    else if (encoding == "BASE64"_L1 || encoding == "B"_L1) bytes = decodeBase64(bytes);
    else transferEncoded = false;
    // A UTF-16 file was converted to UTF-8 as a whole; a CHARSET on its
    // 8-bit values described the original bytes, not these.
    QString out = decodeBytes(bytes, paramValue(cl.params, "CHARSET"_L1), transferEncoded || !m_unicodeFile);
    if (out.contains(u'\r')) {
        out.replace("\r\n"_L1, "\n"_L1);
        out.replace(u'\r', u'\n');
    }
    return out;
}

void Reader::apply(const ContentLine &cl)
{
    Card &card = m_card;
    Contact &c = card.contact;
    const QString &key = cl.key;
    if (key != "PHOTO"_L1 && cl.value.size() > ContactLimits::kMaxValueBytes) {
        warn(u"Card %1: a %2 value larger than 1 MiB was dropped."_s.arg(card.number).arg(cl.name));
        return;
    }
    if (key != "LABEL"_L1 && key != "X-ABLABEL"_L1) card.lastAddress = -1;
    // Single-valued properties: the first is mapped, later ones go to extra.
    auto once = [&card, &key] {
        if (card.mapped.contains(key)) return false;
        card.mapped.insert(key);
        return true;
    };
    auto unescaped = [this, &cl] { return unescapeText(text(cl), v21()).trimmed(); };

    if (key == "VERSION"_L1) {
        const QString version = text(cl).trimmed();
        card.sawVersion = true;
        if (version == "2.1"_L1 || version == "3.0"_L1 || version == "4.0"_L1) card.version = version;
        else warn(u"Card %1 says VERSION %2; it was read as 3.0."_s.arg(card.number).arg(version.left(16)));
        return;
    }
    if (key == "PRODID"_L1) return;
    if (key == "FN"_L1 && once()) {
        c.displayName = unescaped();
        return;
    }
    if (key == "N"_L1 && once()) {
        const QStringList parts = splitUnescaped(text(cl), u';');
        QString *fields[] = {&c.familyName, &c.givenName, &c.middleName, &c.prefix, &c.suffix};
        for (qsizetype i = 0; i < 5; ++i) *fields[i] = unescapeText(parts.value(i), v21()).trimmed();
        return;
    }
    if (key == "NICKNAME"_L1) {
        QStringList names = c.nickname.isEmpty() ? QStringList() : QStringList{c.nickname};
        for (const QString &part : splitUnescaped(text(cl), u',')) {
            const QString name = unescapeText(part, v21()).trimmed();
            if (!name.isEmpty()) names.append(name);
        }
        c.nickname = names.join(", "_L1);
        return;
    }
    if (key == "EMAIL"_L1) {
        QString value = unescaped();
        if (value.startsWith("mailto:"_L1, Qt::CaseInsensitive)) value = value.mid(7).trimmed();
        addValue(ValueKind::Email, cl, value);
        return;
    }
    if (key == "TEL"_L1) {
        QString value = unescaped();
        if (value.startsWith("tel:"_L1, Qt::CaseInsensitive)) value = value.mid(4).trimmed();
        addValue(ValueKind::Phone, cl, value);
        return;
    }
    if (key == "URL"_L1) {
        addValue(ValueKind::Url, cl, unescaped());
        return;
    }
    if (key == "ADR"_L1) {
        addAddress(cl);
        return;
    }
    if (key == "LABEL"_L1) {
        addLabel(cl);
        return;
    }
    if (key == "ORG"_L1 && once()) {
        const QStringList parts = splitUnescaped(text(cl), u';');
        c.organization = unescapeText(parts.value(0), v21()).trimmed();
        QStringList units;
        for (qsizetype i = 1; i < parts.size(); ++i) {
            const QString unit = unescapeText(parts.at(i), v21()).trimmed();
            if (!unit.isEmpty()) units.append(unit);
        }
        c.department = units.join(", "_L1);
        return;
    }
    if (key == "TITLE"_L1 && once()) {
        c.title = unescaped();
        return;
    }
    if (key == "BDAY"_L1 && c.birthday.isEmpty() && addDate(cl, c.birthday)) return;
    const bool anniversary = key == "ANNIVERSARY"_L1 || key == "X-ANNIVERSARY"_L1
        || key == "X-MS-ANNIVERSARY"_L1 || key == "X-EVOLUTION-ANNIVERSARY"_L1;
    if (anniversary && c.anniversary.isEmpty() && addDate(cl, c.anniversary)) return;
    if (key == "NOTE"_L1 && once()) {
        c.notes = unescapeText(text(cl), v21());
        return;
    }
    if (key == "CATEGORIES"_L1) {
        for (const QString &part : splitUnescaped(text(cl), u',')) {
            const QString category = unescapeText(part, v21()).trimmed();
            if (category.isEmpty() || c.categories.contains(category, Qt::CaseInsensitive)) continue;
            if (c.categories.size() >= ContactLimits::kMaxMultiValues) {
                tooMany();
                break;
            }
            c.categories.append(category);
        }
        return;
    }
    if (key == "PHOTO"_L1 && addPhoto(cl)) return;
    if (key == "UID"_L1 && once()) {
        c.uid = unescaped();
        return;
    }
    if (key == "REV"_L1 && once()) {
        c.revision = unescaped();
        return;
    }
    if (key == "X-ABLABEL"_L1 && !cl.group.isEmpty()) {
        const qsizetype index = c.extra.size();
        addExtra(cl);
        if (c.extra.size() > index) card.labels.append({cl.group.toUpper(), index});
        return;
    }
    addExtra(cl);
}

void Reader::addValue(ValueKind kind, const ContentLine &cl, const QString &value)
{
    if (value.isEmpty()) return;
    Contact &c = m_card.contact;
    QList<ContactValue> &list = kind == ValueKind::Email ? c.emails : kind == ValueKind::Phone ? c.phones : c.urls;
    if (list.size() >= ContactLimits::kMaxMultiValues) {
        tooMany();
        return;
    }
    const auto [type, preferred] = readTypes(cl.params, kind);
    list.append({type, value, preferred});
    if (!cl.group.isEmpty()) m_card.grouped.append({kind, list.size() - 1, cl.group.toUpper()});
}

void Reader::addAddress(const ContentLine &cl)
{
    Contact &c = m_card.contact;
    const QStringList parts = splitUnescaped(text(cl), u';');
    ContactAddress a;
    QString *fields[] = {&a.poBox, &a.extended, &a.street, &a.locality, &a.region, &a.postalCode, &a.country};
    for (qsizetype i = 0; i < 7; ++i) *fields[i] = unescapeText(parts.value(i), v21()).trimmed();
    a.label = paramValue(cl.params, "LABEL"_L1).trimmed(); // 4.0 ADR;LABEL=
    std::tie(a.type, a.preferred) = readTypes(cl.params, ValueKind::Address);
    if (a.isEmpty()) return;
    if (c.addresses.size() >= ContactLimits::kMaxMultiValues) {
        tooMany();
        return;
    }
    c.addresses.append(a);
    m_card.lastAddress = c.addresses.size() - 1;
    if (!cl.group.isEmpty()) m_card.grouped.append({ValueKind::Address, c.addresses.size() - 1, cl.group.toUpper()});
}

// vCard 2.1/3.0 LABEL: the formatted label of the ADR it follows, else of
// the first ADR of the same type; kept in extra until the card ends when
// no ADR matches yet (the ADR may come later).
void Reader::addLabel(const ContentLine &cl)
{
    Contact &c = m_card.contact;
    const QString label = unescapeText(text(cl), v21()).trimmed();
    const QString type = readTypes(cl.params, ValueKind::Address).first;
    const qsizetype last = std::exchange(m_card.lastAddress, -1);
    if (label.isEmpty()) return;
    if (last >= 0 && c.addresses.at(last).label.isEmpty()
        && (type.isEmpty() || sameTypeTokens(type, c.addresses.at(last).type))) {
        c.addresses[last].label = label;
        return;
    }
    for (ContactAddress &a : c.addresses) {
        if (a.label.isEmpty() && sameTypeTokens(type, a.type)) {
            a.label = label;
            return;
        }
    }
    const qsizetype index = c.extra.size();
    addExtra(cl);
    if (c.extra.size() > index) m_card.pendingLabels.append({index, type, label});
}

bool Reader::addDate(const ContentLine &cl, QString &field)
{
    QString date = normalizedContactDate(unescapeText(text(cl), v21()));
    if (date.isEmpty()) return false; // e.g. 4.0 VALUE=text "circa 1800": kept in extra
    // Apple writes a birthday without a year as BDAY;X-APPLE-OMIT-YEAR=1604:1604-04-15.
    const QString omitYear = paramValue(cl.params, "X-APPLE-OMIT-YEAR"_L1).trimmed();
    if (!omitYear.isEmpty() && date.startsWith(omitYear + u'-')) date = "--"_L1 + date.mid(5);
    field = date;
    return true;
}

// PHOTO: 2.1 ENCODING=BASE64, 3.0 ENCODING=b, 4.0 data: URI, or a URI kept
// as text. Returns false when the property should go to extra instead.
bool Reader::addPhoto(const ContentLine &cl)
{
    Contact &c = m_card.contact;
    QString declared;
    for (const auto &[name, value] : cl.params) {
        if (name.compare("TYPE"_L1, Qt::CaseInsensitive) == 0 || name.compare("MEDIATYPE"_L1, Qt::CaseInsensitive) == 0) {
            for (const QString &t : value.split(u',')) {
                if (const QString mime = mimeTypeForTypeParam(t); !mime.isEmpty()) declared = mime;
            }
        }
    }
    const QString encoding = paramValue(cl.params, "ENCODING"_L1).toUpper();
    const QString valueType = paramValue(cl.params, "VALUE"_L1).toLower();
    const QString invalid = u"Card %1: the photo could not be decoded; it was dropped."_s.arg(m_card.number);
    QByteArray bytes;
    if (encoding == "B"_L1 || encoding == "BASE64"_L1) {
        if (!c.photo.isEmpty()) return false;
        bytes = decodeBase64(cl.value);
    } else if (const QString value = text(cl).trimmed(); value.startsWith("data:"_L1, Qt::CaseInsensitive)) {
        if (!c.photo.isEmpty()) return false;
        const qsizetype comma = value.indexOf(u',');
        if (comma < 0) {
            warn(invalid);
            return true;
        }
        const QStringList meta = value.mid(5, comma - 5).split(u';');
        if (const QString mime = meta.value(0).trimmed().toLower(); !mime.isEmpty()) declared = mime;
        const QByteArray payload = value.mid(comma + 1).toLatin1();
        bytes = meta.contains("base64"_L1, Qt::CaseInsensitive) ? decodeBase64(payload)
                                                                : QByteArray::fromPercentEncoding(payload);
    } else if (valueType == "uri"_L1 || valueType == "url"_L1 || value.contains("://"_L1)) {
        if (!c.photoUrl.isEmpty()) return false;
        // AGENT-GUARD: kept as text only; nothing in the address book ever
        // fetches it (a remote picture is a read receipt for the sender).
        c.photoUrl = value;
        return true;
    } else {
        if (!c.photo.isEmpty()) return false;
        bytes = decodeBase64(cl.value); // a 2.1 photo without ENCODING
        if (sniffImageMimeType(bytes).isEmpty()) return false;
    }
    if (bytes.isNull()) {
        warn(invalid);
        return true;
    }
    if (bytes.size() > ContactLimits::kMaxPhotoBytes) {
        warn(u"Card %1: the photo is larger than 1 MiB; it was dropped."_s.arg(m_card.number));
        return true;
    }
    if (bytes.isEmpty()) return true;
    const QString sniffed = sniffImageMimeType(bytes);
    c.photo = bytes;
    c.photoMimeType = sniffed.isEmpty() ? declared : sniffed;
    return true;
}

void Reader::addExtra(const ContentLine &cl)
{
    Contact &c = m_card.contact;
    if (c.extra.size() >= ContactLimits::kMaxExtraProperties) {
        if (!m_card.warnedExtras) {
            warn(u"Card %1 has more than %2 other properties; the rest were dropped."_s.arg(m_card.number)
                     .arg(ContactLimits::kMaxExtraProperties));
        }
        m_card.warnedExtras = true;
        return;
    }
    const QString encoding = paramValue(cl.params, "ENCODING"_L1).toUpper();
    const bool binary = encoding == "B"_L1 || encoding == "BASE64"_L1;
    VCardProperty p;
    p.group = cl.group;
    p.name = cl.name;
    for (const auto &[name, value] : cl.params) {
        // AGENT-NOTE: the value below is decoded text, so a 2.1
        // QUOTED-PRINTABLE/CHARSET parameter must not survive: re-emitted,
        // it would make every reader decode the value a second time.
        if (name.compare("CHARSET"_L1, Qt::CaseInsensitive) == 0) continue;
        if (name.compare("ENCODING"_L1, Qt::CaseInsensitive) == 0) {
            if (binary) p.params.append({u"ENCODING"_s, u"b"_s});
            continue;
        }
        p.params.append({name, value});
    }
    if (binary) {
        QByteArray compact;
        for (char ch : cl.value) {
            if (ch != ' ' && ch != '\t') compact.append(ch);
        }
        p.value = QString::fromLatin1(compact);
    } else {
        p.value = text(cl);
        p.value.replace(u'\n', "\\n"_L1); // a QUOTED-PRINTABLE newline must not break the line on write
    }
    c.extra.append(p);
}

} // namespace QindaOffice::Mail::VCard::Detail
