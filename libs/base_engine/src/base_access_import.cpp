// SPDX-License-Identifier: GPL-3.0-or-later
// The Access import: tables, relationships, queries (base_access_import.h).
#include "base_access_import.h"

#include "base_access_query.h"
#include "base_access_schema.h"
#include "base_objects.h"
#include "base_query.h"
#include "base_schema.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace QindaOffice::Base {

namespace {

QString s(const char *text)
{
    return QString::fromLatin1(text);
}

QString cleanName(QString name, const QString &fallback)
{
    static const QRegularExpression unsafe(QStringLiteral("[\"\\[\\]`\\x00-\\x1f]"));
    name.remove(unsafe);
    name = name.trimmed().left(64).trimmed();
    if (name.isEmpty() || BaseFile::isSystemName(name)) {
        name = fallback;
    }
    return name;
}

QString uniqueTableName(const BaseFile &file, const QString &wanted)
{
    const QString base = cleanName(wanted, s("Imported"));
    QString name = base;
    for (int n = 2; file.tableExists(name); ++n) {
        name = QStringLiteral("%1 %2").arg(base).arg(n);
    }
    return name;
}

QStringList filled(const CsvTable &csv, int column)
{
    QStringList out;
    for (const QStringList &row : csv.rows) {
        if (!row.value(column).isEmpty()) {
            out.append(row.value(column));
        }
    }
    return out;
}

struct Imported {
    QString table;
    QHash<QString, QString> fields; // Access column, lower-cased → field name
};

class Run {
public:
    Run(BaseFile &file, AccessReader &reader, AccessImportReport &report, QList<Change> *changes)
        : m_file(file)
        , m_reader(reader)
        , m_report(report)
        , m_changes(changes)
    {
    }

    void readSchema()
    {
        if (std::optional<AccessSchema> parsed = m_reader.parsedSchema()) {
            m_schema = std::move(parsed);
            return;
        }
        QString error;
        const QString ddl = m_reader.schema(&error);
        if (!ddl.trimmed().isEmpty()) {
            m_schema = parseAccessSchema(ddl);
            return;
        }
        m_report.notes.append(QStringLiteral("The Access design could not be read (%1), so types were detected from "
                                             "the values, and keys, indexes and relationships were not imported.")
                                  .arg(error.isEmpty() ? s("mdb-schema printed nothing") : error));
    }

    void table(const QString &source)
    {
        QString error;
        std::optional<CsvTable> csv = m_reader.rows(source, &error);
        if (!csv) {
            m_report.skipped.append(QStringLiteral("Table %1: %2").arg(source, error));
            return;
        }
        const AccessTable *st = m_schema ? m_schema->table(source) : nullptr;
        if (st != nullptr) {
            withSchema(source, *st, std::move(*csv));
        } else {
            withoutSchema(source, std::move(*csv));
        }
    }

    void relationships()
    {
        if (!m_schema) {
            return;
        }
        for (const AccessRelationship &r : m_schema->relationships) {
            const QString text = QStringLiteral("%1.%2 → %3.%4").arg(r.childTable, r.childColumn, r.parentTable, r.parentColumn);
            const auto child = m_imported.constFind(r.childTable.toLower());
            const auto parent = m_imported.constFind(r.parentTable.toLower());
            if (child == m_imported.cend() || parent == m_imported.cend()) {
                if (child != m_imported.cend() || parent != m_imported.cend()) {
                    m_report.skipped.append(QStringLiteral("Relationship %1: only one of its tables was imported.").arg(text));
                }
                continue;
            }
            if (!r.enforced) {
                m_report.skipped.append(QStringLiteral("Relationship %1: Access does not enforce it, so it was not "
                                                       "created (add it under Database Tools ▸ Relationships).")
                                            .arg(text));
                continue;
            }
            Relationship rel;
            rel.childTable = child->table;
            rel.childField = child->fields.value(r.childColumn.toLower());
            rel.parentTable = parent->table;
            rel.parentField = parent->fields.value(r.parentColumn.toLower());
            rel.onDelete = r.cascadeDelete ? RefAction::Cascade : RefAction::NoAction;
            rel.onUpdate = r.cascadeUpdate ? RefAction::Cascade : RefAction::NoAction;
            const SchemaPlan plan = planAddRelationship(m_file, rel);
            Change change;
            const Result made = plan.ok() ? m_file.applyPlan(plan, &change) : Result::failure(plan.error);
            if (!made.ok) {
                m_report.skipped.append(QStringLiteral("Relationship %1: %2").arg(text, made.error));
                continue;
            }
            m_changes->append(change);
            m_report.relationships.append(QStringLiteral("%1.%2 → %3.%4")
                                              .arg(rel.childTable, rel.childField, rel.parentTable, rel.parentField));
        }
    }

