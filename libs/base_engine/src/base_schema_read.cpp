// SPDX-License-Identifier: GPL-3.0-or-later
// Table designs: reading a table back from SQLite introspection
// (table_xinfo, index_list, foreign_key_list) plus qb_fields. The maps for
// QML are base_schema_maps.cpp.
#include "base_calculated.h"
#include "base_indexes.h"
#include "base_rules.h"
#include "base_schema_p.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

namespace {

QVariantMap jsonMap(const QVariant &text)
{
    return QJsonDocument::fromJson(text.toString().toUtf8()).object().toVariantMap();
}

QStringList jsonList(const QVariant &text)
{
    QStringList out;
    for (const QJsonValue &v : QJsonDocument::fromJson(text.toString().toUtf8()).array()) {
        out.append(v.toString());
    }
    return out;
}

} // namespace

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
    const QString createSql = master.first().value(1).toString();
    const QString sql = createSql.toUpper();
    table.autoIncrement = sql.contains(QLatin1String("AUTOINCREMENT"));
    table.withoutRowid = sql.contains(QLatin1String("WITHOUT ROWID"));
    const CurrencyFormat currency = file.currency();

    // Metadata QindaBase keeps for the columns (captions, lookups, type ids;
    // M80: rules and masks; M86: expressions, value lists, Allow Zero
    // Length, and on the table's own row its data macros and link) —
    // columns an older file may not have yet read as NULL. The row with an
    // empty field_name holds the table's own settings.
    QHash<QString, QVariantList> meta;
    if (file.hasSystemTables()) {
        const QStringList select = detail::systemFieldSelect(file);
        for (const QVariantList &row : db.rows(
                 QStringLiteral("SELECT %1 FROM qb_fields WHERE table_name=?").arg(select.join(QStringLiteral(", "))),
                 {table.name})) {
            meta.insert(row.value(0).toString().toLower(), row);
        }
        const QVariantList own = meta.value(QString());
        table.validationRule = own.value(detail::MetaValidationRule).toString();
        table.validationText = own.value(detail::MetaValidationText).toString();
        table.dataMacros = jsonMap(own.value(detail::MetaDataMacros));
        table.link = jsonMap(own.value(detail::MetaLink));
    }
    const QHash<QString, QString> generated = generatedColumnSql(createSql);
    const QList<QVariantList> columns = db.rows(QStringLiteral("SELECT name, type, \"notnull\", dflt_value, pk, hidden "
                                                               "FROM pragma_table_xinfo(?) ORDER BY cid"),
                                                {table.name});
    int pkCount = 0;
    for (const QVariantList &c : columns) {
        pkCount += c.value(4).toInt() > 0 && c.value(5).toInt() == 0 ? 1 : 0;
    }
    for (const QVariantList &c : columns) {
        const int hidden = c.value(5).toInt();
        if (hidden == 1) {
            continue; // a virtual table's hidden column: not designable
        }
        FieldDef f;
        f.name = c.value(0).toString();
        f.originalName = f.name;
        f.declared = c.value(1).toString();
        // M86: hidden 2 and 3 are generated columns — calculated fields.
        f.calculated = hidden == 2 || hidden == 3;
        f.primaryKey = c.value(4).toInt() > 0;
        const bool rowidKey = f.primaryKey && pkCount == 1 && !table.withoutRowid
                              && f.declared.trimmed().compare(QLatin1String("INTEGER"), Qt::CaseInsensitive) == 0;
        f.type = typeFromDeclared(f.declared, rowidKey);
        f.required = c.value(2).toInt() != 0 && !rowidKey && !f.calculated;
        const QVariantList m = meta.value(f.name.toLower());
        if (!m.isEmpty()) {
            if (const std::optional<FieldType> t = typeFromId(m.value(detail::MetaType).toString());
                t && (typeFromDeclared(f.declared, rowidKey) == *t || (rowidKey && *t == FieldType::Integer))) {
                f.type = *t;
            }
            f.caption = m.value(detail::MetaCaption).toString();
            f.description = m.value(detail::MetaDescription).toString();
            f.lookup.table = m.value(detail::MetaLookupTable).toString();
            f.lookup.valueField = m.value(detail::MetaLookupField).toString();
            f.lookup.displayField = m.value(detail::MetaLookupDisplay).toString();
            f.validationRule = m.value(detail::MetaValidationRule).toString();
            f.validationText = m.value(detail::MetaValidationText).toString();
            f.inputMask = m.value(detail::MetaInputMask).toString();
            f.lookup.values = jsonList(m.value(detail::MetaLookupValues));
            f.lookup.limitToList = m.value(detail::MetaLimitToList).toInt() != 0;
            f.allowZeroLength = m.value(detail::MetaAllowZeroLength).isNull()
                                || m.value(detail::MetaAllowZeroLength).toInt() != 0;
            if (f.calculated) {
                f.expression = m.value(detail::MetaExpression).toString();
            }
        }
        if (f.calculated && f.expression.isEmpty()) {
            f.generatedSql = generated.value(f.name.toLower()); // a foreign file's own
        }
        f.defaultValue = f.calculated ? QString() : defaultToEditText(c.value(3).toString(), f.type, currency);
        table.fields.append(f);
    }
    // Indexes: plain single-field ones are field properties, the Indexes
    // dialog's are IndexDefs, the rest are kept verbatim (base_indexes.h).
    readIndexes(db, &table);
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
    // The triggers QindaBase derives from the design (rules, M80; data
    // macros and link guards, M86) are made again from it by every plan;
    // every other trigger is kept verbatim.
    for (const QVariantList &t : db.rows(
             QStringLiteral("SELECT name, sql FROM sqlite_schema WHERE type='trigger' AND tbl_name=?"), {table.name})) {
        if (isDerivedTrigger(t.value(0).toString())) {
            table.derivedTriggers.append(t.value(0).toString());
        } else {
            table.triggerSql.append(t.value(1).toString());
        }
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
