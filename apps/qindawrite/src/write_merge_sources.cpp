// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_merge_sources.h"

#include "number_format.h"
#include "workbook.h"
#include "xlsx_bridge.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>

namespace QindaOffice::Apps::Write::MergeSources {

namespace {

constexpr char kPeopleSchema[] = "qindaoffice.mail.contacts/1";

// A sheet's used rows and columns: {rows, columns} past the last value.
std::pair<qint32, qint32> extent(const Calc::Sheet &sheet)
{
    qint32 rows = 0;
    qint32 columns = 0;
    for (auto it = sheet.cells().cbegin(); it != sheet.cells().cend(); ++it) {
        if (Calc::toText(it.value().value).isEmpty()) {
            continue;
        }
        const qint32 col = qint32(it.key() >> 32);
        const qint32 row = qint32(it.key() & 0xffffffffU);
        rows = qMax(rows, row + 1);
        columns = qMax(columns, col + 1);
    }
    return {rows, columns};
}

QString displayed(const Calc::Workbook &book, const Calc::Sheet &sheet, qint32 row, qint32 col)
{
    const Calc::CellAddr addr{row, col};
    const Calc::Cell *cell = sheet.cellAt(addr);
    if (cell == nullptr) {
        return {};
    }
    const Calc::CellStyle *style = book.style(sheet.styleIdAt(addr));
    const QString code = style != nullptr ? style->numberFormat : QStringLiteral("General");
    return Calc::formatWithCode(cell->value, code).text.trimmed();
}

QJsonArray contactsArray(QString *error)
{
    QFile file(peopleStorePath());
    if (!file.exists()) {
        if (error != nullptr) {
            *error = QStringLiteral("QindaMail has no People yet (%1)").arg(file.fileName());
        }
        return {};
    }
    if (!file.open(QIODevice::ReadOnly) || file.size() > 256LL * 1024 * 1024) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return {};
    }
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse);
    const QJsonObject root = document.object();
    const QString schema = root.value(QStringLiteral("schema")).toString();
    if (parse.error != QJsonParseError::NoError || (!schema.isEmpty() && schema != QLatin1String(kPeopleSchema))) {
        if (error != nullptr) {
            *error = QStringLiteral("QindaMail's People could not be read (%1)").arg(file.fileName());
        }
        return {};
    }
    return root.value(QStringLiteral("contacts")).toArray();
}

QString preferred(const QJsonArray &values, const QString &type = QString())
{
    QString first;
    for (const QJsonValue &v : values) {
        const QJsonObject o = v.toObject();
        const QString value = o.value(QStringLiteral("value")).toString();
        if (!type.isEmpty() && !o.value(QStringLiteral("type")).toString().contains(type, Qt::CaseInsensitive)) {
            continue;
        }
        if (o.value(QStringLiteral("preferred")).toBool()) {
            return value;
        }
        if (first.isEmpty()) {
            first = value;
        }
    }
    return first;
}

bool isGroup(const QJsonObject &contact)
{
    return contact.value(QStringLiteral("kind")).toString() == u"group";
}

QString groupName(const QJsonObject &group)
{
    const QString name = group.value(QStringLiteral("displayName")).toString().trimmed();
    return name.isEmpty() ? QStringLiteral("Group") : name;
}

// Word's address columns for one contact.
QStringList recordOf(const QJsonObject &c)
{
    QJsonObject address;
    for (const QJsonValue &a : c.value(QStringLiteral("addresses")).toArray()) {
        if (address.isEmpty() || a.toObject().value(QStringLiteral("preferred")).toBool()) {
            address = a.toObject();
        }
    }
    const QStringList street = address.value(QStringLiteral("street")).toString().split(QLatin1Char('\n'));
    QString second = street.mid(1).join(QStringLiteral(", "));
    if (const QString extended = address.value(QStringLiteral("extended")).toString(); !extended.isEmpty()) {
        second = second.isEmpty() ? extended : second + QStringLiteral(", ") + extended;
    }
    const QJsonArray phones = c.value(QStringLiteral("phones")).toArray();
    auto text = [&c](const char *key) { return c.value(QLatin1String(key)).toString(); };
    const QString home = preferred(phones, QStringLiteral("home"));
    return {text("prefix"), text("givenName"), text("middleName"), text("familyName"), text("suffix"),
            text("nickname"), text("organization"), text("title"), street.value(0), second,
            address.value(QStringLiteral("locality")).toString(), address.value(QStringLiteral("region")).toString(),
            address.value(QStringLiteral("postalCode")).toString(), address.value(QStringLiteral("country")).toString(),
            preferred(c.value(QStringLiteral("emails")).toArray()), home.isEmpty() ? preferred(phones) : home,
            preferred(phones, QStringLiteral("work")), text("displayName")};
}

