// SPDX-License-Identifier: GPL-3.0-or-later
// Outlook / Outlook.com / Google CSV contact import (M64): decoding, RFC 4180
// records and the header layout. Rows are contact_csv_row.cpp. See
// contact_csv.h.

#include "contact_csv_p.h"

#include <QRegularExpression>
#include <QStringDecoder>

#include <optional>

namespace QindaOffice::Mail::ContactCsv {

using namespace Qt::StringLiterals;

namespace {

constexpr qsizetype kMaxFields = 4096;

QString decodeText(const QByteArray &data)
{
    QByteArrayView bytes(data);
    std::optional<QStringConverter::Encoding> encoding;
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        bytes = bytes.sliced(3);
        encoding = QStringConverter::Utf8;
    } else if (bytes.startsWith("\xFF\xFE")) {
        bytes = bytes.sliced(2);
        encoding = QStringConverter::Utf16LE;
    } else if (bytes.startsWith("\xFE\xFF")) {
        bytes = bytes.sliced(2);
        encoding = QStringConverter::Utf16BE;
    } else if (bytes.isValidUtf8()) {
        encoding = QStringConverter::Utf8;
    }
    if (encoding) {
        QStringDecoder decoder(*encoding);
        const QString text = decoder.decode(bytes);
        return text;
    }
    // AGENT-NOTE: Outlook desktop writes the ANSI code page; windows-1252
    // is right for Western locales (ICU codec; Latin-1 is the fallback and
    // differs only in 0x80–0x9F).
    QStringDecoder ansi("windows-1252"_L1);
    if (ansi.isValid()) {
        const QString text = ansi.decode(bytes);
        return text;
    }
    return QString::fromLatin1(bytes);
}

QChar detectDelimiter(const QString &text)
{
    int comma = 0, semicolon = 0, tab = 0;
    bool quoted = false;
    for (qsizetype i = 0; i < text.size() && i < 65536; ++i) {
        const QChar ch = text.at(i);
        if (ch == u'"') quoted = !quoted;
        else if (quoted) continue;
        else if (ch == u'\n' || ch == u'\r') break;
        else if (ch == u',') ++comma;
        else if (ch == u';') ++semicolon;
        else if (ch == u'\t') ++tab;
    }
    if (semicolon > comma && semicolon >= tab) return u';';
    if (tab > comma && tab > semicolon) return u'\t';
    return u',';
}

// RFC 4180 records, bounded per field and per row.
class Records {
public:
    Records(const QString &text, QChar delimiter) : m_text(text), m_delimiter(delimiter) {}
    bool next(QStringList &fields);
    [[nodiscard]] bool truncated() const { return m_truncated; }

private:
    const QString &m_text;
    QChar m_delimiter;
    qsizetype m_pos = 0;
    bool m_truncated = false;
};

bool Records::next(QStringList &fields)
{
    fields.clear();
    const qsizetype n = m_text.size();
    if (m_pos >= n) return false;
    QString field;
    bool quoted = false;
    bool fieldStart = true;
    auto push = [&] {
        if (fields.size() < kMaxFields) fields.append(field);
        else m_truncated = true;
        field.clear();
        fieldStart = true;
    };
    auto add = [&](QChar ch) {
        if (field.size() < ContactLimits::kMaxValueBytes) field.append(ch);
        else m_truncated = true;
    };
    while (m_pos < n) {
        const QChar ch = m_text.at(m_pos++);
        if (quoted) {
            if (ch != u'"') {
                add(ch);
            } else if (m_pos < n && m_text.at(m_pos) == u'"') {
                add(u'"');
                ++m_pos;
            } else {
                quoted = false; // text after the closing quote is kept (lenient)
            }
            continue;
        }
        if (ch == u'"' && fieldStart) {
            quoted = true;
            fieldStart = false;
            continue;
        }
        fieldStart = false;
        if (ch == m_delimiter) {
            push();
        } else if (ch == u'\r' || ch == u'\n') {
            if (ch == u'\r' && m_pos < n && m_text.at(m_pos) == u'\n') ++m_pos;
            push();
            return true;
        } else {
            add(ch);
        }
    }
    push();
    return true;
}

bool hasIdentity(const Contact &c)
{
    for (const QString &field : {c.displayName, c.givenName, c.middleName, c.familyName, c.nickname, c.organization}) {
        if (!field.isEmpty()) return true;
    }
    return !c.emails.isEmpty();
}

} // namespace

