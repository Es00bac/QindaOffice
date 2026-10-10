// SPDX-License-Identifier: GPL-3.0-or-later
// Access's XML import: the <dataroot> data, its schema, and the import
// through the Access import's pipeline. See base_xml_import.h.
#include "base_xml_import.h"

#include "base_schema.h"
#include "base_xml_export.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QXmlStreamReader>

namespace QindaOffice::Base {

namespace {

QString s(const char *text)
{
    return QString::fromUtf8(text);
}

const QString kXsdNs = s("http://www.w3.org/2001/XMLSchema");
const QString kOdNs = s("urn:schemas-microsoft-com:officedata");
const QString kQbNs = QString::fromLatin1(kQindaBaseXmlNamespace);

// An Access jet type in the Access import's words (base_access_schema.h).
QString columnType(const QString &jet, const QString &qbType, int maxLength)
{
    if (!qbType.isEmpty()) {
        return s("QB:") + qbType;
    }
    const QString j = jet.toLower();
    if (j == QLatin1String("autonumber")) {
        return s("SERIAL");
    }
    if (j == QLatin1String("text") || j == QLatin1String("replicationid")) {
        return maxLength > 0 && maxLength <= kShortTextLimit ? s("VARCHAR (%1)").arg(maxLength) : s("VARCHAR (255)");
    }
    if (j == QLatin1String("memo")) {
        return s("TEXT");
    }
    if (j == QLatin1String("byte") || j == QLatin1String("integer") || j == QLatin1String("longinteger")) {
        return s("INTEGER");
    }
    if (j == QLatin1String("bigint")) {
        return s("BIGINT");
    }
    if (j == QLatin1String("single") || j == QLatin1String("double") || j == QLatin1String("decimal")) {
        return s("DOUBLE PRECISION");
    }
    if (j == QLatin1String("currency")) {
        return s("NUMERIC(15,2)");
    }
    if (j == QLatin1String("yesno")) {
        return s("BOOLEAN");
    }
    if (j == QLatin1String("datetime")) {
        return s("TIMESTAMP WITHOUT TIME ZONE");
    }
    if (j == QLatin1String("oleobject")) {
        return s("BYTEA");
    }
    if (j == QLatin1String("hyperlink")) {
        return s("HYPERLINK");
    }
    return s("TEXT");
}

QString attribute(const QXmlStreamReader &r, const QString &ns, const char *name)
{
    return r.attributes().value(ns, QString::fromLatin1(name)).toString();
}

// The schema's tables, columns, keys and indexes (r at <xsd:schema>).
void readSchema(QXmlStreamReader &r, AccessXmlDocument *doc)
{
    int depth = 0;
    AccessTable *table = nullptr;
    int tableDepth = -1;
    AccessColumn *column = nullptr;
    QHash<QString, QString> jet;   // "table.column" (lower) → jet type
    QHash<QString, QString> qb;    // → qb:type
    QHash<QString, int> lengths;
    while (!r.atEnd()) {
        r.readNext();
        if (r.isEndElement()) {
            if (--depth < 0) {
                break; // </xsd:schema>
            }
            if (depth == tableDepth) {
                table = nullptr;
                column = nullptr;
            }
            continue;
        }
        if (!r.isStartElement()) {
            continue;
        }
        ++depth;
        const bool xsd = r.namespaceUri() == kXsdNs;
        const QString local = r.name().toString();
        if (xsd && local == QLatin1String("element") && depth == 1) {
            const QString name = nameFromXml(attribute(r, QString(), "name"));
            if (name.compare(QLatin1String("dataroot"), Qt::CaseInsensitive) != 0 && !name.isEmpty()) {
                doc->schema.tables.append(AccessTable{name, {}, {}, {}});
                table = &doc->schema.tables.last();
                tableDepth = depth - 1;
            }
            continue;
        }
        if (table == nullptr) {
            continue;
        }
        if (xsd && local == QLatin1String("element") && !attribute(r, QString(), "name").isEmpty()) {
            AccessColumn c;
            c.name = nameFromXml(attribute(r, QString(), "name"));
            c.required = attribute(r, QString(), "minOccurs") == QLatin1String("1")
                         || attribute(r, kOdNs, "nonNullable") == QLatin1String("yes");
            const QString key = (table->name + QLatin1Char('.') + c.name).toLower();
            jet.insert(key, attribute(r, kOdNs, "jetType"));
            qb.insert(key, attribute(r, kQbNs, "type"));
            const QString expression = attribute(r, kQbNs, "expression");
            if (!expression.isEmpty()) {
                doc->expressions[table->name.toLower()].insert(c.name.toLower(), expression);
            }
            table->columns.append(c);
            column = &table->columns.last();
        } else if (xsd && local == QLatin1String("maxLength") && column != nullptr) {
            lengths.insert((table->name + QLatin1Char('.') + column->name).toLower(),
                           attribute(r, QString(), "value").toInt());
        } else if (r.namespaceUri() == kOdNs && local == QLatin1String("index")) {
            AccessIndex index;
            index.name = attribute(r, QString(), "index-name");
            index.primary = attribute(r, QString(), "primary") == QLatin1String("yes");
            index.unique = attribute(r, QString(), "unique") == QLatin1String("yes");
            for (const QString &part : attribute(r, QString(), "index-key").split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
                index.columns.append(nameFromXml(part));
                index.descending.append(false);
            }
            if (!index.columns.isEmpty()) {
                table->indexes.append(index);
            }
        }
    }
    for (AccessTable &t : doc->schema.tables) {
        for (AccessColumn &c : t.columns) {
            const QString key = (t.name + QLatin1Char('.') + c.name).toLower();
            c.type = columnType(jet.value(key), qb.value(key), lengths.value(key));
        }
    }
}

// A value as the Access import reads it (base_csv.h's canonical text).
QString canonical(const QString &value, const QString &type)
{
    static const QRegularExpression dateTime(s("^(\\d{4}-\\d{2}-\\d{2})(?:[T ](\\d{2}:\\d{2}:\\d{2}))?"));
    const QString t = type.toUpper();
    if (t.startsWith(QLatin1String("TIMESTAMP")) || t == QLatin1String("QB:DATE") || t == QLatin1String("QB:DATETIME")) {
        const QRegularExpressionMatch m = dateTime.match(value.trimmed());
        if (!m.hasMatch()) {
            return value;
        }
        const QString time = m.captured(2);
        const bool dateOnly = t == QLatin1String("QB:DATE")
                              || (t != QLatin1String("QB:DATETIME") && (time.isEmpty() || time == QLatin1String("00:00:00")));
        return dateOnly ? m.captured(1) : m.captured(1) + QLatin1Char(' ') + (time.isEmpty() ? s("00:00:00") : time);
    }
    if (t == QLatin1String("BOOLEAN") || t == QLatin1String("QB:YESNO")) {
        const QString v = value.trimmed().toLower();
        return v == QLatin1String("1") || v == QLatin1String("true") || v == QLatin1String("-1") ? s("Yes") : s("No");
    }
    if (t == QLatin1String("BYTEA") || t == QLatin1String("QB:FILE")) {
        return QString::fromLatin1(QByteArray::fromBase64(value.trimmed().toLatin1()).toHex().toUpper());
    }
    return value;
}

// The records (r at <dataroot>): table → field → value, in document order.
void readData(QXmlStreamReader &r, QHash<QString, QList<QHash<QString, QString>>> *records,
              QHash<QString, QStringList> *order, QStringList *tables)
{
    while (r.readNextStartElement()) {
        const QString table = nameFromXml(r.name().toString());
        if (!tables->contains(table, Qt::CaseInsensitive)) {
            tables->append(table);
        }
        QHash<QString, QString> record;
        while (r.readNextStartElement()) {
            const QString field = nameFromXml(r.name().toString());
            QStringList &fields = (*order)[table.toLower()];
            if (!fields.contains(field, Qt::CaseInsensitive)) {
                fields.append(field);
            }
            record.insert(field.toLower(), r.readElementText(QXmlStreamReader::IncludeChildElements));
        }
        (*records)[table.toLower()].append(record);
    }
}

class XmlReader final : public AccessReader {
public:
    explicit XmlReader(const AccessXmlDocument &doc)
        : m_doc(doc)
    {
    }
    [[nodiscard]] QStringList tables(QString *) override
    {
        QStringList out;
        for (const XmlTable &t : m_doc.tables) {
            out.append(t.name);
        }
        return out;
    }
    [[nodiscard]] std::optional<CsvTable> rows(const QString &table, QString *error) override
    {
        const auto it = m_doc.data.constFind(table.toLower());
        if (it == m_doc.data.cend()) {
            *error = s("The XML holds no records of %1.").arg(table);
            return std::nullopt;
        }
        // Calculated fields come back worked out, after the import (below).
        const QHash<QString, QString> calculated = m_doc.expressions.value(table.toLower());
        CsvTable out;
        QList<int> kept;
        for (int c = 0; c < it->header.size(); ++c) {
            if (!calculated.contains(it->header.at(c).toLower())) {
                kept.append(c);
                out.header.append(it->header.at(c));
            }
        }
        for (const QStringList &row : it->rows) {
            QStringList values;
            for (const int c : std::as_const(kept)) {
                values.append(row.value(c));
            }
            out.rows.append(values);
        }
        return out;
    }
    [[nodiscard]] QString schema(QString *error) override
    {
        *error = s("the XML has no schema");
        return {};
    }
    [[nodiscard]] QStringList queries(QString *) override { return {}; }
    [[nodiscard]] QString querySql(const QString &, QString *) override { return {}; }
    [[nodiscard]] QHash<QString, int> queryFlags(QString *) override { return {}; }
    [[nodiscard]] std::optional<AccessSchema> parsedSchema() override
    {
        if (m_doc.schemaSource.isEmpty()) {
            return std::nullopt;
        }
        AccessSchema schema = m_doc.schema;
        for (AccessTable &t : schema.tables) {
            const QHash<QString, QString> calculated = m_doc.expressions.value(t.name.toLower());
            t.columns.removeIf([&](const AccessColumn &c) { return calculated.contains(c.name.toLower()); });
        }
        return schema;
    }

private:
    const AccessXmlDocument &m_doc;
};

} // namespace

AccessXmlDocument readAccessXml(const QString &path, const QByteArray &given)
{
    AccessXmlDocument doc;
    QByteArray bytes = given;
    if (bytes.isEmpty()) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            doc.error = file.errorString();
            return doc;
        }
        bytes = file.readAll();
    }
    QHash<QString, QList<QHash<QString, QString>>> records;
    QHash<QString, QStringList> order;
    QStringList tables;
    QString schemaFile;
    QXmlStreamReader r(bytes);
    while (!r.atEnd()) {
        r.readNext();
        if (!r.isStartElement()) {
            continue;
        }
        if (r.namespaceUri() == kXsdNs && r.name() == QLatin1String("schema")) {
            doc.schemaSource = s("embedded");
            readSchema(r, &doc);
        } else if (r.name() == QLatin1String("dataroot")) {
            schemaFile = r.attributes().value(s("http://www.w3.org/2001/XMLSchema-instance"),
                                              s("noNamespaceSchemaLocation")).toString();
            readData(r, &records, &order, &tables);
        }
    }
    if (r.hasError()) {
        doc.error = s("The XML cannot be read (line %1: %2).").arg(r.lineNumber()).arg(r.errorString());
        return doc;
    }
    if (doc.schemaSource.isEmpty() && !schemaFile.isEmpty()) {
        QFile xsd(QFileInfo(path).absoluteDir().filePath(schemaFile));
        if (xsd.open(QIODevice::ReadOnly)) {
            QXmlStreamReader sr(xsd.readAll());
            while (!sr.atEnd()) {
                sr.readNext();
                if (sr.isStartElement() && sr.namespaceUri() == kXsdNs && sr.name() == QLatin1String("schema")) {
                    doc.schemaSource = QFileInfo(schemaFile).fileName();
                    readSchema(sr, &doc);
                }
            }
        }
    }
    // Every table of the schema is in the document, even without records.
    for (const AccessTable &t : std::as_const(doc.schema.tables)) {
        if (!tables.contains(t.name, Qt::CaseInsensitive)) {
            tables.append(t.name);
        }
    }
    for (const QString &table : std::as_const(tables)) {
        const AccessTable *design = doc.schema.table(table);
        QStringList header;
        QStringList types;
        if (design != nullptr) {
            for (const AccessColumn &c : design->columns) {
                header.append(c.name);
                types.append(c.type);
            }
        }
        for (const QString &field : order.value(table.toLower())) {
            if (!header.contains(field, Qt::CaseInsensitive)) {
                header.append(field);
                types.append(QString());
            }
        }
        CsvTable csv;
        csv.header = header;
        static const QRegularExpression isoDateTime(s("^\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}"));
        for (const QHash<QString, QString> &record : records.value(table.toLower())) {
            QStringList row;
            for (int c = 0; c < header.size(); ++c) {
                const QString value = record.value(header.at(c).toLower());
                // Without a schema an ISO date-time still reads as one.
                const QString type = types.at(c).isEmpty() && isoDateTime.match(value).hasMatch()
                                         ? s("TIMESTAMP")
                                         : types.at(c);
                row.append(value.isEmpty() ? QString() : canonical(value, type));
            }
            csv.rows.append(row);
        }
        doc.tables.append({table, header, csv.rows.size()});
        doc.data.insert(table.toLower(), csv);
    }
    doc.ok = !doc.tables.isEmpty();
    if (!doc.ok) {
        doc.error = s("The XML holds no records and no table design.");
    }
    return doc;
}

