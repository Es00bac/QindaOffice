// SPDX-License-Identifier: GPL-3.0-or-later
// Records: columns, reads with sort/filter, totals, lookups, input
// resolution and constraint messages. Edits are in base_records_edit.cpp.
#include "base_records.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace QindaOffice::Base {

namespace {

QString q(const QString &identifier)
{
    return quoteIdentifier(identifier);
}

QString likePattern(const QString &text)
{
    QString escaped = text;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('%'), QStringLiteral("\\%"));
    escaped.replace(QLatin1Char('_'), QStringLiteral("\\_"));
    return QLatin1Char('%') + escaped + QLatin1Char('%');
}

} // namespace

QVariantMap ColumnInfo::toMap() const
{
    return {{QStringLiteral("name"), name},
            {QStringLiteral("label"), label()},
            {QStringLiteral("caption"), caption},
            {QStringLiteral("type"), typeId(type)},
            {QStringLiteral("typeLabel"), typeLabel(type)},
            {QStringLiteral("required"), required},
            {QStringLiteral("primaryKey"), primaryKey},
            {QStringLiteral("readOnly"), readOnly},
            {QStringLiteral("defaultValue"), defaultValue},
            {QStringLiteral("numeric"), isNumeric(type)},
            {QStringLiteral("lookupTable"), lookup.table},
            {QStringLiteral("lookupField"), lookup.valueField},
            {QStringLiteral("lookupDisplay"), lookup.displayField}};
}