namespace Detail {

namespace {

struct ScalarSpec {
    QString Contact::*member;
    const char *names[4];
};
const ScalarSpec kScalars[] = {
    {&Contact::displayName, {"display name", "name"}},
    {&Contact::prefix, {"title", "name prefix"}}, // Outlook's "Title" is Mr./Dr.
    {&Contact::givenName, {"first name", "given name"}},
    {&Contact::middleName, {"middle name", "additional name"}},
    {&Contact::familyName, {"last name", "family name", "surname"}},
    {&Contact::suffix, {"suffix", "name suffix"}},
    {&Contact::nickname, {"nickname"}},
    {&Contact::organization, {"company", "organization name", "organization 1 - name", "organization"}},
    {&Contact::department, {"department", "organization department", "organization 1 - department"}},
    {&Contact::title, {"job title", "organization title", "organization 1 - title"}},
    {&Contact::notes, {"notes"}},
};

struct TypedSpec {
    const char *name;
    const char *type;
};
const TypedSpec kPhones[] = {
    {"home phone", "home"}, {"home phone 2", "home"}, {"mobile phone", "cell"}, {"business phone", "work"},
    {"business phone 2", "work"}, {"company main phone", "main"}, {"business fax", "work,fax"},
    {"home fax", "home,fax"}, {"other fax", "other,fax"}, {"other phone", "other"}, {"pager", "pager"},
    {"car phone", "car"}, {"assistant's phone", "assistant"}, {"callback", "callback"},
    {"radio phone", "radio"}, {"isdn", "isdn"}, {"telex", "telex"}, {"tty/tdd phone", "textphone"},
    {"work phone", "work"}, {"mobile number", "cell"}, {"fax number", "fax"}, {"pager number", "pager"},
};
const TypedSpec kUrls[] = {
    {"web page", ""}, {"personal web page", "home"}, {"business web page", "work"},
    {"web page 1", ""}, {"web page 2", ""},
};
const char *const kEmails[][3] = {
    {"e-mail address", "e-mail type", "e-mail display name"},
    {"e-mail 2 address", "e-mail 2 type", "e-mail 2 display name"},
    {"e-mail 3 address", "e-mail 3 type", "e-mail 3 display name"},
    {"email address", nullptr, nullptr}, {"primary email", nullptr, nullptr}, {"secondary email", nullptr, nullptr},
};
const TypedSpec kAddressPrefixes[] = {{"business", "work"}, {"work", "work"}, {"home", "home"}, {"other", "other"}};
const char *const kIgnored[] = {"initials", "priority", "sensitivity", "private", "file as"};

QString headerKey(const QString &name)
{
    QString key = name;
    key.remove(QChar(0xFEFF));
    return key.simplified().toLower();
}

// "User 1" → X-USER-1; the three Outlook relations get the names other
// address books (KAddressBook, Evolution) use.
QString extraName(const QString &header)
{
    static const QHash<QString, QString> known = {
        {u"spouse"_s, u"X-SPOUSE"_s}, {u"manager's name"_s, u"X-MANAGER"_s}, {u"assistant's name"_s, u"X-ASSISTANT"_s},
    };
    if (const auto it = known.constFind(headerKey(header)); it != known.cend()) return *it;
    QString name = u"X-"_s;
    for (QChar ch : header.toUpper()) {
        if (ch == u'\'') continue;
        if ((ch >= u'A' && ch <= u'Z') || (ch >= u'0' && ch <= u'9')) name.append(ch);
        else if (!name.endsWith(u'-')) name.append(u'-');
    }
    while (name.endsWith(u'-') && name.size() > 2) name.chop(1);
    return name.size() > 2 ? name : QString();
}

} // namespace

Layout layoutFor(const QStringList &header)
{
    Layout layout;
    QHash<QString, int> index;
    for (int i = 0; i < header.size(); ++i) {
        const QString key = headerKey(header.at(i));
        if (!key.isEmpty() && !index.contains(key)) index.insert(key, i);
    }
    QList<bool> known(header.size(), false);
    auto take = [&](const QString &name) {
        const int column = index.value(name, -1);
        if (column >= 0) known[column] = true;
        return column;
    };
    auto takeL1 = [&](const char *name) { return name ? take(QString::fromLatin1(name)) : -1; };
    for (const ScalarSpec &spec : kScalars) {
        QList<int> columns;
        for (const char *name : spec.names) {
            if (const int column = takeL1(name); column >= 0) columns.append(column);
        }
        if (!columns.isEmpty()) layout.scalars.append({spec.member, columns});
    }
    for (const TypedSpec &spec : kPhones) {
        if (const int column = takeL1(spec.name); column >= 0) layout.phones.append({column, QString::fromLatin1(spec.type)});
    }
    layout.primaryPhone = take(u"primary phone"_s);
    for (const TypedSpec &spec : kUrls) {
        if (const int column = takeL1(spec.name); column >= 0) layout.urls.append({column, QString::fromLatin1(spec.type)});
    }
    for (const auto &names : kEmails) {
        const Layout::Email email{takeL1(names[0]), takeL1(names[1]), takeL1(names[2])};
        if (email.address >= 0) layout.emails.append(email);
    }
    for (const TypedSpec &prefix : kAddressPrefixes) {
        const QString p = QString::fromLatin1(prefix.name) + u' ';
        Layout::Address a;
        a.type = QString::fromLatin1(prefix.type);
        for (const QString &suffix : {u"street"_s, u"address"_s, u"street 2"_s, u"address 2"_s, u"street 3"_s}) {
            if (const int column = take(p + suffix); column >= 0) a.street.append(column);
        }
        auto first = [&](std::initializer_list<QString> suffixes) {
            int found = -1;
            for (const QString &suffix : suffixes) {
                if (const int column = take(p + suffix); column >= 0 && found < 0) found = column;
            }
            return found;
        };
        a.city = first({u"city"_s});
        a.region = first({u"state"_s});
        a.postal = first({u"postal code"_s, u"zipcode"_s, u"zip code"_s});
        a.country = first({u"country/region"_s, u"country"_s});
        a.poBox = first({u"address po box"_s, u"po box"_s});
        if (!a.street.isEmpty() || a.city >= 0 || a.postal >= 0 || a.country >= 0 || a.poBox >= 0) layout.addresses.append(a);
    }
    if (const int column = take(u"birthday"_s); column >= 0) layout.birthday.append(column);
    if (const int column = take(u"anniversary"_s); column >= 0) layout.anniversary.append(column);
    layout.categories = take(u"categories"_s);
    for (const QString &name : {u"group membership"_s, u"labels"_s}) {
        if (const int column = take(name); column >= 0) layout.groups.append(column);
    }
    layout.photo = take(u"photo"_s);
    for (const char *name : kIgnored) takeL1(name);
    // Google's numbered groups: "E-mail 1 - Type", "Phone 2 - Value", …
    static const QRegularExpression numbered(u"^(e-mail|phone|address|website|event|organization) (\\d{1,2}) - (.+)$"_s);
    for (int i = 0; i < header.size(); ++i) {
        if (known.at(i)) continue;
        const QRegularExpressionMatch m = numbered.match(headerKey(header.at(i)));
        if (!m.hasMatch()) continue;
        known[i] = true;
        layout.google[m.captured(1)][m.captured(2).toInt()].insert(m.captured(3), i);
    }
    for (int i = 0; i < header.size(); ++i) {
        if (known.at(i)) {
            ++layout.recognized;
        } else if (const QString name = extraName(header.at(i)); !name.isEmpty()) {
            layout.unknown.append({i, name});
        }
    }
    return layout;
}

} // namespace Detail

