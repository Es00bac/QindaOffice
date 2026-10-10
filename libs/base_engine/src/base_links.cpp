// SPDX-License-Identifier: GPL-3.0-or-later
// Linked tables: their sources, guards, copies and refreshes. See
// base_links.h for the contract.
#include "base_links.h"

#include "base_schema_p.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace QindaOffice::Base {

namespace {

QString s(const char *text)
{
    return QString::fromUtf8(text);
}

QString q(const QString &identifier)
{
    return quoteIdentifier(identifier);
}

constexpr qint64 kTextFileLimit = qint64(200) * 1024 * 1024;

QString now()
{
    return QDateTime::currentDateTime().toString(s("yyyy-MM-dd HH:mm:ss"));
}

QString cleanName(QString name, int column, QSet<QString> *used)
{
    name.remove(QLatin1Char('"'));
    name.remove(QLatin1Char('['));
    name.remove(QLatin1Char(']'));
    name.remove(QLatin1Char('`'));
    name = name.simplified().left(64).trimmed();
    if (name.isEmpty() || BaseFile::isSystemName(name)) {
        name = s("Field%1").arg(column + 1);
    }
    QString unique = name;
    for (int n = 2; used->contains(unique.toLower()); ++n) {
        unique = s("%1_%2").arg(name).arg(n);
    }
    used->insert(unique.toLower());
    return unique;
}

// A CSV or Excel table as the copy's fields and storage values: types from
// the source where it states them, else from the text as the CSV import
// detects them; a value that does not fit its type is kept as its text.
LinkCopy fromTable(const CsvTable &table, const CurrencyFormat &currency)
{
    LinkCopy copy;
    QSet<QString> used;
    for (int c = 0; c < table.header.size(); ++c) {
        QStringList values;
        for (const QStringList &row : table.rows) {
            if (!row.value(c).isEmpty()) {
                values.append(row.value(c));
            }
        }
        FieldDef f;
        f.name = cleanName(table.header.at(c), c, &used);
        f.type = table.types.value(c).value_or(detectType(values, table.header.at(c)));
        if (f.type == FieldType::AutoNumber) {
            f.type = FieldType::Integer; // a copy numbers nothing
        }
        copy.fields.append(f);
    }
    for (const QStringList &row : table.rows) {
        QVariantList values;
        for (int c = 0; c < copy.fields.size(); ++c) {
            const QString text = row.value(c);
            const FieldType type = copy.fields.at(c).type;
            if (type == FieldType::File) {
                values.append(text.isEmpty() ? QVariant() : QVariant(QByteArray::fromHex(text.toLatin1())));
                continue;
            }
            const Parsed parsed = parseInput(type, text, currency);
            values.append(parsed.ok ? parsed.value : QVariant(text));
        }
        copy.rows.append(values);
    }
    return copy;
}

// Whether the copy's fields still are the source's (names, types, key).
bool sameFields(const QList<FieldDef> &copy, const QList<FieldDef> &source)
{
    if (copy.size() != source.size()) {
        return false;
    }
    for (int i = 0; i < copy.size(); ++i) {
        if (copy.at(i).name != source.at(i).name || copy.at(i).type != source.at(i).type
            || copy.at(i).primaryKey != source.at(i).primaryKey) {
            return false;
        }
    }
    return true;
}

// Replaces the copy's records (inside the caller's transaction or capture).
Result fillCopy(BaseFile &file, const TableDef &def, const LinkCopy &copy, const LinkSource &source)
{
    Connection &db = file.db();
    Result r = db.run(s("INSERT OR REPLACE INTO qb_meta(key, value) VALUES('qb.quiet', '1')"));
    if (r.ok) {
        r = db.run(s("DELETE FROM %1").arg(q(def.name)));
    }
    QStringList names;
    for (const FieldDef &f : def.fields) {
        names.append(q(f.name));
    }
    if (r.ok && !copy.rows.isEmpty()) {
        QString error;
        Statement insert = db.prepare(s("INSERT INTO %1(%2) VALUES(%3)")
                                          .arg(q(def.name), names.join(s(", ")),
                                               QStringList(names.size(), s("?")).join(s(", "))),
                                      &error);
        if (!insert.isValid()) {
            return Result::failure(error);
        }
        for (const QVariantList &row : copy.rows) {
            insert.reset();
            if (!insert.bindAll(row) || insert.step() == Statement::Step::Error) {
                return Result::failure(s("The copy of %1 could not be written: %2").arg(def.name, insert.errorText()));
            }
        }
    }
    if (r.ok) {
        r = db.run(s("DELETE FROM qb_meta WHERE key = 'qb.quiet'"));
    }
    if (r.ok) {
        const QString json = QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(source.toMap())).toJson(QJsonDocument::Compact));
        r = db.run(s("UPDATE qb_fields SET link_source = ? WHERE table_name = ? AND field_name = ''"), {json, def.name});
    }
    return r;
}