    void queries()
    {
        if (!m_file.hasSystemTables()) {
            m_report.notes.append(QStringLiteral("Saved queries need QindaBase's system tables; add them and import "
                                                 "again to bring the queries."));
            return;
        }
        QString error;
        const QStringList names = m_reader.queries(&error);
        if (!error.isEmpty()) {
            m_report.notes.append(QStringLiteral("The saved queries could not be listed: %1").arg(error));
            return;
        }
        QString flagError;
        const QHash<QString, int> flags = m_reader.queryFlags(&flagError);
        QHash<QString, QString> tables;
        for (auto it = m_imported.cbegin(); it != m_imported.cend(); ++it) {
            tables.insert(it.key(), it->table);
        }
        for (const QString &name : names) {
            if (name.startsWith(QLatin1Char('~')) || name.startsWith(QLatin1String("MSys"), Qt::CaseInsensitive)) {
                continue; // the hidden record sources of Access forms and reports
            }
            const QString kind = flags.contains(name) ? accessQueryKind(flags.value(name)) : s("select");
            if (kind != QLatin1String("select")) {
                const bool vowel = QStringLiteral("aeiou").contains(kind.at(0));
                m_report.skipped.append(QStringLiteral("Query %1: %2 %3 query; only select queries are converted.")
                                            .arg(name, vowel ? s("an") : s("a"), kind));
                continue;
            }
            QString sqlError;
            const QString sql = m_reader.querySql(name, &sqlError);
            if (sql.isEmpty()) {
                m_report.skipped.append(QStringLiteral("Query %1: %2").arg(name, sqlError));
                continue;
            }
            const AccessQueryConversion converted = convertAccessQuery(sql, tables);
            if (!converted.design) {
                m_report.skipped.append(QStringLiteral("Query %1: %2").arg(name, converted.reason));
                continue;
            }
            const CompiledQuery compiled = compileQuery(m_file, *converted.design);
            if (!compiled.ok) {
                m_report.skipped.append(QStringLiteral("Query %1: %2").arg(name, compiled.error));
                continue;
            }
            QVariantMap definition = converted.design->toMap();
            definition.insert(s("description"), QStringLiteral("Imported from Access, where its SQL was: %1").arg(sql));
            const QString saved = ObjectStore::uniqueName(m_file, s("query"), cleanName(name, s("Query")));
            Change change;
            const Result r = ObjectStore::save(m_file, s("query"), saved, definition, false, &change);
            if (!r.ok) {
                m_report.skipped.append(QStringLiteral("Query %1: %2").arg(name, r.error));
                continue;
            }
            m_changes->append(change);
            m_report.queries.append(saved);
        }
        if (!flagError.isEmpty() && !m_report.queries.isEmpty()) {
            m_report.notes.append(QStringLiteral("The kinds of the queries could not be checked (%1); check that the "
                                                 "imported ones only show records.")
                                      .arg(flagError));
        }
    }

private:
    // A field design for each column mdb-export printed (nullopt: left out).
    QList<std::optional<FieldDef>> fieldsFor(const QString &source, const AccessTable &st, CsvTable *csv)
    {
        QList<std::optional<FieldDef>> out;
        QSet<QString> used;
        const QStringList key = st.primaryKey();
        const CurrencyFormat currency = m_file.currency();
        for (int c = 0; c < csv->header.size(); ++c) {
            const QString header = csv->header.at(c);
            const AccessColumn *col = st.column(header);
            std::optional<FieldType> type =
                col != nullptr ? accessFieldType(*col) : std::optional<FieldType>(detectType(filled(*csv, c), header));
            if (!type) {
                m_report.skipped.append(QStringLiteral("%1.%2: an attachment or multi-valued field, which mdb-tools "
                                                       "cannot read.")
                                            .arg(source, header));
                out.append(std::nullopt);
                continue;
            }
            if (col != nullptr && col->type.startsWith(QLatin1String("UUID"), Qt::CaseInsensitive)) {
                for (QStringList &row : csv->rows) {
                    row[c] = QString::fromLatin1(QByteArray::fromHex(row[c].toLatin1())); // -b hex of its "{…}" text
                }
            }
            const QStringList values = filled(*csv, c);
            if (*type == FieldType::Currency
                && !std::all_of(values.cbegin(), values.cend(),
                                [&](const QString &v) { return parseMoney(v, currency.decimals).has_value(); })) {
                type = FieldType::Number;
                m_report.notes.append(QStringLiteral("%1.%2 holds fractions of a cent, so it became a decimal number.")
                                          .arg(source, header));
            }
            if (*type == FieldType::DateTime && !values.isEmpty()
                && std::all_of(values.cbegin(), values.cend(), [](const QString &v) { return v.size() == 10; })) {
                type = FieldType::Date; // all at midnight: exportAccessTable dropped the times
            }
            const QString base = cleanName(header, QStringLiteral("Field%1").arg(c + 1));
            QString name = base;
            for (int n = 2; used.contains(name.toLower()); ++n) {
                name = QStringLiteral("%1_%2").arg(base).arg(n);
            }
            used.insert(name.toLower());
            FieldDef f;
            f.name = name;
            f.type = *type;
            f.description = col != nullptr ? col->description : QString();
            f.primaryKey = col != nullptr && key.contains(col->name, Qt::CaseInsensitive) && f.type != FieldType::File;
            // Yes/No is never empty in Access; a file is added after its record exists.
            f.required = col != nullptr && col->required && !f.primaryKey && f.type != FieldType::File
                         && f.type != FieldType::YesNo;
            if (f.type == FieldType::AutoNumber && !(f.primaryKey && key.size() == 1)) {
                f.type = FieldType::Integer; // QindaBase numbers only a table's single key
            }
            for (const AccessIndex &index : st.indexes) {
                if (!index.primary && index.columns.size() == 1 && f.type != FieldType::File && !f.primaryKey
                    && index.columns.first().compare(header, Qt::CaseInsensitive) == 0) {
                    (index.unique ? f.unique : f.indexed) = true;
                }
            }
            out.append(f);
        }
        return out;
    }

