// SPDX-License-Identifier: GPL-3.0-or-later
// CSV contact import (M64): one row → a Contact, by the header layout.
// See contact_csv.h.

#include "contact_csv_p.h"

#include <QDate>
#include <QRegularExpression>

namespace QindaOffice::Mail::ContactCsv::Detail {

using namespace Qt::StringLiterals;

namespace {

// Outlook: M/D/YYYY (US), or the locale's D.M.YYYY / D/M/YYYY when that is
// unambiguous; "0/0/00" and the year 4501 mean "none". ISO dates as well.
QString csvDate(const QString &text)
{
    const QString t = text.trimmed();
    if (t.isEmpty()) return {};
    if (const QString iso = normalizedContactDate(t); !iso.isEmpty()) return iso;
    static const QRegularExpression re(u"^(\\d{1,2})([/.\\-])(\\d{1,2})\\2(\\d{2}|\\d{4})(?:\\s.*)?$"_s);
    const QRegularExpressionMatch m = re.match(t);
    if (!m.hasMatch()) return {};
    const int a = m.captured(1).toInt();
    const int b = m.captured(3).toInt();
    int year = m.captured(4).toInt();
    if (a == 0 || b == 0) return {};
    if (m.captured(4).size() == 2) year += year <= QDate::currentDate().year() % 100 ? 2000 : 1900;
    if (year >= 4500) return {};
    const bool dayFirst = m.captured(2) == "."_L1 || a > 12;
    const QDate date(year, dayFirst ? b : a, dayFirst ? a : b);
    return date.isValid() ? date.toString(Qt::ISODate) : QString();
}

// Google: "* Work" marks the primary value; known labels become tokens,
// anything else is the user's own label.
QPair<QString, bool> googleType(QString label)
{
    bool preferred = false;
    label = label.trimmed();
    if (label.startsWith(u'*')) {
        preferred = true;
        label = label.mid(1).trimmed();
    }
    static const QHash<QString, QString> known = {
        {u"mobile"_s, u"cell"_s}, {u"work fax"_s, u"work,fax"_s}, {u"home fax"_s, u"home,fax"_s},
        {u"other fax"_s, u"other,fax"_s}, {u"work mobile"_s, u"work,cell"_s}, {u"work pager"_s, u"work,pager"_s},
        {u"home page"_s, u"homepage"_s},
    };
    const QString lower = label.toLower();
    if (const auto it = known.constFind(lower); it != known.cend()) return {*it, preferred};
    return {isTypeTokenList(lower) ? lower : label, preferred};
}

QString digitsOf(const QString &text)
{
    QString out;
    for (QChar ch : text) {
        if (ch.isDigit()) out.append(ch);
    }
    return out;
}

// The vCard escaped-text form Contact::extra values carry (contact.h).
QString escapedValue(const QString &text)
{
    QString out = text;
    out.replace(u'\\', "\\\\"_L1);
    out.replace(u',', "\\,"_L1);
    out.replace(u';', "\\;"_L1);
    out.replace(u'\n', "\\n"_L1);
    return out;
}

class RowReader {
public:
    RowReader(const QStringList &row, const Layout &layout, Stats &stats) : m_row(row), m_layout(layout), m_stats(stats) {}
    Contact contact();

private:
    [[nodiscard]] QString cell(int column) const
    {
        if (column < 0 || column >= m_row.size()) return {};
        QString value = m_row.at(column).trimmed();
        value.replace("\r\n"_L1, "\n"_L1);
        return value;
    }
    void addEmail(const QString &address, const QString &type, bool preferred);
    static void add(QList<ContactValue> &list, const QString &type, const QString &value, bool preferred);
    void addCategory(const QString &category);
    void google();