// Notes the problem of a failed refresh in the link itself (not undoable:
// it is the link's state, not the user's data).
void noteProblem(BaseFile &file, const QString &table, LinkSource source, const QString &problem)
{
    source.problem = problem;
    const QString json = QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(source.toMap())).toJson(QJsonDocument::Compact));
    file.db().run(s("UPDATE qb_fields SET link_source = ? WHERE table_name = ? AND field_name = ''"), {json, table});
}

QString relativeTo(const BaseFile &file, const QString &path)
{
    return QDir(QFileInfo(file.path()).absolutePath()).relativeFilePath(QFileInfo(path).absoluteFilePath());
}

// The copy's design over its source: the link, no rules, no lookups.
TableDef copyDesign(const QString &name, const QList<FieldDef> &fields, const LinkSource &source)
{
    TableDef def;
    def.name = name;
    def.autoIncrement = false;
    def.fields = fields;
    def.link = source.toMap();
    return def;
}

} // namespace

QVariantMap LinkSource::toMap() const
{
    return {{s("kind"), kind},           {s("path"), path},         {s("relativePath"), relativePath},
            {s("table"), table},         {s("delimiter"), delimiter}, {s("headerRow"), headerRow},
            {s("refreshed"), refreshed}, {s("rows"), rows},         {s("problem"), problem}};
}

LinkSource LinkSource::fromMap(const QVariantMap &map)
{
    LinkSource l;
    l.kind = map.value(s("kind")).toString();
    l.path = map.value(s("path")).toString();
    l.relativePath = map.value(s("relativePath")).toString();
    l.table = map.value(s("table")).toString();
    l.delimiter = map.value(s("delimiter")).toString();
    l.headerRow = map.value(s("headerRow"), true).toBool();
    l.refreshed = map.value(s("refreshed")).toString();
    l.rows = map.value(s("rows")).toLongLong();
    l.problem = map.value(s("problem")).toString();
    return l;
}

QString LinkSource::description() const
{
    const QString file = QFileInfo(path).fileName();
    if (kind == QLatin1String("qbase")) {
        return s("%1 in %2").arg(table, file);
    }
    if (kind == QLatin1String("xlsx") && !table.isEmpty()) {
        return s("%1 of %2").arg(table, file);
    }
    return file;
}

bool isLinkTrigger(const QString &name)
{
    return name.startsWith(QLatin1String("qbk:"), Qt::CaseInsensitive);
}

QStringList linkGuardStatements(const TableDef &table)
{
    if (table.link.isEmpty()) {
        return {};
    }
    const QString message = s("%1 is linked to %2; change the source, then refresh the link.")
                                .arg(table.name, LinkSource::fromMap(table.link).description());
    QStringList out;
    for (const auto &[op, id] : {std::pair{"INSERT", "i"}, std::pair{"UPDATE", "u"}, std::pair{"DELETE", "d"}}) {
        out.append(s("CREATE TRIGGER %1 BEFORE %2 ON %3 FOR EACH ROW WHEN NOT EXISTS (SELECT 1 FROM qb_meta WHERE key = "
                     "'qb.quiet') BEGIN SELECT RAISE(ABORT, %4); END")
                       .arg(q(s("qbk:%1:%2").arg(table.name, QString::fromLatin1(id))), QString::fromLatin1(op),
                            q(table.name), quoteLiteral(message)));
    }
    return out;
}

QString linkPath(const BaseFile &file, const LinkSource &source)
{
    if (!source.relativePath.isEmpty()) {
        const QString relative = QDir(QFileInfo(file.path()).absolutePath()).filePath(source.relativePath);
        if (QFileInfo::exists(relative)) {
            return QFileInfo(relative).absoluteFilePath();
        }
    }
    return QFileInfo::exists(source.path) ? source.path : QString();
}

QStringList linkableTables(const QString &path, QString *error)
{
    const std::unique_ptr<BaseFile> source = BaseFile::open(path, error);
    return source ? source->tableNames() : QStringList();
}

