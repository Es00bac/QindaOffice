// SPDX-License-Identifier: GPL-3.0-or-later
// Table designs: validation and schema plans (create, alter/rebuild, drop,
// relationships). See base_schema.h for how a change is planned.
#include "base_schema_p.h"

#include <QSet>

namespace QindaOffice::Base {

namespace {

QString q(const QString &identifier)
{
    return quoteIdentifier(identifier);
}

QString refSql(RefAction action)
{
    switch (action) {
    case RefAction::Cascade: return QStringLiteral("CASCADE");
    case RefAction::SetNull: return QStringLiteral("SET NULL");
    case RefAction::Restrict: return QStringLiteral("RESTRICT");
    case RefAction::NoAction: break;
    }
    return {};
}

QString defaultSql(const FieldDef &field, const CurrencyFormat &currency, QString *error)
{
    const QString text = field.defaultValue.trimmed();
    if (text.isEmpty()) {
        return {};
    }
    if (text.compare(QLatin1String("Date()"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("(date('now','localtime'))");
    }
    if (text.compare(QLatin1String("Now()"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("(datetime('now','localtime'))");
    }
    if (text.startsWith(QLatin1Char('='))) {
        return QLatin1Char('(') + text.mid(1) + QLatin1Char(')');
    }
    const Parsed parsed = parseInput(field.type, field.defaultValue, currency);
    if (!parsed.ok) {
        *error = QStringLiteral("The default value of %1 %2.").arg(field.name, parsed.error);
        return {};
    }
    return sqlLiteral(parsed.value);
}

bool integerLike(FieldType type)
{
    return type == FieldType::Integer || type == FieldType::AutoNumber;
}

} // namespace

namespace detail {

QString expectedStorage(FieldType type)
{
    switch (type) {
    case FieldType::AutoNumber:
    case FieldType::Integer:
    case FieldType::Currency:
    case FieldType::YesNo:
        return QStringLiteral("'integer'");
    case FieldType::Number:
        return QStringLiteral("'integer','real'");
    case FieldType::Other:
        return QStringLiteral("'blob','text','integer','real'");
    default:
        return QStringLiteral("'text'");
    }
}

// Converting between these loses information → back up first.
bool lossy(FieldType from, FieldType to)
{
    if (from == to) {
        return false;
    }
    if (to == FieldType::YesNo) {
        return true;
    }
    if (integerLike(to) && (from == FieldType::Number || from == FieldType::Currency)) {
        return true;
    }
    return from == FieldType::DateTime && to == FieldType::Date;
}

QString metaInsert(const QString &table, const FieldDef &f)
{
    return QStringLiteral("INSERT OR REPLACE INTO qb_fields(table_name, field_name, type, caption, description, "
                          "lookup_table, lookup_field, lookup_display) VALUES(%1, %2, %3, %4, %5, %6, %7, %8)")
        .arg(quoteLiteral(table), quoteLiteral(f.name), quoteLiteral(typeId(f.type)),
             f.caption.isEmpty() ? QStringLiteral("NULL") : quoteLiteral(f.caption),
             f.description.isEmpty() ? QStringLiteral("NULL") : quoteLiteral(f.description),
             f.lookup.isSet() ? quoteLiteral(f.lookup.table) : QStringLiteral("NULL"),
             f.lookup.isSet() ? quoteLiteral(f.lookup.valueField) : QStringLiteral("NULL"),
             f.lookup.isSet() && !f.lookup.displayField.isEmpty() ? quoteLiteral(f.lookup.displayField)
                                                                  : QStringLiteral("NULL"));
}

void appendFieldIndexes(QStringList *out, const TableDef &table, const QString &name)
{
    for (const FieldDef &f : table.fields) {
        const QString index = q(QStringLiteral("qbx_%1_%2").arg(name, f.name));
        if (f.unique && !(f.primaryKey && table.primaryKey().size() == 1)) {
            out->append(QStringLiteral("CREATE UNIQUE INDEX %1 ON %2(%3)").arg(index, q(name), q(f.name)));
        } else if (f.indexed && !f.primaryKey) {
            out->append(QStringLiteral("CREATE INDEX %1 ON %2(%3)").arg(index, q(name), q(f.name)));
        }
    }
}

// Other tables' foreign keys that point at `table`.
QList<Relationship> incoming(const BaseFile &file, const QString &table)
{
    QList<Relationship> out;
    for (const Relationship &r : readRelationships(file)) {
        if (r.parentTable.compare(table, Qt::CaseInsensitive) == 0
            && r.childTable.compare(table, Qt::CaseInsensitive) != 0) {
            out.append(r);
        }
    }
    return out;
}

QString validateTable(const BaseFile &file, const TableDef &table, const QString &existingName)
{
    if (QString e = validateName(table.name, QStringLiteral("table")); !e.isEmpty()) {
        return e;
    }
    if (BaseFile::isSystemName(table.name)) {
        return QStringLiteral("Table names starting with qb_ or sqlite_ are reserved.");
    }
    if (table.name.compare(existingName, Qt::CaseInsensitive) != 0 && file.tableExists(table.name)) {
        return QStringLiteral("There is already a table named %1.").arg(table.name);
    }
    if (table.name.compare(existingName, Qt::CaseInsensitive) != 0
        && !file.db().scalar(QStringLiteral("SELECT name FROM sqlite_schema WHERE name=? COLLATE NOCASE"), {table.name})
                .toString()
                .isEmpty()) {
        return QStringLiteral("The name %1 is already used by a view or index.").arg(table.name);
    }
    if (table.complexKeys) {
        return QStringLiteral("%1 has a relationship over several fields, which QindaBase cannot redesign; use the "
                              "SQL view.")
            .arg(table.name);
    }
    if (table.fields.isEmpty()) {
        return QStringLiteral("A table needs at least one field.");
    }
    QSet<QString> names;
    int autoNumbers = 0;
    int keys = 0;
    for (const FieldDef &f : table.fields) {
        if (QString e = validateName(f.name, QStringLiteral("field")); !e.isEmpty()) {
            return e;
        }
        if (names.contains(f.name.toLower())) {
            return QStringLiteral("Two fields are named %1.").arg(f.name);
        }
        names.insert(f.name.toLower());
        autoNumbers += f.type == FieldType::AutoNumber ? 1 : 0;
        keys += f.primaryKey ? 1 : 0;
        if (f.type == FieldType::AutoNumber && !f.primaryKey) {
            return QStringLiteral("The AutoNumber field %1 must be the primary key.").arg(f.name);
        }
        if (f.type == FieldType::Other && f.originalName.isEmpty()) {
            return QStringLiteral("Choose a type for %1.").arg(f.name);
        }
        if (f.lookup.isSet()) {
            const bool self = f.lookup.table.compare(table.name, Qt::CaseInsensitive) == 0;
            if (!self && !file.tableExists(f.lookup.table)) {
                return QStringLiteral("The lookup of %1 names a missing table %2.").arg(f.name, f.lookup.table);
            }
            if (!self && !fieldTypes(file, f.lookup.table).contains(f.lookup.valueField)) {
                return QStringLiteral("%1 has no field %2 to look up.").arg(f.lookup.table, f.lookup.valueField);
            }
        }
    }
    if (autoNumbers > 1) {
        return QStringLiteral("A table can have only one AutoNumber field.");
    }
    if (autoNumbers == 1 && keys > 1) {
        return QStringLiteral("An AutoNumber key must be the only key field.");
    }
    if (table.withoutRowid && keys == 0) {
        return QStringLiteral("This table needs a primary key.");
    }
    for (const Relationship &r : table.relationships) {
        if (table.fieldIndex(r.childField) < 0) {
            return QStringLiteral("The relationship names a missing field %1.").arg(r.childField);
        }
        const bool self = r.parentTable.compare(table.name, Qt::CaseInsensitive) == 0;
        std::optional<TableDef> parent = self ? std::optional<TableDef>(table) : readTable(file, r.parentTable);
        if (!parent) {
            return QStringLiteral("The relationship names a missing table %1.").arg(r.parentTable);
        }
        const FieldDef *key = parent->field(r.parentField);
        if (key == nullptr) {
            return QStringLiteral("%1 has no field %2.").arg(r.parentTable, r.parentField);
        }
        if (!(key->primaryKey && parent->primaryKey().size() == 1) && !key->unique) {
            return QStringLiteral("%1.%2 must be the primary key or unique to be on the one side of a relationship.")
                .arg(r.parentTable, r.parentField);
        }
        const FieldType childType = table.field(r.childField)->type;
        const bool compatible = childType == key->type || (integerLike(childType) && integerLike(key->type));
        if (!compatible) {
            return QStringLiteral("%1 (%2) and %3.%4 (%5) have different types.")
                .arg(r.childField, typeLabel(childType), r.parentTable, r.parentField, typeLabel(key->type));
        }
        if (r.onDelete == RefAction::SetNull && table.field(r.childField)->required) {
            return QStringLiteral("%1 is required, so deleting a %2 record cannot set it to empty.")
                .arg(r.childField, r.parentTable);
        }
    }
    return {};
}

} // namespace detail

using namespace detail;

QString validateName(const QString &name, const QString &what)
{
    if (name.trimmed().isEmpty()) {
        return QStringLiteral("Every %1 needs a name.").arg(what);
    }
    if (name != name.trimmed()) {
        return QStringLiteral("The %1 name \"%2\" starts or ends with a space.").arg(what, name);
    }
    if (name.size() > 64) {
        return QStringLiteral("The %1 name %2 is longer than 64 characters.").arg(what, name.left(20));
    }
    for (const QChar ch : name) {
        if (ch.category() == QChar::Other_Control || ch == QLatin1Char('"') || ch == QLatin1Char('[')
            || ch == QLatin1Char(']') || ch == QLatin1Char('`')) {
            return QStringLiteral("The %1 name %2 contains a character that is not allowed (\" [ ] ` or a control "
                                  "character).")
                .arg(what, name);
        }
    }
    return {};
}

QString columnSql(const FieldDef &field, bool singlePrimaryKey, bool autoIncrement, const CurrencyFormat &currency,
                  QString *error)
{
    QString declared = declaredType(field.type);
    if (!field.declared.isEmpty() && field.type != FieldType::AutoNumber) {
        const bool rowidKey = singlePrimaryKey && field.primaryKey
                              && field.declared.trimmed().compare(QLatin1String("INTEGER"), Qt::CaseInsensitive) == 0;
        if (typeFromDeclared(field.declared, rowidKey) == field.type) {
            declared = field.declared; // a foreign file's own spelling survives a rebuild
        }
    }
    QString sql = q(field.name) + QLatin1Char(' ') + declared;
    if (field.primaryKey && singlePrimaryKey) {
        sql += QStringLiteral(" PRIMARY KEY");
        if (field.type == FieldType::AutoNumber) {
            if (autoIncrement) {
                sql += QStringLiteral(" AUTOINCREMENT");
            }
        } else {
            sql += QStringLiteral(" NOT NULL");
        }
    } else if (field.required || field.primaryKey) {
        sql += QStringLiteral(" NOT NULL");
    }
    QString problem;
    const QString dflt = defaultSql(field, currency, &problem);
    if (!problem.isEmpty()) {
        *error = problem;
        return {};
    }
    if (!dflt.isEmpty()) {
        sql += QStringLiteral(" DEFAULT ") + dflt;
    }
    return sql;
}

QString createTableSql(const TableDef &table, const QString &name, const CurrencyFormat &currency, QString *error)
{
    const QStringList keys = table.primaryKey();
    QStringList parts;
    for (const FieldDef &f : table.fields) {
        QString problem;
        const QString column = columnSql(f, keys.size() == 1, table.autoIncrement, currency, &problem);
        if (!problem.isEmpty()) {
            *error = problem;
            return {};
        }
        parts.append(QStringLiteral("  ") + column);
    }
    if (keys.size() > 1) {
        QStringList quoted;
        for (const QString &k : keys) {
            quoted.append(q(k));
        }
        parts.append(QStringLiteral("  PRIMARY KEY(%1)").arg(quoted.join(QStringLiteral(", "))));
    }
    for (const Relationship &r : table.relationships) {
        QString fk = QStringLiteral("  FOREIGN KEY(%1) REFERENCES %2(%3)")
                         .arg(q(r.childField), q(r.parentTable.compare(table.name, Qt::CaseInsensitive) == 0
                                                     ? name
                                                     : r.parentTable),
                              q(r.parentField));
        if (r.onDelete != RefAction::NoAction) {
            fk += QStringLiteral(" ON DELETE ") + refSql(r.onDelete);
        }
        if (r.onUpdate != RefAction::NoAction) {
            fk += QStringLiteral(" ON UPDATE ") + refSql(r.onUpdate);
        }
        parts.append(fk);
    }
    return QStringLiteral("CREATE TABLE %1 (\n%2\n)%3")
        .arg(q(name), parts.join(QStringLiteral(",\n")),
             table.withoutRowid ? QStringLiteral(" WITHOUT ROWID") : QString());
}

SchemaPlan planCreateTable(const BaseFile &file, const TableDef &input)
{
    SchemaPlan plan;
    TableDef table = input;
    table.originalName.clear();
    plan.label = QStringLiteral("Create table %1").arg(table.name);
    if (QString e = validateTable(file, table, QString()); !e.isEmpty()) {
        plan.error = e;
        return plan;
    }
    QString error;
    const QString create = createTableSql(table, table.name, file.currency(), &error);
    if (!error.isEmpty()) {
        plan.error = error;
        return plan;
    }
    plan.statements.append(create);
    appendFieldIndexes(&plan.statements, table, table.name);
    if (file.hasSystemTables()) {
        for (const FieldDef &f : table.fields) {
            plan.statements.append(metaInsert(table.name, f));
        }
    }
    plan.notes.append(QStringLiteral("Creates %1 with %2 field(s).").arg(table.name).arg(table.fields.size()));
    return plan;
}

SchemaPlan planDropTable(const BaseFile &file, const QString &table)
{
    SchemaPlan plan;
    plan.label = QStringLiteral("Delete table %1").arg(table);
    plan.destructive = true;
    if (!file.tableExists(table)) {
        plan.error = QStringLiteral("There is no table named %1.").arg(table);
        return plan;
    }
    const QList<Relationship> refs = incoming(file, table);
    if (!refs.isEmpty()) {
        plan.error = QStringLiteral("%1 is on the one side of a relationship with %2; delete the relationship first.")
                         .arg(table, refs.first().childTable);
        return plan;
    }
    plan.statements.append(QStringLiteral("DROP TABLE %1").arg(q(table)));
    if (file.hasSystemTables()) {
        plan.statements.append(QStringLiteral("DELETE FROM qb_fields WHERE table_name = %1").arg(quoteLiteral(table)));
    }
    const qint64 rows = file.db().scalar(QStringLiteral("SELECT count(*) FROM %1").arg(q(table))).toLongLong();
    plan.notes.append(QStringLiteral("Deletes %1 and its %2 record(s). A backup is made first.").arg(table).arg(rows));
    return plan;
}

SchemaPlan planAddRelationship(const BaseFile &file, const Relationship &relationship)
{
    std::optional<TableDef> before = readTable(file, relationship.childTable);
    if (!before) {
        SchemaPlan plan;
        plan.error = QStringLiteral("There is no table named %1.").arg(relationship.childTable);
        return plan;
    }
    TableDef after = *before;
    for (const Relationship &r : after.relationships) {
        if (r.childField.compare(relationship.childField, Qt::CaseInsensitive) == 0) {
            SchemaPlan plan;
            plan.error = QStringLiteral("%1.%2 already belongs to a relationship; edit that one instead.")
                             .arg(relationship.childTable, relationship.childField);
            return plan;
        }
    }
    Relationship rel = relationship;
    rel.childTable = before->name;
    after.relationships.append(rel);
    const int fi = after.fieldIndex(rel.childField);
    if (fi >= 0 && !after.fields[fi].primaryKey && !after.fields[fi].unique) {
        after.fields[fi].indexed = true; // lookups and cascades stay fast
    }
    SchemaPlan plan = planAlterTable(file, *before, after);
    plan.label = QStringLiteral("Relationship %1.%2 → %3.%4")
                     .arg(rel.childTable, rel.childField, rel.parentTable, rel.parentField);
    return plan;
}

SchemaPlan planRemoveRelationship(const BaseFile &file, const Relationship &relationship)
{
    std::optional<TableDef> before = readTable(file, relationship.childTable);
    SchemaPlan plan;
    if (!before) {
        plan.error = QStringLiteral("There is no table named %1.").arg(relationship.childTable);
        return plan;
    }
    TableDef after = *before;
    const qsizetype removed =
        after.relationships.removeIf([&](const Relationship &r) { return r.sameLink(relationship); });
    if (removed == 0) {
        plan.error = QStringLiteral("There is no such relationship.");
        return plan;
    }
    plan = planAlterTable(file, *before, after);
    plan.label = QStringLiteral("Remove relationship %1.%2 → %3.%4")
                     .arg(relationship.childTable, relationship.childField, relationship.parentTable,
                          relationship.parentField);
    return plan;
}

} // namespace QindaOffice::Base
