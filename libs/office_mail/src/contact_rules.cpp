// SPDX-License-Identifier: GPL-3.0-or-later
// Contact rules shared by the readers, the writer and the store (M64):
// dates, e-mail syntax, normalization, validation, image sniffing and
// search folding. See contact.h.

#include "contact.h"

#include <QDate>

#include <algorithm>

namespace QindaOffice::Mail {

using namespace Qt::StringLiterals;

namespace {

QString unixNewlines(QString text)
{
    text.replace(u"\r\n"_s, u"\n"_s);
    text.replace(u'\r', u'\n');
    return text;
}

void normalizeValues(QList<ContactValue> &values, bool email)
{
    QList<ContactValue> out;
    for (ContactValue v : std::as_const(values)) {
        v.value = v.value.trimmed();
        if (email && v.value.startsWith("mailto:"_L1, Qt::CaseInsensitive)) v.value = v.value.mid(7).trimmed();
        v.type = v.type.trimmed();
        if (isTypeTokenList(v.type.toLower())) v.type = v.type.toLower();
        if (!v.value.isEmpty()) out.append(v);
    }
    values = out;
}

bool allDigits(const QString &text)
{
    if (text.isEmpty()) return false;
    for (QChar ch : text) {
        if (ch < u'0' || ch > u'9') return false;
    }
    return true;
}

QString isoDate(int year, int month, int day)
{
    if (year <= 0) return {};
    const QDate date(year, month, day);
    return date.isValid() ? date.toString(Qt::ISODate) : QString();
}

void addWords(QStringList &tokens, const QString &folded)
{
    QString word;
    QString run;
    auto flush = [&tokens](QString &part) {
        if (!part.isEmpty()) tokens.append(part);
        part.clear();
    };
    for (QChar ch : folded) {
        if (ch.isSpace()) {
            flush(run);
            flush(word);
            continue;
        }
        word.append(ch);
        if (ch.isLetterOrNumber()) run.append(ch);
        else flush(run);
    }
    flush(run);
    flush(word);
}

QString digitsOnly(const QString &text)
{
    QString out;
    for (QChar ch : text) {
        if (ch >= u'0' && ch <= u'9') out.append(ch);
    }
    return out;
}

bool isPhoneLike(const QString &word)
{
    for (QChar ch : word) {
        if (!((ch >= u'0' && ch <= u'9') || QStringView(u"+-(). /").contains(ch))) return false;
    }
    return true;
}

} // namespace

QString normalizedContactDate(const QString &text)
{
    QString t = text.trimmed();
    for (QChar cut : {QChar(u'T'), QChar(u' ')}) {
        const qsizetype at = t.indexOf(cut);
        if (at > 0) t.truncate(at);
    }
    if (t.startsWith("--"_L1)) {
        const QString md = t.mid(2);
        const QStringList parts = md.contains(u'-') ? md.split(u'-')
                                                    : QStringList{md.left(2), md.mid(2)};
        if (md.size() > 5 || parts.size() != 2 || !allDigits(parts.at(0)) || !allDigits(parts.at(1))) return {};
        const int month = parts.at(0).toInt();
        const int day = parts.at(1).toInt();
        // 2000 is a leap year, so "--02-29" is a valid birthday.
        if (!QDate(2000, month, day).isValid()) return {};
        return u"--%1-%2"_s.arg(month, 2, 10, u'0').arg(day, 2, 10, u'0');
    }
    if (t.contains(u'-')) {
        const QStringList parts = t.split(u'-');
        if (parts.size() != 3 || parts.at(0).size() != 4) return {};
        for (const QString &part : parts) {
            if (!allDigits(part) || part.size() > 4) return {};
        }
        return isoDate(parts.at(0).toInt(), parts.at(1).toInt(), parts.at(2).toInt());
    }
    if (t.size() != 8 || !allDigits(t)) return {};
    return isoDate(t.left(4).toInt(), t.mid(4, 2).toInt(), t.mid(6, 2).toInt());
}

bool isPlausibleEmail(const QString &address)
{
    if (address.isEmpty() || address.size() > 254 || address.count(u'@') != 1) return false;
    const qsizetype at = address.indexOf(u'@');
    if (at == 0 || at == address.size() - 1) return false;
    for (QChar ch : address) {
        // Separators of an address list would split the recipient.
        if (ch.isSpace() || ch.unicode() < 0x20 || QStringView(u"<>,;").contains(ch)) return false;
    }
    return true;
}

bool isTypeTokenList(const QString &type)
{
    if (type.isEmpty() || type.startsWith(u',') || type.endsWith(u',') || type.contains(",,"_L1)) return false;
    for (QChar ch : type) {
        if (!((ch >= u'a' && ch <= u'z') || (ch >= u'0' && ch <= u'9') || ch == u'-' || ch == u',')) return false;
    }
    return true;
}

void normalizeContact(Contact &c)
{
    for (QString *field : {&c.id, &c.uid, &c.displayName, &c.prefix, &c.givenName, &c.middleName,
                           &c.familyName, &c.suffix, &c.nickname, &c.organization, &c.department,
                           &c.title, &c.birthday, &c.anniversary, &c.photoMimeType, &c.photoUrl,
                           &c.revision}) {
        *field = field->trimmed();
    }
    c.notes = unixNewlines(c.notes);
    // Invalid dates are kept so validateContact() can name them.
    for (QString *date : {&c.birthday, &c.anniversary}) {
        const QString canonical = normalizedContactDate(*date);
        if (!canonical.isEmpty()) *date = canonical;
    }
    normalizeValues(c.emails, true);
    normalizeValues(c.phones, false);
    normalizeValues(c.urls, false);
    QList<ContactAddress> addresses;
    for (ContactAddress a : std::as_const(c.addresses)) {
        for (QString *field : {&a.type, &a.poBox, &a.extended, &a.locality, &a.region, &a.postalCode, &a.country}) {
            *field = field->trimmed();
        }
        a.street = unixNewlines(a.street).trimmed();
        a.label = unixNewlines(a.label).trimmed();
        if (isTypeTokenList(a.type.toLower())) a.type = a.type.toLower();
        if (!a.isEmpty()) addresses.append(a);
    }
    c.addresses = addresses;
    QStringList categories;
    for (const QString &category : std::as_const(c.categories)) {
        const QString t = category.trimmed();
        if (!t.isEmpty() && !categories.contains(t, Qt::CaseInsensitive)) categories.append(t);
    }
    c.categories = categories;
    c.extra.removeIf([](const VCardProperty &p) { return p.name.trimmed().isEmpty(); });
    // M76: a group keeps its members once each; an individual has none.
    c.kind = c.kind.trimmed().toLower();
    if (c.kind == "individual"_L1) c.kind.clear();
    QList<ContactMember> members;
    if (c.isGroup()) {
        for (ContactMember m : std::as_const(c.members)) {
            m.uri = m.uri.trimmed();
            m.name = m.name.simplified();
            if (!m.uri.isEmpty() && !members.contains(m)) members.append(m);
        }
    }
    c.members = members;
}

QStringList validateContact(const Contact &c)
{
    QStringList errors;
    bool identity = false;
    for (const QString &field : {c.displayName, c.givenName, c.middleName, c.familyName, c.nickname,
                                 c.organization, c.primaryEmail()}) {
        if (!field.trimmed().isEmpty()) identity = true;
    }
    if (!identity) errors.append(u"A contact needs a name, an e-mail address or an organization."_s);
    for (qsizetype i = 0; i < c.emails.size(); ++i) {
        if (!isPlausibleEmail(c.emails.at(i).value)) {
            errors.append(u"emails[%1]: “%2” is not an e-mail address."_s.arg(i).arg(c.emails.at(i).value.left(80)));
        }
    }
    const QList<QPair<QString, qsizetype>> counts = {
        {u"emails"_s, c.emails.size()}, {u"phones"_s, c.phones.size()}, {u"urls"_s, c.urls.size()},
        {u"addresses"_s, c.addresses.size()}, {u"categories"_s, c.categories.size()},
    };
    for (const auto &[name, count] : counts) {
        if (count > ContactLimits::kMaxMultiValues) {
            errors.append(u"%1: at most %2 entries."_s.arg(name).arg(ContactLimits::kMaxMultiValues));
        }
    }
    if (c.extra.size() > ContactLimits::kMaxExtraProperties) {
        errors.append(u"extra: at most %1 properties."_s.arg(ContactLimits::kMaxExtraProperties));
    }
    const QList<QPair<QString, const QString *>> texts = {
        {u"displayName"_s, &c.displayName}, {u"notes"_s, &c.notes}, {u"organization"_s, &c.organization},
        {u"title"_s, &c.title}, {u"nickname"_s, &c.nickname}, {u"photoRemoteUri"_s, &c.photoUrl},
    };
    for (const auto &[name, text] : texts) {
        if (text->size() > ContactLimits::kMaxValueBytes) errors.append(u"%1: longer than 1 MiB."_s.arg(name));
    }
    for (const auto &[name, date] : {QPair<QString, QString>(u"birthday"_s, c.birthday),
                                     QPair<QString, QString>(u"anniversary"_s, c.anniversary)}) {
        if (!date.isEmpty() && normalizedContactDate(date) != date) {
            errors.append(u"%1: use YYYY-MM-DD, or --MM-DD without a year."_s.arg(name));
        }
    }
    if (c.photo.size() > ContactLimits::kMaxPhotoBytes) errors.append(u"photo: larger than 1 MiB."_s);
    if (c.isGroup() && c.effectiveDisplayName().isEmpty()) errors.append(u"A group needs a name."_s);
    if (c.members.size() > ContactLimits::kMaxGroupMembers)
        errors.append(u"members: at most %1 entries."_s.arg(ContactLimits::kMaxGroupMembers));
    for (qsizetype i = 0; i < c.members.size(); ++i) {
        const ContactMember &m = c.members.at(i);
        const bool control = std::any_of(m.uri.cbegin(), m.uri.cend(), [](QChar ch) { return ch.unicode() < 0x20; });
        if (m.uri.size() > 2048 || control || (m.isAddress() && !isPlausibleEmail(m.address())))
            errors.append(u"members[%1]: “%2” is not a member address."_s.arg(i).arg(m.uri.left(80)));
    }
    return errors;
}

QString sniffImageMimeType(const QByteArray &b)
{
    if (b.startsWith("\xFF\xD8\xFF")) return u"image/jpeg"_s;
    if (b.startsWith("\x89PNG\r\n\x1A\n")) return u"image/png"_s;
    if (b.startsWith("GIF87a") || b.startsWith("GIF89a")) return u"image/gif"_s;
    if (b.size() >= 12 && b.startsWith("RIFF") && b.mid(8, 4) == "WEBP") return u"image/webp"_s;
    if (b.size() >= 26 && b.startsWith("BM")) return u"image/bmp"_s;
    return {};
}

QString imageExtensionForMimeType(const QString &mimeType)
{
    const QString m = mimeType.trimmed().toLower();
    if (m == "image/jpeg"_L1 || m == "image/jpg"_L1 || m == "image/pjpeg"_L1) return u"jpg"_s;
    if (m == "image/png"_L1) return u"png"_s;
    if (m == "image/gif"_L1) return u"gif"_s;
    if (m == "image/bmp"_L1 || m == "image/x-ms-bmp"_L1) return u"bmp"_s;
    if (m == "image/webp"_L1) return u"webp"_s;
    return {};
}

QString foldForSearch(const QString &text)
{
    const QString decomposed = text.normalized(QString::NormalizationForm_KD);
    QString out;
    out.reserve(decomposed.size());
    for (QChar ch : decomposed) {
        if (ch.category() != QChar::Mark_NonSpacing) out.append(ch);
    }
    return out.toCaseFolded();
}

ContactSearchKey contactSearchKey(const Contact &c)
{
    ContactSearchKey key;
    for (const QString &field : {c.effectiveDisplayName(), c.givenName, c.middleName, c.familyName,
                                 c.nickname, c.organization, c.department}) {
        addWords(key.tokens, foldForSearch(field));
    }
    for (const ContactValue &e : c.emails) addWords(key.tokens, foldForSearch(e.value));
    for (const ContactValue &p : c.phones) {
        const QString digits = digitsOnly(p.value);
        if (!digits.isEmpty()) key.phoneDigits.append(digits);
    }
    key.tokens.removeDuplicates();
    return key;
}

QStringList searchWords(const QString &query)
{
    QStringList words;
    QString word;
    for (QChar ch : foldForSearch(query)) {
        if (ch.isSpace()) {
            if (!word.isEmpty()) words.append(word);
            word.clear();
        } else {
            word.append(ch);
        }
    }
    if (!word.isEmpty()) words.append(word);
    return words;
}

bool searchKeyMatches(const ContactSearchKey &key, const QStringList &words)
{
    for (const QString &word : words) {
        bool hit = false;
        for (const QString &token : key.tokens) {
            if (token.startsWith(word)) {
                hit = true;
                break;
            }
        }
        if (!hit && isPhoneLike(word)) {
            const QString digits = digitsOnly(word);
            for (const QString &phone : key.phoneDigits) {
                if (digits.size() >= 3 && phone.contains(digits)) hit = true;
            }
        }
        if (!hit) return false;
    }
    return true;
}

QString formatMailbox(const QString &name, const QString &address)
{
    const QString n = name.trimmed();
    if (n.isEmpty() || n.compare(address, Qt::CaseInsensitive) == 0) return address;
    bool quote = false;
    for (QChar ch : n) {
        if (QStringView(u"()<>[]:;@\\,.\"").contains(ch)) quote = true;
    }
    if (!quote) return u"%1 <%2>"_s.arg(n, address);
    QString escaped = n;
    escaped.replace(u'\\', u"\\\\"_s);
    escaped.replace(u'"', u"\\\""_s);
    return u"\"%1\" <%2>"_s.arg(escaped, address);
}

} // namespace QindaOffice::Mail