    const QStringList &m_row;
    const Layout &m_layout;
    Stats &m_stats;
    Contact m_c;
};

void RowReader::add(QList<ContactValue> &list, const QString &type, const QString &value, bool preferred)
{
    if (!value.isEmpty() && list.size() < ContactLimits::kMaxMultiValues) list.append({type, value, preferred});
}

void RowReader::addEmail(const QString &address, const QString &type, bool preferred)
{
    if (address.isEmpty()) return;
    if (!isPlausibleEmail(address)) ++m_stats.invalidEmails;
    else add(m_c.emails, type, address, preferred);
}

void RowReader::addCategory(const QString &category)
{
    const QString t = category.trimmed();
    if (t.isEmpty() || m_c.categories.contains(t, Qt::CaseInsensitive)) return;
    if (m_c.categories.size() < ContactLimits::kMaxMultiValues) m_c.categories.append(t);
}

void RowReader::google()
{
    for (auto kind = m_layout.google.cbegin(); kind != m_layout.google.cend(); ++kind) {
        for (const QHash<QString, int> &fields : kind.value()) {
            auto field = [&](const char *name) { return cell(fields.value(QString::fromLatin1(name), -1)); };
            const QString label = !field("type").isEmpty() ? field("type") : field("label");
            const auto [type, preferred] = googleType(label);
            const QStringList values = field("value").split(" ::: "_L1, Qt::SkipEmptyParts);
            for (qsizetype i = 0; i < values.size(); ++i) {
                const QString value = values.at(i).trimmed();
                const bool first = preferred && i == 0;
                if (kind.key() == "e-mail"_L1) addEmail(value, type, first);
                else if (kind.key() == "phone"_L1) add(m_c.phones, type, value, first);
                else if (kind.key() == "website"_L1) add(m_c.urls, type, value, first);
                else if (kind.key() == "event"_L1 && type.compare("anniversary"_L1, Qt::CaseInsensitive) == 0
                         && m_c.anniversary.isEmpty()) m_c.anniversary = csvDate(value);
            }
            if (kind.key() == "address"_L1 && m_c.addresses.size() < ContactLimits::kMaxMultiValues) {
                ContactAddress a;
                a.type = type;
                a.preferred = preferred;
                a.street = field("street");
                a.locality = field("city");
                a.poBox = field("po box");
                a.region = field("region");
                a.postalCode = field("postal code");
                a.country = field("country");
                a.extended = field("extended address");
                a.label = field("formatted");
                if (!a.isEmpty()) m_c.addresses.append(a);
            }
        }
    }
}

Contact RowReader::contact()
{
    for (const auto &[member, columns] : m_layout.scalars) {
        for (int column : columns) {
            if (const QString value = cell(column); !value.isEmpty()) {
                m_c.*member = value;
                break;
            }
        }
    }
    for (const Layout::Email &e : m_layout.emails) {
        QString address = cell(e.address);
        if (!address.isEmpty() && cell(e.type).compare("EX"_L1, Qt::CaseInsensitive) == 0) {
            // An Exchange X.500 address ("/o=ExchangeLabs/ou=…"), not SMTP.
            // Outlook names the SMTP address, when it knows one, in the
            // display name: "Ada Lovelace (ada@example.org)".
            static const QRegularExpression smtp(u"\\(([^()\\s]+@[^()\\s]+)\\)"_s);
            const QRegularExpressionMatch m = smtp.match(cell(e.display));
            address = m.hasMatch() ? m.captured(1) : QString();
            if (address.isEmpty()) ++m_stats.exchange;
        }
        addEmail(address, {}, false);
    }
    for (const auto &[column, type] : m_layout.phones) add(m_c.phones, type, cell(column), false);
    if (const QString primary = cell(m_layout.primaryPhone); !primary.isEmpty()) {
        bool found = false;
        for (ContactValue &phone : m_c.phones) {
            if (!found && !digitsOf(primary).isEmpty() && digitsOf(phone.value) == digitsOf(primary)) {
                phone.preferred = true;
                found = true;
            }
        }
        if (!found && m_c.phones.size() < ContactLimits::kMaxMultiValues) m_c.phones.prepend({QString(), primary, true});
    }
    for (const Layout::Address &spec : m_layout.addresses) {
        ContactAddress a;
        a.type = spec.type;
        QStringList street;
        for (int column : spec.street) {
            if (const QString line = cell(column); !line.isEmpty()) street.append(line);
        }
        a.street = street.join(u'\n');
        a.locality = cell(spec.city);
        a.region = cell(spec.region);
        a.postalCode = cell(spec.postal);
        a.country = cell(spec.country);
        a.poBox = cell(spec.poBox);
        if (!a.isEmpty() && m_c.addresses.size() < ContactLimits::kMaxMultiValues) m_c.addresses.append(a);
    }
    for (const auto &[column, type] : m_layout.urls) add(m_c.urls, type, cell(column), false);
    for (int column : m_layout.birthday) m_c.birthday = csvDate(cell(column));
    for (int column : m_layout.anniversary) m_c.anniversary = csvDate(cell(column));
    for (const QString &category : cell(m_layout.categories).split(u';')) addCategory(category);
    for (int column : m_layout.groups) {
        for (const QString &group : cell(column).split(" ::: "_L1)) {
            if (!group.trimmed().startsWith(u'*')) addCategory(group); // "* myContacts", "* starred"
        }
    }
    // AGENT-GUARD: a remote picture URL is kept as text, never fetched.
    if (const QString photo = cell(m_layout.photo); photo.contains("://"_L1)) m_c.photoUrl = photo;
    google();
    static const QStringList defaults = {u"unspecified"_s, u"normal"_s, u"false"_s, u"0/0/00"_s};
    for (const auto &[column, name] : m_layout.unknown) {
        const QString value = cell(column);
        if (value.isEmpty() || defaults.contains(value.toLower())) continue;
        if (m_c.extra.size() < ContactLimits::kMaxExtraProperties) m_c.extra.append({QString(), name, {}, escapedValue(value)});
    }
    return m_c;
}

} // namespace

Contact contactFromRow(const QStringList &row, const Layout &layout, Stats &stats)
{
    return RowReader(row, layout, stats).contact();
}

} // namespace QindaOffice::Mail::ContactCsv::Detail