// A contact group's people (M76, office_mail contact.h): its cards
// ("urn:uuid:<uid>"), groups inside it (as deep as QindaMail expands them),
// and raw addresses ("mailto:", a name and an e-mail only); each once.
void expandGroup(const QJsonArray &contacts, const QJsonObject &group, int depth, QSet<QString> &seen,
                 QList<QStringList> &records)
{
    constexpr int kMaxDepth = 8;        // office_mail ContactLimits::kMaxGroupDepth
    constexpr int kMaxMembers = 5000;   // ContactLimits::kMaxGroupMembers
    if (depth > kMaxDepth) {
        return;
    }
    for (const QJsonValue &m : group.value(QStringLiteral("members")).toArray()) {
        const QString uri = m.toObject().value(QStringLiteral("uri")).toString().trimmed();
        if (uri.isEmpty() || records.size() >= kMaxMembers || seen.contains(uri)) {
            continue;
        }
        seen.insert(uri);
        if (uri.startsWith(QLatin1String("mailto:"), Qt::CaseInsensitive)) {
            QStringList record(18);
            record[14] = QUrl::fromPercentEncoding(uri.mid(7).toUtf8()).trimmed(); // E-mail Address
            record[17] = m.toObject().value(QStringLiteral("name")).toString(); // Display Name
            records.append(record);
            continue;
        }
        for (const QJsonValue &v : contacts) {
            const QJsonObject c = v.toObject();
            const QString uid = c.value(QStringLiteral("uid")).toString().trimmed();
            const QString id = c.value(QStringLiteral("id")).toString();
            const bool match = (!uid.isEmpty() && (uri == QStringLiteral("urn:uuid:") + uid || uri == uid))
                || (!id.isEmpty() && uri == QStringLiteral("urn:uuid:") + id);
            if (match) {
                if (isGroup(c)) {
                    expandGroup(contacts, c, depth + 1, seen, records);
                } else {
                    records.append(recordOf(c));
                }
                break;
            }
        }
    }
}

} // namespace

bool isWorkbook(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == u"xlsx" || suffix == u"xlsm";
}

QStringList sheets(const QString &path)
{
    Calc::Workbook book;
    QStringList names;
    if (!isWorkbook(path) || !Calc::IO::readXlsxFile(path, &book)) {
        return names;
    }
    for (qint32 i = 0; i < book.sheetCount(); ++i) {
        if (extent(book.sheetAt(i)).first > 0) {
            names.append(book.sheetName(i));
        }
    }
    return names;
}

Docx::MergeData read(const QString &path, const QString &sheet, QString *error)
{
    if (!isWorkbook(path)) {
        return Docx::readDelimitedFile(path, error);
    }
    Calc::Workbook book;
    if (!Calc::IO::readXlsxFile(path, &book, error)) {
        return {};
    }
    Docx::MergeData data;
    data.source = path;
    for (qint32 i = 0; i < book.sheetCount(); ++i) {
        const Calc::Sheet &s = book.sheetAt(i);
        const auto [rows, columns] = extent(s);
        if (rows == 0 || (!sheet.isEmpty() && book.sheetName(i) != sheet)) {
            continue;
        }
        // Word reads the first row as the field names, the rest as records.
        for (qint32 c = 0; c < qMin<qint32>(columns, Docx::kMaxMergeFields); ++c) {
            const QString name = displayed(book, s, 0, c);
            data.fields.append(name.isEmpty() ? QStringLiteral("F%1").arg(c + 1) : name);
        }
        for (qint32 r = 1; r < qMin<qint32>(rows, Docx::kMaxMergeRecords + 1); ++r) {
            QStringList record;
            bool any = false;
            for (qint32 c = 0; c < data.fields.size(); ++c) {
                record.append(displayed(book, s, r, c));
                any = any || !record.last().isEmpty();
            }
            if (any) {
                data.records.append(record);
            }
        }
        return data;
    }
    if (error != nullptr) {
        *error = sheet.isEmpty() ? QStringLiteral("the workbook holds no data")
                                 : QStringLiteral("the workbook has no sheet %1").arg(sheet);
    }
    return {};
}