    QStringList multiColumnIndexes(const AccessTable &st, const QString &table, const QHash<QString, QString> &fields)
    {
        QStringList out;
        for (const AccessIndex &index : st.indexes) {
            if (index.primary || index.columns.size() < 2) {
                continue;
            }
            QStringList keys;
            for (int i = 0; i < index.columns.size(); ++i) {
                const QString field = fields.value(index.columns.at(i).toLower());
                if (field.isEmpty()) {
                    keys.clear(); // a column that was left out
                    break;
                }
                keys.append(quoteIdentifier(field) + (index.descending.value(i) ? s(" DESC") : QString()));
            }
            if (!keys.isEmpty()) {
                const QString name = QStringLiteral("qbx_%1_%2").arg(table, cleanName(index.name, s("index")));
                out.append(QStringLiteral("CREATE %1INDEX %2 ON %3(%4)")
                               .arg(index.unique ? s("UNIQUE ") : QString(), quoteIdentifier(name), quoteIdentifier(table),
                                    keys.join(s(", "))));
            }
        }
        return out;
    }

    void withSchema(const QString &source, const AccessTable &st, CsvTable csv)
    {
        TableDef def;
        def.name = uniqueTableName(m_file, source);
        const QList<std::optional<FieldDef>> fields = fieldsFor(source, st, &csv);
        CsvImportPlan plan;
        plan.table = def.name;
        plan.createNew = false;
        plan.addId = false;
        plan.skipBadRows = true;
        Imported done{def.name, {}};
        for (int c = 0; c < fields.size(); ++c) {
            if (const std::optional<FieldDef> &f = fields.at(c)) {
                def.fields.append(*f);
                def.autoIncrement = def.autoIncrement || f->type == FieldType::AutoNumber;
                plan.columns.append({c, f->name, f->type, true, f->primaryKey});
                done.fields.insert(csv.header.at(c).toLower(), f->name);
            }
        }
        SchemaPlan create = planCreateTable(m_file, def);
        create.statements.append(multiColumnIndexes(st, def.name, done.fields));
        Change schemaChange;
        const Result made = create.ok() ? m_file.applyPlan(create, &schemaChange) : Result::failure(create.error);
        if (!made.ok) {
            m_report.skipped.append(QStringLiteral("Table %1: %2").arg(source, made.error));
            return;
        }
        Change dataChange;
        const ImportResult r = importCsv(m_file, csv, plan, nullptr, &dataChange);
        if (!r.ok) {
            m_file.applyChange(schemaChange, true); // never leave an empty half-import behind
            m_report.skipped.append(QStringLiteral("Table %1: %2").arg(source, r.error));
            return;
        }
        finish(source, done, r, {schemaChange, dataChange});
    }