AccessImportReport importAccessXml(BaseFile &file, const AccessXmlDocument &document, const QStringList &tables,
                                   QList<Change> *changes)
{
    XmlReader reader(document);
    AccessImportOptions options;
    QString ignored;
    options.tables = tables.isEmpty() ? reader.tables(&ignored) : tables;
    options.queries = false;
    AccessImportReport report = importAccessDatabase(file, reader, options, changes);
    if (document.schemaSource.isEmpty() && report.ok) {
        report.notes.prepend(s("The XML has no schema, so the types were worked out from the values."));
    }
    // Calculated fields (QindaBase's qb:expression): added to the imported
    // table where they stood, worked out again by the expression.
    for (const AccessImportReport::Table &imported : std::as_const(report.tables)) {
        const QHash<QString, QString> calculated = document.expressions.value(imported.source.toLower());
        const AccessTable *design = document.schema.table(imported.source);
        if (calculated.isEmpty() || design == nullptr) {
            continue;
        }
        const std::optional<TableDef> before = readTable(file, imported.table);
        if (!before) {
            continue;
        }
        TableDef after = *before;
        int position = 0;
        for (const AccessColumn &c : design->columns) {
            if (!calculated.contains(c.name.toLower())) {
                position = std::min<int>(position + 1, int(after.fields.size()));
                continue;
            }
            FieldDef f;
            f.name = c.name;
            f.calculated = true;
            f.expression = calculated.value(c.name.toLower());
            f.type = accessFieldType(c).value_or(FieldType::Other);
            after.fields.insert(position, f);
            position = std::min<int>(position + 1, int(after.fields.size()));
        }
        const SchemaPlan plan = planAlterTable(file, *before, after);
        Change change;
        const Result made = plan.ok() ? file.applyPlan(plan, &change) : Result::failure(plan.error);
        if (made.ok) {
            changes->append(change);
        } else {
            report.notes.append(s("%1: the calculated fields could not be made (%2); they were left out.")
                                    .arg(imported.table, made.error));
        }
    }
    return report;
}

} // namespace QindaOffice::Base
