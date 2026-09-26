// SPDX-License-Identifier: GPL-3.0-or-later
// Table designs: maps for QML, and reading a table back from SQLite
// introspection (table_xinfo, index_list, foreign_key_list) plus qb_fields.
#include "base_schema.h"

#include <QRegularExpression>

namespace QindaOffice::Base {

namespace {

QString s(const char *text)
{
    return QString::fromLatin1(text);
}

RefAction actionFromSql(const QString &sql)
{
    const QString t = sql.trimmed().toUpper();
    if (t == QLatin1String("CASCADE")) {
        return RefAction::Cascade;
    }
    if (t == QLatin1String("SET NULL")) {
        return RefAction::SetNull;
    }
    if (t == QLatin1String("RESTRICT")) {
        return RefAction::Restrict;
    }
    return RefAction::NoAction;
}

QString unwrapParens(QString text)
{
    text = text.trimmed();
    while (text.startsWith(QLatin1Char('(')) && text.endsWith(QLatin1Char(')'))) {
        text = text.mid(1, text.size() - 2).trimmed();
    }
    return text;
}

} // namespace

QString refActionId(RefAction action)
{
    switch (action) {
    case RefAction::Restrict: return s("restrict");
    case RefAction::Cascade: return s("cascade");
    case RefAction::SetNull: return s("setNull");
    case RefAction::NoAction: break;
    }
    return s("noAction");
}

RefAction refActionFromId(const QString &id)
{
    if (id == QLatin1String("restrict")) {
        return RefAction::Restrict;
    }
    if (id == QLatin1String("cascade")) {
        return RefAction::Cascade;
    }
    if (id == QLatin1String("setNull")) {
        return RefAction::SetNull;
    }
    return RefAction::NoAction;
}

// ---- maps ------------------------------------------------------------------

QVariantMap FieldDef::toMap() const
{
    return {{s("name"), name},
            {s("originalName"), originalName},
            {s("type"), typeId(type)},
            {s("typeLabel"), typeLabel(type)},
            {s("declared"), declared},
            {s("primaryKey"), primaryKey},
            {s("required"), required},
            {s("defaultValue"), defaultValue},
            {s("unique"), unique},
            {s("indexed"), indexed},
            {s("caption"), caption},
            {s("description"), description},
            {s("lookupTable"), lookup.table},
            {s("lookupField"), lookup.valueField},
            {s("lookupDisplay"), lookup.displayField}};
}

FieldDef FieldDef::fromMap(const QVariantMap &map)
{
    FieldDef f;
    f.name = map.value(s("name")).toString().trimmed();
    f.originalName = map.value(s("originalName")).toString();
    f.type = typeFromId(map.value(s("type")).toString()).value_or(FieldType::Text);
    f.declared = map.value(s("declared")).toString();
    f.primaryKey = map.value(s("primaryKey")).toBool();
    f.required = map.value(s("required")).toBool();
    f.defaultValue = map.value(s("defaultValue")).toString();
    f.unique = map.value(s("unique")).toBool();
    f.indexed = map.value(s("indexed")).toBool();
    f.caption = map.value(s("caption")).toString();
    f.description = map.value(s("description")).toString();
    f.lookup.table = map.value(s("lookupTable")).toString();
    f.lookup.valueField = map.value(s("lookupField")).toString();
    f.lookup.displayField = map.value(s("lookupDisplay")).toString();
    return f;
}

QString Relationship::id() const
{
    return childTable + QLatin1Char('.') + childField + QLatin1Char('>') + parentTable + QLatin1Char('.') + parentField;
}

QVariantMap Relationship::toMap() const
{
    return {{s("id"), id()},
            {s("childTable"), childTable},
            {s("childField"), childField},
            {s("parentTable"), parentTable},
            {s("parentField"), parentField},
            {s("onDelete"), refActionId(onDelete)},
            {s("onUpdate"), refActionId(onUpdate)}};
}

Relationship Relationship::fromMap(const QVariantMap &map)
{
    Relationship r;
    r.childTable = map.value(s("childTable")).toString();
    r.childField = map.value(s("childField")).toString();
    r.parentTable = map.value(s("parentTable")).toString();
    r.parentField = map.value(s("parentField")).toString();
    r.onDelete = refActionFromId(map.value(s("onDelete")).toString());
    r.onUpdate = refActionFromId(map.value(s("onUpdate")).toString());
    return r;
}

bool Relationship::sameLink(const Relationship &other) const
{
    return childTable.compare(other.childTable, Qt::CaseInsensitive) == 0
           && childField.compare(other.childField, Qt::CaseInsensitive) == 0
           && parentTable.compare(other.parentTable, Qt::CaseInsensitive) == 0
           && parentField.compare(other.parentField, Qt::CaseInsensitive) == 0;
}

int TableDef::fieldIndex(const QString &name) const
{
    for (int i = 0; i < fields.size(); ++i) {
        if (fields.at(i).name.compare(name, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return -1;
}

const FieldDef *TableDef::field(const QString &name) const
{
    const int i = fieldIndex(name);
    return i < 0 ? nullptr : &fields.at(i);
}

QStringList TableDef::primaryKey() const
{
    QStringList out;
    for (const FieldDef &f : fields) {
        if (f.primaryKey) {
            out.append(f.name);
        }
    }
    return out;
}

QVariantMap TableDef::toMap() const
{
    QVariantList fieldList;
    for (const FieldDef &f : fields) {
        fieldList.append(f.toMap());
    }
    QVariantList relationList;
    for (const Relationship &r : relationships) {
        relationList.append(r.toMap());
    }
    return {{s("name"), name},
            {s("originalName"), originalName},
            {s("fields"), fieldList},
            {s("relationships"), relationList},
            {s("autoIncrement"), autoIncrement},
            {s("withoutRowid"), withoutRowid},
            {s("complexKeys"), complexKeys},
            {s("otherIndexSql"), otherIndexSql},
            {s("triggerSql"), triggerSql}};
}

TableDef TableDef::fromMap(const QVariantMap &map)
{
    TableDef t;
    t.name = map.value(s("name")).toString().trimmed();
    t.originalName = map.value(s("originalName")).toString();
    for (const QVariant &v : map.value(s("fields")).toList()) {
        t.fields.append(FieldDef::fromMap(v.toMap()));
    }
    for (const QVariant &v : map.value(s("relationships")).toList()) {
        t.relationships.append(Relationship::fromMap(v.toMap()));
    }
    t.autoIncrement = map.value(s("autoIncrement"), true).toBool();
    t.withoutRowid = map.value(s("withoutRowid")).toBool();
    t.complexKeys = map.value(s("complexKeys")).toBool();
    t.otherIndexSql = map.value(s("otherIndexSql")).toStringList();
    t.triggerSql = map.value(s("triggerSql")).toStringList();
    return t;
}

// ---- reading ---------------------------------------------------------------

QString defaultToEditText(const QString &sqlDefault, FieldType type, const CurrencyFormat &currency)
{
    if (sqlDefault.isEmpty()) {
        return {};
    }
    const QString inner = unwrapParens(sqlDefault);
    const QString upper = inner.toUpper();
    if (upper == QLatin1String("NULL")) {
        return {};
    }
    if (upper == QLatin1String("CURRENT_DATE") || upper.startsWith(QLatin1String("DATE('NOW'"))) {
        return s("Date()");
    }
    if (upper == QLatin1String("CURRENT_TIMESTAMP") || upper.startsWith(QLatin1String("DATETIME('NOW'"))) {
        return s("Now()");
    }
    if (inner.startsWith(QLatin1Char('\'')) && inner.endsWith(QLatin1Char('\'')) && inner.size() >= 2) {
        QString text = inner.mid(1, inner.size() - 2);
        text.replace(QStringLiteral("''"), QStringLiteral("'"));
        return text;
    }
    static const QRegularExpression number(QStringLiteral("^[-+]?\\d+(\\.\\d*)?([eE][-+]?\\d+)?$"));
    if (number.match(inner).hasMatch()) {
        QVariant stored;
        bool isInt = false;
        const qint64 n = inner.toLongLong(&isInt);
        stored = isInt ? QVariant::fromValue(n) : QVariant(inner.toDouble());
        return editText(type, stored, currency);
    }
    return QLatin1Char('=') + inner; // an expression, kept verbatim
}

std::optional<TableDef> readTable(const BaseFile &file, const QString &name, QString *error)
{
    const Connection &db = file.db();
    const QList<QVariantList> master =
        db.rows(QStringLiteral("SELECT name, sql FROM sqlite_schema WHERE type='table' AND name=? COLLATE NOCASE"), {name});
    if (master.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("There is no table named %1.").arg(name);
        }
        return std::nullopt;
    }
    TableDef table;
    table.name = master.first().value(0).toString();
    table.originalName = table.name;
    const QString sql = master.first().value(1).toString().toUpper();
    table.autoIncrement = sql.contains(QLatin1String("AUTOINCREMENT"));
    table.withoutRowid = sql.contains(QLatin1String("WITHOUT ROWID"));
    const CurrencyFormat currency = file.currency();

    // Metadata QindaBase keeps for the columns (captions, lookups, type ids).
    QHash<QString, QVariantList> meta;
    if (file.hasSystemTables()) {
        for (const QVariantList &row :
             db.rows(QStringLiteral("SELECT field_name, type, caption, description, lookup_table, lookup_field, "
                                    "lookup_display FROM qb_fields WHERE table_name=?"),
                     {table.name})) {
            meta.insert(row.value(0).toString().toLower(), row);
        }
    }
    const QList<QVariantList> columns = db.rows(QStringLiteral("SELECT name, type, \"notnull\", dflt_value, pk, hidden "
                                                               "FROM pragma_table_xinfo(?) ORDER BY cid"),
                                                {table.name});
    int pkCount = 0;
    for (const QVariantList &c : columns) {
        pkCount += c.value(4).toInt() > 0 && c.value(5).toInt() == 0 ? 1 : 0;
    }
    for (const QVariantList &c : columns) {
        if (c.value(5).toInt() != 0) {
            continue; // generated or hidden column: not designable
        }
        FieldDef f;
        f.name = c.value(0).toString();
        f.originalName = f.name;
        f.declared = c.value(1).toString();
        f.primaryKey = c.value(4).toInt() > 0;
        const bool rowidKey = f.primaryKey && pkCount == 1 && !table.withoutRowid
                              && f.declared.trimmed().compare(QLatin1String("INTEGER"), Qt::CaseInsensitive) == 0;
        f.type = typeFromDeclared(f.declared, rowidKey);
        f.required = c.value(2).toInt() != 0 && !rowidKey;
        const QVariantList m = meta.value(f.name.toLower());
        if (!m.isEmpty()) {
            if (const std::optional<FieldType> t = typeFromId(m.value(1).toString());
                t && (typeFromDeclared(f.declared, rowidKey) == *t || (rowidKey && *t == FieldType::Integer))) {
                f.type = *t;
            }
            f.caption = m.value(2).toString();
            f.description = m.value(3).toString();
            f.lookup.table = m.value(4).toString();
            f.lookup.valueField = m.value(5).toString();
            f.lookup.displayField = m.value(6).toString();
        }
        f.defaultValue = defaultToEditText(c.value(3).toString(), f.type, currency);
        table.fields.append(f);
    }
    // Indexes: single-column plain/unique ones are field properties; the
    // rest are kept verbatim through rebuilds.
    for (const QVariantList &idx :
         db.rows(QStringLiteral("SELECT name, \"unique\", origin, partial FROM pragma_index_list(?)"), {table.name})) {
        const QString indexName = idx.value(0).toString();
        const bool unique = idx.value(1).toInt() != 0;
        const QString origin = idx.value(2).toString();
        if (origin == QLatin1String("pk")) {
            continue;
        }
        const QList<QVariantList> cols =
            db.rows(QStringLiteral("SELECT name FROM pragma_index_info(?) ORDER BY seqno"), {indexName});
        const bool simple = cols.size() == 1 && !cols.first().value(0).isNull() && idx.value(3).toInt() == 0;
        if (simple) {
            const int fi = table.fieldIndex(cols.first().value(0).toString());
            if (fi >= 0) {
                FieldDef &f = table.fields[fi];
                (unique ? f.unique : f.indexed) = true;
                continue;
            }
        }
        if (origin == QLatin1String("c")) {
            const QVariant sqlText =
                db.scalar(QStringLiteral("SELECT sql FROM sqlite_schema WHERE type='index' AND name=?"), {indexName});
            if (!sqlText.toString().isEmpty()) {
                table.otherIndexSql.append(sqlText.toString());
            }
        }
    }
    // Foreign keys (this table is the child).
    const QList<QVariantList> keys = db.rows(
        QStringLiteral("SELECT id, seq, \"table\", \"from\", \"to\", on_update, on_delete FROM pragma_foreign_key_list(?)"),
        {table.name});
    QHash<int, int> parts;
    for (const QVariantList &k : keys) {
        parts[k.value(0).toInt()] += 1;
    }
    for (const QVariantList &k : keys) {
        if (parts.value(k.value(0).toInt()) > 1) {
            table.complexKeys = true;
            continue;
        }
        Relationship r;
        r.childTable = table.name;
        r.childField = k.value(3).toString();
        r.parentTable = k.value(2).toString();
        r.parentField = k.value(4).toString();
        if (r.parentField.isEmpty()) {
            // REFERENCES parent without a column = the parent's primary key.
            r.parentField = db.scalar(QStringLiteral("SELECT name FROM pragma_table_info(?) WHERE pk=1"), {r.parentTable})
                                .toString();
        }
        r.onUpdate = actionFromSql(k.value(5).toString());
        r.onDelete = actionFromSql(k.value(6).toString());
        table.relationships.append(r);
    }
    for (const QVariantList &t :
         db.rows(QStringLiteral("SELECT sql FROM sqlite_schema WHERE type='trigger' AND tbl_name=?"), {table.name})) {
        table.triggerSql.append(t.value(0).toString());
    }
    return table;
}

QList<Relationship> readRelationships(const BaseFile &file)
{
    QList<Relationship> out;
    for (const QString &table : file.tableNames()) {
        if (const std::optional<TableDef> def = readTable(file, table)) {
            out.append(def->relationships);
        }
    }
    return out;
}

QHash<QString, FieldType> fieldTypes(const BaseFile &file, const QString &table)
{
    QHash<QString, FieldType> out;
    if (const std::optional<TableDef> def = readTable(file, table)) {
        for (const FieldDef &f : def->fields) {
            out.insert(f.name, f.type);
        }
    }
    return out;
}

} // namespace QindaOffice::Base