    void withoutSchema(const QString &source, CsvTable csv)
    {
        static const QRegularExpression hex(QStringLiteral("^(?:[0-9A-F]{2}){4,}$"));
        csv.types.resize(csv.header.size());
        for (int c = 0; c < csv.header.size(); ++c) {
            const QStringList values = filled(csv, c);
            if (!values.isEmpty()
                && std::all_of(values.cbegin(), values.cend(), [](const QString &v) { return hex.match(v).hasMatch(); })) {
                csv.types[c] = FieldType::File; // binary as -b hex prints it (an OLE object)
            }
        }
        const CsvImportPlan plan = proposeImport(m_file, csv, uniqueTableName(m_file, source));
        Change schemaChange;
        Change dataChange;
        const ImportResult r = importCsv(m_file, csv, plan, &schemaChange, &dataChange);
        if (!r.ok) {
            m_report.skipped.append(QStringLiteral("Table %1: %2").arg(source, r.error));
            return;
        }
        Imported done{plan.table, {}};
        for (const CsvColumnPlan &c : plan.columns) {
            if (c.include) {
                done.fields.insert(csv.header.value(c.source).toLower(), c.name);
            }
        }
        finish(source, done, r, {schemaChange, dataChange});
    }

    void finish(const QString &source, const Imported &done, const ImportResult &r, const QList<Change> &changes)
    {
        if (r.skipped > 0) {
            m_report.notes.append(QStringLiteral("%1: %2 record(s) did not fit and were left out (%3)")
                                      .arg(done.table)
                                      .arg(r.skipped)
                                      .arg(r.problems.value(0)));
        }
        m_changes->append(changes);
        m_imported.insert(source.toLower(), done);
        m_report.tables.append({source, done.table, r.imported});
    }

    BaseFile &m_file;
    AccessReader &m_reader;
    AccessImportReport &m_report;
    QList<Change> *m_changes;
    std::optional<AccessSchema> m_schema;
    QHash<QString, Imported> m_imported; // Access table, lower-cased
};

} // namespace

QVariantMap AccessImportReport::toMap() const
{
    QVariantList list;
    for (const Table &t : tables) {
        list.append(QVariantMap{{s("source"), t.source}, {s("table"), t.table}, {s("rows"), t.rows}});
    }
    return {{s("ok"), ok},
            {s("error"), error},
            {s("tables"), list},
            {s("relationships"), relationships},
            {s("queries"), queries},
            {s("skipped"), skipped},
            {s("notes"), notes}};
}

AccessImportReport importAccessDatabase(BaseFile &file, AccessReader &reader, const AccessImportOptions &options,
                                        QList<Change> *changes)
{
    AccessImportReport report;
    if (options.tables.isEmpty()) {
        report.error = QStringLiteral("Choose at least one table.");
        return report;
    }
    Run run(file, reader, report, changes);
    run.readSchema();
    for (const QString &source : options.tables) {
        run.table(source);
    }
    run.relationships();
    if (options.queries) {
        run.queries();
    }
    report.ok = !report.tables.isEmpty();
    if (!report.ok) {
        report.error = QStringLiteral("No table could be imported.");
    }
    return report;
}

} // namespace QindaOffice::Base