QString peopleStorePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/qindaoffice/mail/contacts.json");
}

QList<std::pair<QString, int>> peopleGroups(QString *error)
{
    const QJsonArray contacts = contactsArray(error);
    QList<std::pair<QString, int>> groups;
    if (contacts.isEmpty()) {
        return groups;
    }
    int people = 0;
    QList<std::pair<QString, int>> made; // QindaMail's contact groups (M76)
    QMap<QString, int> counts;           // categories
    for (const QJsonValue &v : contacts) {
        const QJsonObject c = v.toObject();
        if (isGroup(c)) {
            made.append({groupName(c), int(c.value(QStringLiteral("members")).toArray().size())});
            continue;
        }
        ++people;
        for (const QJsonValue &category : c.value(QStringLiteral("categories")).toArray()) {
            counts[category.toString()] += 1;
        }
    }
    groups.append({QString(), people});
    groups.append(made);
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        const bool named = std::any_of(made.cbegin(), made.cend(), [&](const auto &g) { return g.first == it.key(); });
        if (!it.key().isEmpty() && !named) {
            groups.append({it.key(), it.value()});
        }
    }
    return groups;
}

Docx::MergeData people(const QString &group, QString *error)
{
    Docx::MergeData data;
    data.source = group.isEmpty() ? QStringLiteral("qindamail:people") : QStringLiteral("qindamail:people/") + group;
    data.fields = {QStringLiteral("Title"),        QStringLiteral("First Name"),     QStringLiteral("Middle Name"),
                   QStringLiteral("Last Name"),    QStringLiteral("Suffix"),         QStringLiteral("Nickname"),
                   QStringLiteral("Company Name"), QStringLiteral("Job Title"),      QStringLiteral("Address Line 1"),
                   QStringLiteral("Address Line 2"), QStringLiteral("City"),         QStringLiteral("State"),
                   QStringLiteral("ZIP Code"),     QStringLiteral("Country or Region"), QStringLiteral("E-mail Address"),
                   QStringLiteral("Home Phone"),   QStringLiteral("Work Phone"),     QStringLiteral("Display Name")};
    const QJsonArray contacts = contactsArray(error);
    if (!group.isEmpty()) {
        for (const QJsonValue &v : contacts) {
            if (const QJsonObject c = v.toObject(); isGroup(c) && groupName(c) == group) {
                QSet<QString> seen;
                expandGroup(contacts, c, 0, seen, data.records);
                return data;
            }
        }
    }
    for (const QJsonValue &v : contacts) {
        const QJsonObject c = v.toObject();
        if (isGroup(c)) {
            continue; // a group is not a recipient; its people are
        }
        if (!group.isEmpty() && !c.value(QStringLiteral("categories")).toArray().contains(QJsonValue(group))) {
            continue;
        }
        data.records.append(recordOf(c));
    }
    return data;
}

QStringList newListFields()
{
    return {QStringLiteral("Title"),          QStringLiteral("First Name"), QStringLiteral("Last Name"),
            QStringLiteral("Company Name"),   QStringLiteral("Address Line 1"), QStringLiteral("Address Line 2"),
            QStringLiteral("City"),           QStringLiteral("State"),      QStringLiteral("ZIP Code"),
            QStringLiteral("Country or Region"), QStringLiteral("Home Phone"), QStringLiteral("Work Phone"),
            QStringLiteral("E-mail Address")};
}

} // namespace QindaOffice::Apps::Write::MergeSources