std::optional<LinkCopy> readLinkSource(const BaseFile &file, const LinkSource &source, const LinkReader &xlsx,
                                       QString *error)
{
    const QString path = linkPath(file, source);
    if (path.isEmpty()) {
        *error = s("%1 was not found.").arg(source.path);
        return std::nullopt;
    }
    if (QFileInfo(path).absoluteFilePath() == QFileInfo(file.path()).absoluteFilePath()) {
        *error = s("A table cannot be linked to the database it is in.");
        return std::nullopt;
    }
    if (source.kind == QLatin1String("qbase")) {
        const std::unique_ptr<BaseFile> other = BaseFile::open(path, error);
        if (!other) {
            return std::nullopt;
        }
        const std::optional<TableDef> def = readTable(*other, source.table, error);
        if (!def) {
            return std::nullopt;
        }
        LinkCopy copy;
        QStringList columns;
        const bool singleKey = def->primaryKey().size() == 1;
        for (const FieldDef &f : def->fields) {
            FieldDef c;
            c.name = f.name;
            c.type = f.type == FieldType::AutoNumber ? FieldType::Integer : f.type;
            c.caption = f.caption;
            c.description = f.description;
            c.primaryKey = f.primaryKey && singleKey && !f.calculated;
            copy.fields.append(c);
            columns.append(q(f.name));
        }
        copy.rows = other->db().rows(s("SELECT %1 FROM %2").arg(columns.join(s(", ")), q(def->name)), {}, error);
        if (!error->isEmpty()) {
            return std::nullopt;
        }
        return copy;
    }
    std::optional<CsvTable> table;
    if (source.kind == QLatin1String("csv")) {
        QFile f(path);
        if (f.size() > kTextFileLimit || !f.open(QIODevice::ReadOnly)) {
            *error = f.size() > kTextFileLimit ? s("%1 is larger than 200 MB.").arg(source.path) : f.errorString();
            return std::nullopt;
        }
        table = parseCsv(f.readAll(), source.delimiter.isEmpty() ? QChar() : source.delimiter.at(0), source.headerRow);
    } else if (source.kind == QLatin1String("xlsx")) {
        if (!xlsx) {
            *error = s("Excel links are refreshed by QindaBase's window.");
            return std::nullopt;
        }
        table = xlsx(source, path, error);
    } else {
        *error = s("A link reads a QindaBase or SQLite database, a CSV file or an Excel sheet.");
    }
    if (!table) {
        return std::nullopt;
    }
    return fromTable(*table, file.currency());
}

LinkResult linkTable(BaseFile &file, const QString &name, const LinkSource &input, const LinkReader &xlsx)
{
    LinkResult out;
    out.table = name.trimmed();
    LinkSource source = input;
    source.path = QFileInfo(source.path).absoluteFilePath();
    source.relativePath = relativeTo(file, source.path);
    if (!file.hasSystemTables()) {
        out.error = s("A link is kept in QindaBase's system tables; add them first.");
        return out;
    }
    QString error;
    const std::optional<LinkCopy> copy = readLinkSource(file, source, xlsx, &error);
    if (!copy) {
        out.error = error;
        return out;
    }
    source.refreshed = now();
    source.rows = copy->rows.size();
    const TableDef def = copyDesign(out.table, copy->fields, source);
    SchemaPlan plan = planCreateTable(file, def);
    plan.label = s("Link %1 to %2").arg(out.table, source.description());
    Change schema;
    const Result made = plan.ok() ? file.applyPlan(plan, &schema) : Result::failure(plan.error);
    if (!made.ok) {
        out.error = made.error;
        return out;
    }
    Change data;
    const Result filled = file.capture(plan.label, [&] { return fillCopy(file, def, *copy, source); }, &data);
    if (!filled.ok) {
        file.applyChange(schema, true); // nothing half-linked stays behind
        out.error = filled.error;
        return out;
    }
    out.ok = true;
    out.rows = copy->rows.size();
    out.changes = {schema, data};
    return out;
}