int RowSet::columnIndex(const QString &name) const
{
    for (int i = 0; i < columns.size(); ++i) {
        if (columns.at(i).name.compare(name, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return -1;
}

QList<ColumnInfo> Records::columns(const BaseFile &file, const QString &table)
{
    QList<ColumnInfo> out;
    const std::optional<TableDef> def = readTable(file, table);
    if (!def) {
        return out;
    }
    for (const FieldDef &f : def->fields) {
        ColumnInfo c;
        c.name = f.name;
        c.caption = f.caption;
        c.type = f.type;
        c.required = f.required;
        c.primaryKey = f.primaryKey;
        c.readOnly = f.type == FieldType::AutoNumber || f.type == FieldType::Other || def->withoutRowid;
        c.defaultValue = f.defaultValue;
        c.lookup = f.lookup;
        if (!c.lookup.isSet()) {
            // A relationship without an explicit lookup still looks up its
            // parent: the parent's first text field is the display.
            for (const Relationship &r : def->relationships) {
                if (r.childField.compare(f.name, Qt::CaseInsensitive) == 0) {
                    c.lookup.table = r.parentTable;
                    c.lookup.valueField = r.parentField;
                    if (const std::optional<TableDef> parent = readTable(file, r.parentTable)) {
                        for (const FieldDef &pf : parent->fields) {
                            if (pf.type == FieldType::Text && !pf.primaryKey) {
                                c.lookup.displayField = pf.name;
                                break;
                            }
                        }
                    }
                }
            }
        }
        out.append(c);
    }
    return out;
}

QString Records::whereClause(const QList<ColumnInfo> &columns, const QList<FilterClause> &filters,
                             const CurrencyFormat &currency, QVariantList *bindings, QString *error)
{
    QStringList parts;
    for (const FilterClause &f : filters) {
        const auto column = std::find_if(columns.cbegin(), columns.cend(), [&](const ColumnInfo &c) {
            return c.name.compare(f.field, Qt::CaseInsensitive) == 0;
        });
        if (column == columns.cend()) {
            *error = QStringLiteral("There is no field %1.").arg(f.field);
            return {};
        }
        const QString field = q(column->name);
        if (f.op == QLatin1String("isNull")) {
            parts.append(field + QStringLiteral(" IS NULL"));
            continue;
        }
        if (f.op == QLatin1String("isNotNull")) {
            parts.append(field + QStringLiteral(" IS NOT NULL"));
            continue;
        }
        if (f.op == QLatin1String("contains") || f.op == QLatin1String("notContains")) {
            parts.append(QStringLiteral("%1 %2LIKE ? ESCAPE '\\'")
                             .arg(field, f.op == QLatin1String("notContains") ? QStringLiteral("NOT ") : QString()));
            bindings->append(likePattern(f.value));
            continue;
        }
        static const QHash<QString, QString> ops{{QStringLiteral("eq"), QStringLiteral("=")},
                                                  {QStringLiteral("ne"), QStringLiteral("<>")},
                                                  {QStringLiteral("lt"), QStringLiteral("<")},
                                                  {QStringLiteral("le"), QStringLiteral("<=")},
                                                  {QStringLiteral("gt"), QStringLiteral(">")},
                                                  {QStringLiteral("ge"), QStringLiteral(">=")}};
        if (!ops.contains(f.op)) {
            *error = QStringLiteral("Unknown filter %1.").arg(f.op);
            return {};
        }
        const Parsed parsed = parseInput(column->type, f.value, currency);
        if (!parsed.ok) {
            *error = QStringLiteral("%1 %2.").arg(column->label(), parsed.error);
            return {};
        }
        if (!parsed.value.isValid()) {
            parts.append(field + (f.op == QLatin1String("ne") ? QStringLiteral(" IS NOT NULL") : QStringLiteral(" IS NULL")));
            continue;
        }
        parts.append(QStringLiteral("%1 %2 ?").arg(field, ops.value(f.op)));
        bindings->append(parsed.value);
    }
    return parts.isEmpty() ? QString() : QStringLiteral(" WHERE ") + parts.join(QStringLiteral(" AND "));
}

RowSet Records::read(const BaseFile &file, const QString &table, const RowQuery &query)
{
    RowSet set;
    set.table = table;
    set.columns = columns(file, table);
    if (set.columns.isEmpty()) {
        set.error = QStringLiteral("There is no table named %1.").arg(table);
        return set;
    }
    const std::optional<TableDef> def = readTable(file, table);
    set.editable = def && !def->withoutRowid && !file.readOnly();
    QVariantList bindings;
    QString error;
    const QString where = whereClause(set.columns, query.filters, file.currency(), &bindings, &error);
    if (!error.isEmpty()) {
        set.error = error;
        return set;
    }
    QStringList names;
    for (const ColumnInfo &c : set.columns) {
        names.append(q(c.name));
    }
    QStringList order;
    for (const SortKey &k : query.sort) {
        if (set.columnIndex(k.field) >= 0) {
            order.append(q(k.field) + (k.descending ? QStringLiteral(" DESC") : QStringLiteral(" ASC")));
        }
    }
    const QString rowid = def && def->withoutRowid ? QStringLiteral("0") : QStringLiteral("rowid");
    order.append(rowid);
    set.totalRows = file.db().scalar(QStringLiteral("SELECT count(*) FROM %1%2").arg(q(table), where), bindings).toLongLong();
    const QString sql = QStringLiteral("SELECT %1, %2 FROM %3%4 ORDER BY %5 LIMIT %6")
                            .arg(rowid, names.join(QStringLiteral(", ")), q(table), where,
                                 order.join(QStringLiteral(", ")))
                            .arg(query.limit);
    Statement statement = file.db().prepare(sql, &error);
    if (!statement.isValid() || !statement.bindAll(bindings)) {
        set.error = error.isEmpty() ? statement.errorText() : error;
        return set;
    }
    const int count = int(set.columns.size());
    Statement::Step step = statement.step();
    while (step == Statement::Step::Row) {
        set.rowids.append(statement.value(0).toLongLong());
        QVariantList row;
        row.reserve(count);
        for (int c = 0; c < count; ++c) {
            row.append(statement.value(c + 1));
        }
        set.values.append(row);
        step = statement.step();
    }
    if (step == Statement::Step::Error) {
        set.error = statement.errorText();
    }
    set.truncated = set.totalRows > set.rowids.size();
    return set;
}

std::optional<QVariantList> Records::readRow(const BaseFile &file, const QString &table, qint64 rowid,
                                             const QList<ColumnInfo> &columns)
{
    QStringList names;
    for (const ColumnInfo &c : columns) {
        names.append(q(c.name));
    }
    const QList<QVariantList> rows =
        file.db().rows(QStringLiteral("SELECT %1 FROM %2 WHERE rowid = ?").arg(names.join(QStringLiteral(", ")), q(table)),
                       {rowid});
    if (rows.isEmpty()) {
        return std::nullopt;
    }
    return rows.first();
}

QVariant Records::total(const BaseFile &file, const QString &table, const QString &field, const QString &function,
                        const QList<FilterClause> &filters, QString *display, QString *error)
{
    const QList<ColumnInfo> cols = columns(file, table);
    const auto column = std::find_if(cols.cbegin(), cols.cend(), [&](const ColumnInfo &c) { return c.name == field; });
    if (column == cols.cend()) {
        return {};
    }
    static const QHash<QString, QString> functions{{QStringLiteral("sum"), QStringLiteral("sum")},
                                                    {QStringLiteral("average"), QStringLiteral("avg")},
                                                    {QStringLiteral("count"), QStringLiteral("count")},
                                                    {QStringLiteral("min"), QStringLiteral("min")},
                                                    {QStringLiteral("max"), QStringLiteral("max")}};
    if (!functions.contains(function)) {
        return {};
    }
    const bool numeric = isNumeric(column->type) || column->type == FieldType::YesNo;
    if ((function == QLatin1String("sum") || function == QLatin1String("average")) && !numeric) {
        if (error != nullptr) {
            *error = QStringLiteral("%1 is not a number.").arg(column->label());
        }
        return {};
    }
    QVariantList bindings;
    QString problem;
    const CurrencyFormat currency = file.currency();
    const QString where = whereClause(cols, filters, currency, &bindings, &problem);
    // AGENT-NOTE: sum() over integer storage (currency minor units, whole
    // numbers) is exact 64-bit arithmetic in SQLite; only average divides.
    const QVariant value = file.db().scalar(
        QStringLiteral("SELECT %1(%2) FROM %3%4").arg(functions.value(function), q(field), q(table), where), bindings);
    if (display != nullptr) {
        if (!value.isValid()) {
            *display = QString();
        } else if (function == QLatin1String("count")) {
            *display = QString::number(value.toLongLong());
        } else if (function == QLatin1String("average") && column->type == FieldType::Currency) {
            *display = displayText(FieldType::Currency, QVariant::fromValue(qint64(std::llround(value.toDouble()))),
                                   currency);
        } else if (function == QLatin1String("average")) {
            *display = formatNumber(std::round(value.toDouble() * 1e6) / 1e6);
        } else {
            *display = displayText(column->type == FieldType::YesNo && function == QLatin1String("sum")
                                       ? FieldType::Integer
                                       : column->type,
                                   value, currency);
        }
    }
    return value;
}

QList<QPair<QVariant, QString>> Records::lookupChoices(const BaseFile &file, const LookupDef &lookup, int limit)
{
    QList<QPair<QVariant, QString>> out;
    if (!lookup.isSet() || !file.tableExists(lookup.table)) {
        return out;
    }
    const QString display = lookup.displayField.isEmpty() ? lookup.valueField : lookup.displayField;
    const QList<QVariantList> rows = file.db().rows(QStringLiteral("SELECT %1, %2 FROM %3 ORDER BY %2 COLLATE NOCASE LIMIT %4")
                                                        .arg(q(lookup.valueField), q(display), q(lookup.table))
                                                        .arg(limit));
    const QHash<QString, FieldType> types = fieldTypes(file, lookup.table);
    const FieldType displayType = types.value(display, FieldType::Text);
    const CurrencyFormat currency = file.currency();
    for (const QVariantList &row : rows) {
        out.append({row.value(0), displayText(displayType, row.value(1), currency)});
    }
    return out;
}

Parsed Records::resolveInput(const BaseFile &file, const ColumnInfo &column, const QVariant &input)
{
    const CurrencyFormat currency = file.currency();
    Parsed parsed = parseValue(column.type, input, currency);
    if (parsed.ok || !column.lookup.isSet() || column.lookup.displayField.isEmpty()) {
        return parsed;
    }
    // Text typed into a lookup field names the related record.
    const QString text = input.toString().trimmed();
    const QList<QVariantList> matches =
        file.db().rows(QStringLiteral("SELECT %1 FROM %2 WHERE %3 = ? COLLATE NOCASE LIMIT 2")
                           .arg(q(column.lookup.valueField), q(column.lookup.table), q(column.lookup.displayField)),
                       {text});
    if (matches.size() == 1) {
        return Parsed{true, matches.first().value(0), {}};
    }
    return Parsed{false, {},
                  matches.isEmpty() ? QStringLiteral("has no match in %1 for \"%2\"").arg(column.lookup.table, text)
                                    : QStringLiteral("matches several %1 records; pick one from the list")
                                          .arg(column.lookup.table)};
}

QString Records::explain(const QString &sqliteError, const QList<ColumnInfo> &columns, const QString &table)
{
    const auto label = [&](const QString &name) {
        for (const ColumnInfo &c : columns) {
            if (c.name.compare(name, Qt::CaseInsensitive) == 0) {
                return c.label();
            }
        }
        return name;
    };
    static const QRegularExpression notNull(QStringLiteral("NOT NULL constraint failed: [^.]+\\.(\\S+)"));
    static const QRegularExpression unique(QStringLiteral("UNIQUE constraint failed: [^.]+\\.(\\S+)"));
    QRegularExpressionMatch m = notNull.match(sqliteError);
    if (m.hasMatch()) {
        return QStringLiteral("%1 is required.").arg(label(m.captured(1)));
    }
    m = unique.match(sqliteError);
    if (m.hasMatch()) {
        return QStringLiteral("Another record already has this %1; it must be unique.").arg(label(m.captured(1)));
    }
    if (sqliteError.contains(QLatin1String("FOREIGN KEY constraint failed"))) {
        return QStringLiteral("This would break a relationship of %1: a related record is missing, or other records "
                              "still depend on this one.")
            .arg(table);
    }
    return sqliteError;
}

} // namespace QindaOffice::Base