ReadResult read(const QByteArray &data)
{
    ReadResult result;
    int droppedWarnings = 0;
    auto warn = [&result, &droppedWarnings](const QString &message) {
        if (result.warnings.size() < ContactLimits::kMaxWarnings) result.warnings.append(message);
        else ++droppedWarnings;
    };
    if (data.size() > ContactLimits::kMaxImportFileBytes) {
        warn(u"The file is larger than 64 MiB; it was not read."_s);
        return result;
    }
    const QString text = decodeText(data);
    Records records(text, detectDelimiter(text));
    QStringList header;
    if (!records.next(header)) {
        warn(u"The file is empty."_s);
        return result;
    }
    const Detail::Layout layout = Detail::layoutFor(header);
    if (layout.recognized == 0) {
        warn(u"The first row names no contact columns (First Name, E-mail Address, …); nothing was read."_s);
        return result;
    }
    Detail::Stats stats;
    QStringList row;
    int number = 1;
    bool warnedFull = false;
    while (records.next(row)) {
        ++number;
        if (row.size() == 1 && row.constFirst().trimmed().isEmpty()) continue; // a blank line, not a row
        Contact contact = Detail::contactFromRow(row, layout, stats);
        if (!hasIdentity(contact)) {
            ++result.skipped;
            warn(u"Row %1 has no name, organization or e-mail address; it was skipped."_s.arg(number));
        } else if (result.contacts.size() >= ContactLimits::kMaxContacts) {
            ++result.skipped;
            if (!warnedFull) warn(u"Only the first %1 contacts were read."_s.arg(ContactLimits::kMaxContacts));
            warnedFull = true;
        } else {
            result.contacts.append(std::move(contact));
        }
    }
    if (stats.exchange > 0) {
        warn(u"%1 Exchange-only address(es) had no SMTP form and were left out."_s.arg(stats.exchange));
    }
    if (stats.invalidEmails > 0) {
        warn(u"%1 e-mail value(s) were not e-mail addresses and were left out."_s.arg(stats.invalidEmails));
    }
    if (records.truncated()) warn(u"Some fields were longer than 1 MiB and were cut short."_s);
    if (droppedWarnings > 0) result.warnings.append(u"…and %1 more warnings."_s.arg(droppedWarnings));
    return result;
}

} // namespace QindaOffice::Mail::ContactCsv