LinkResult refreshLink(BaseFile &file, const QString &table, const LinkReader &xlsx, bool undoable)
{
    LinkResult out;
    out.table = table;
    const std::optional<TableDef> before = readTable(file, table, &out.error);
    if (!before) {
        return out;
    }
    if (before->link.isEmpty()) {
        out.error = s("%1 is not a linked table.").arg(table);
        return out;
    }
    LinkSource source = LinkSource::fromMap(before->link);
    QString error;
    const std::optional<LinkCopy> copy = readLinkSource(file, source, xlsx, &error);
    if (!copy) {
        out.error = error;
        if (!file.readOnly()) {
            noteProblem(file, before->name, source, error);
        }
        return out;
    }
    source.refreshed = now();
    source.rows = copy->rows.size();
    source.problem.clear();
    TableDef def = *before;
    if (!sameFields(before->fields, copy->fields)) {
        // The source's fields changed: the copy is made again to match.
        out.redesigned = true;
        def = copyDesign(before->name, copy->fields, source);
        SchemaPlan plan;
        plan.label = s("Refresh %1").arg(before->name);
        for (const QString &trigger : before->derivedTriggers) {
            plan.statements.append(s("DROP TRIGGER IF EXISTS %1").arg(q(trigger)));
        }
        plan.statements.append(s("DROP TABLE %1").arg(q(before->name)));
        QString sqlError;
        plan.statements.append(createTableSql(def, def.name, file.currency(), &sqlError));
        plan.statements.append(s("DELETE FROM qb_fields WHERE table_name = %1").arg(quoteLiteral(def.name)));
        for (const FieldDef &f : def.fields) {
            plan.statements.append(detail::metaInsert(def.name, f));
        }
        plan.statements.append(detail::metaTableInsert(def));
        plan.statements.append(linkGuardStatements(def));
        Change schema;
        const Result made = sqlError.isEmpty() ? file.applyPlan(plan, &schema) : Result::failure(sqlError);
        if (!made.ok) {
            out.error = made.error;
            return out;
        }
        out.changes.append(schema);
    }
    Result filled;
    if (undoable) {
        Change data;
        filled = file.capture(s("Refresh %1").arg(def.name), [&] { return fillCopy(file, def, *copy, source); }, &data);
        out.changes.append(data);
    } else {
        // At open: one transaction, nothing to undo.
        filled = file.db().exec(s("BEGIN IMMEDIATE"));
        if (filled.ok) {
            filled = fillCopy(file, def, *copy, source);
            file.db().exec(filled.ok ? s("COMMIT") : s("ROLLBACK"));
        }
    }
    out.ok = filled.ok;
    out.error = filled.error;
    out.rows = copy->rows.size();
    return out;
}

QList<LinkResult> refreshLinks(BaseFile &file, const LinkReader &xlsx)
{
    QList<LinkResult> out;
    for (const auto &[table, source] : linkedTables(file)) {
        if (file.readOnly()) {
            out.append({false, table, s("The database is read-only, so %1 shows its copy of %2.").arg(table, source.refreshed),
                        0, false, {}});
            continue;
        }
        out.append(refreshLink(file, table, xlsx, false));
    }
    return out;
}

SchemaPlan planUnlinkTable(const BaseFile &file, const QString &table)
{
    SchemaPlan plan;
    const std::optional<TableDef> before = readTable(file, table, &plan.error);
    if (!before) {
        return plan;
    }
    if (before->link.isEmpty()) {
        plan.error = s("%1 is not a linked table.").arg(table);
        return plan;
    }
    TableDef after = *before;
    after.link.clear();
    plan = planAlterTable(file, *before, after);
    plan.label = s("Convert %1 to a local table").arg(table);
    plan.notes.append(s("%1 keeps its records and can be edited; it no longer follows %2.")
                          .arg(table, LinkSource::fromMap(before->link).description()));
    return plan;
}

SchemaPlan planRelinkTable(const BaseFile &file, const QString &table, const QString &path)
{
    SchemaPlan plan;
    const std::optional<TableDef> before = readTable(file, table, &plan.error);
    if (!before) {
        return plan;
    }
    if (before->link.isEmpty()) {
        plan.error = s("%1 is not a linked table.").arg(table);
        return plan;
    }
    if (!QFileInfo::exists(path)) {
        plan.error = s("%1 was not found.").arg(path);
        return plan;
    }
    LinkSource source = LinkSource::fromMap(before->link);
    source.path = QFileInfo(path).absoluteFilePath();
    source.relativePath = relativeTo(file, source.path);
    source.problem.clear();
    TableDef after = *before;
    after.link = source.toMap();
    plan = planAlterTable(file, *before, after);
    plan.label = s("Relink %1 to %2").arg(table, source.description());
    return plan;
}

QList<QPair<QString, LinkSource>> linkedTables(const BaseFile &file)
{
    QList<QPair<QString, LinkSource>> out;
    if (!file.hasSystemTables() || !detail::systemFieldColumns(file).contains(s("link_source"))) {
        return out;
    }
    for (const QVariantList &row : file.db().rows(s("SELECT table_name, link_source FROM qb_fields WHERE field_name = '' "
                                                     "AND link_source IS NOT NULL ORDER BY table_name"))) {
        const QVariantMap map = QJsonDocument::fromJson(row.value(1).toString().toUtf8()).object().toVariantMap();
        if (!map.isEmpty() && file.tableExists(row.value(0).toString())) {
            out.append({row.value(0).toString(), LinkSource::fromMap(map)});
        }
    }
    return out;
}

} // namespace QindaOffice::Base
