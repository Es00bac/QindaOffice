// SPDX-License-Identifier: GPL-3.0-or-later
// Query designs: maps for QML and saved objects, and compilation to SQL
// (select with joins/totals/criteria/sort, update, delete, append).
#include "base_expression.h"

#include <QHash>
#include <QSet>

namespace QindaOffice::Base {

namespace {

QString s(const char *text)
{
    return QString::fromLatin1(text);
}

QString q(const QString &identifier)
{
    return quoteIdentifier(identifier);
}

const char *const kKinds[] = {"select", "update", "delete", "append", "sql"};
const char *const kTotals[] = {"groupBy", "sum", "average", "count", "min", "max", "where", "expression"};

// Keys QueryDesign::toMap writes; everything else in a saved map is `extra`.
const QStringList kKnownKeys{s("kind"), s("tables"), s("joins"), s("columns"), s("totals"), s("distinct"),
                             s("top"),  s("appendTable"), s("sql"), s("version")};

struct TableInfo {
    QString ref;
    QString name;
    TableDef def;
};

} // namespace

QString queryKindId(QueryKind kind)
{
    return QString::fromLatin1(kKinds[int(kind)]);
}

QueryKind queryKindFromId(const QString &id)
{
    for (int i = 0; i < 5; ++i) {
        if (id == QLatin1String(kKinds[i])) {
            return QueryKind(i);
        }
    }
    return QueryKind::Select;
}

QString totalKindId(TotalKind kind)
{
    return QString::fromLatin1(kTotals[int(kind)]);
}

TotalKind totalKindFromId(const QString &id)
{
    for (int i = 0; i < 8; ++i) {
        if (id == QLatin1String(kTotals[i])) {
            return TotalKind(i);
        }
    }
    return TotalKind::GroupBy;
}

QVariantMap QueryParameter::toMap() const
{
    return {{s("name"), name}, {s("prompt"), prompt}, {s("type"), typeId(type)}, {s("typeLabel"), typeLabel(type)}};
}

QVariantMap QueryDesign::toMap() const
{
    QVariantMap out = extra;
    QVariantList tableList;
    for (const QueryTable &t : tables) {
        tableList.append(QVariantMap{{s("name"), t.name}, {s("alias"), t.alias}});
    }
    QVariantList joinList;
    for (const QueryJoin &j : joins) {
        joinList.append(QVariantMap{{s("leftTable"), j.leftTable}, {s("leftField"), j.leftField},
                                    {s("rightTable"), j.rightTable}, {s("rightField"), j.rightField},
                                    {s("kind"), j.kind}});
    }
    QVariantList columnList;
    for (const QueryColumn &c : columns) {
        columnList.append(QVariantMap{{s("table"), c.table},       {s("field"), c.field},
                                      {s("expression"), c.expression}, {s("alias"), c.alias},
                                      {s("total"), totalKindId(c.total)}, {s("sort"), c.sort},
                                      {s("show"), c.show},         {s("criteria"), c.criteria},
                                      {s("orCriteria"), c.orCriteria}, {s("updateTo"), c.updateTo},
                                      {s("appendTo"), c.appendTo}});
    }
    out.insert(s("version"), 1);
    out.insert(s("kind"), queryKindId(kind));
    out.insert(s("tables"), tableList);
    out.insert(s("joins"), joinList);
    out.insert(s("columns"), columnList);
    out.insert(s("totals"), totals);
    out.insert(s("distinct"), distinct);
    out.insert(s("top"), top);
    out.insert(s("appendTable"), appendTable);
    out.insert(s("sql"), sql);
    return out;
}

QueryDesign QueryDesign::fromMap(const QVariantMap &map)
{
    QueryDesign d;
    d.kind = queryKindFromId(map.value(s("kind")).toString());
    for (const QVariant &v : map.value(s("tables")).toList()) {
        const QVariantMap m = v.toMap();
        d.tables.append({m.value(s("name")).toString(), m.value(s("alias")).toString()});
    }
    for (const QVariant &v : map.value(s("joins")).toList()) {
        const QVariantMap m = v.toMap();
        d.joins.append({m.value(s("leftTable")).toString(), m.value(s("leftField")).toString(),
                        m.value(s("rightTable")).toString(), m.value(s("rightField")).toString(),
                        m.value(s("kind"), s("inner")).toString()});
    }
    for (const QVariant &v : map.value(s("columns")).toList()) {
        const QVariantMap m = v.toMap();
        QueryColumn c;
        c.table = m.value(s("table")).toString();
        c.field = m.value(s("field")).toString();
        c.expression = m.value(s("expression")).toString();
        c.alias = m.value(s("alias")).toString();
        c.total = totalKindFromId(m.value(s("total")).toString());
        c.sort = m.value(s("sort")).toInt();
        c.show = m.value(s("show"), true).toBool();
        c.criteria = m.value(s("criteria")).toString();
        c.orCriteria = m.value(s("orCriteria")).toStringList();
        c.updateTo = m.value(s("updateTo")).toString();
        c.appendTo = m.value(s("appendTo")).toString();
        d.columns.append(c);
    }
    d.totals = map.value(s("totals")).toBool();
    d.distinct = map.value(s("distinct")).toBool();
    d.top = map.value(s("top")).toInt();
    d.appendTable = map.value(s("appendTable")).toString();
    d.sql = map.value(s("sql")).toString();
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        if (!kKnownKeys.contains(it.key())) {
            d.extra.insert(it.key(), it.value());
        }
    }
    return d;
}

CompiledQuery compileQuery(const BaseFile &file, const QueryDesign &design)
{
    CompiledQuery out;
    const auto fail = [&out](const QString &error) {
        out.ok = false;
        out.error = error;
        return out;
    };
    if (design.kind == QueryKind::Sql) {
        const SqlInfo info = inspectSql(file, design.sql);
        if (!info.ok) {
            return fail(info.error);
        }
        if (info.schema) {
            return fail(QStringLiteral("Schema changes (%1) belong in Database Tools ▸ SQL, where they are previewed and "
                                       "backed up.")
                            .arg(info.keyword));
        }
        out.ok = true;
        out.sql = design.sql.trimmed();
        out.action = !info.readOnly;
        out.parameters = info.parameters;
        return out;
    }
    if (design.tables.isEmpty()) {
        return fail(QStringLiteral("Add a table to the query."));
    }
    QList<TableInfo> tables;
    QSet<QString> refs;
    for (const QueryTable &t : design.tables) {
        std::optional<TableDef> def = readTable(file, t.name);
        if (!def) {
            return fail(QStringLiteral("The query uses a missing table %1.").arg(t.name));
        }
        if (refs.contains(t.ref().toLower())) {
            return fail(QStringLiteral("%1 is in the query twice; give one an alias.").arg(t.ref()));
        }
        refs.insert(t.ref().toLower());
        tables.append({t.ref(), def->name, *def});
    }
    const bool single = tables.size() == 1;
    const auto tableSql = [&](const TableInfo &t) {
        return t.ref == t.name ? q(t.name) : QStringLiteral("%1 AS %2").arg(q(t.name), q(t.ref));
    };
    // Field references: [field] (unique across the tables) or [table].[field].
    const auto resolve = [&](const QString &table, const QString &field) -> std::optional<Expr::FieldRef> {
        std::optional<Expr::FieldRef> found;
        for (const TableInfo &t : tables) {
            if (!table.isEmpty() && t.ref.compare(table, Qt::CaseInsensitive) != 0) {
                continue;
            }
            if (const FieldDef *f = t.def.field(field)) {
                if (found && table.isEmpty()) {
                    return found; // ambiguous: first table wins, as in the grid
                }
                found = Expr::FieldRef{single && design.kind != QueryKind::Select && design.kind != QueryKind::Append
                                           ? q(f->name)
                                           : QStringLiteral("%1.%2").arg(q(t.ref), q(f->name)),
                                       f->type};
            }
        }
        return found;
    };
    Expr::Context ctx{resolve, file.currency(), &out.parameters};

    // Each grid column's SQL value and type.
    struct Col {
        QString sql;
        std::optional<FieldType> type;
        bool aggregate = false;
        QString name;
        QString sourceTable;
        QString sourceField;
    };
    QList<Col> cols;
    for (int i = 0; i < design.columns.size(); ++i) {
        const QueryColumn &c = design.columns.at(i);
        Col col;
        if (!c.expression.trimmed().isEmpty()) {
            const Expr::Compiled e = Expr::expression(c.expression, ctx);
            if (!e.ok()) {
                return fail(QStringLiteral("Column %1: %2").arg(i + 1).arg(e.error));
            }
            col = {e.sql, e.type, e.aggregate, c.alias.isEmpty() ? QStringLiteral("Expr%1").arg(i + 1) : c.alias};
        } else if (c.field == QLatin1String("*")) {
            col.sql = c.table.isEmpty() ? QStringLiteral("*") : QStringLiteral("%1.*").arg(q(c.table));
            col.name = QStringLiteral("*");
        } else {
            const std::optional<Expr::FieldRef> ref = resolve(c.table, c.field);
            if (!ref) {
                return fail(QStringLiteral("Column %1: there is no field %2.").arg(i + 1).arg(c.field));
            }
            col = {ref->sql, ref->type, false, c.alias.isEmpty() ? c.field : c.alias, c.table, c.field};
            for (const TableInfo &t : tables) {
                if (c.table.isEmpty() ? t.def.field(c.field) != nullptr : t.ref.compare(c.table, Qt::CaseInsensitive) == 0) {
                    col.sourceTable = t.name;
                    col.sourceField = t.def.field(c.field) != nullptr ? t.def.field(c.field)->name : c.field;
                    break;
                }
            }
        }
        if (design.totals && design.kind == QueryKind::Select) {
            static const QHash<TotalKind, QString> fn{{TotalKind::Sum, s("sum")}, {TotalKind::Average, s("avg")},
                                                      {TotalKind::Count, s("count")}, {TotalKind::Min, s("min")},
                                                      {TotalKind::Max, s("max")}};
            if (fn.contains(c.total)) {
                static const QHash<TotalKind, QString> prefix{{TotalKind::Sum, s("SumOf")}, {TotalKind::Average, s("AvgOf")},
                                                              {TotalKind::Count, s("CountOf")}, {TotalKind::Min, s("MinOf")},
                                                              {TotalKind::Max, s("MaxOf")}};
                if (c.alias.isEmpty()) {
                    col.name = prefix.value(c.total) + col.name;
                }
                col.sql = QStringLiteral("%1(%2)").arg(fn.value(c.total), col.sql);
                col.aggregate = true;
                if (c.total == TotalKind::Count) {
                    col.type = FieldType::Integer;
                } else if (c.total == TotalKind::Average && col.type != FieldType::Currency) {
                    col.type = FieldType::Number;
                }
                col.sourceTable.clear();
            }
        }
        cols.append(col);
    }

    // Criteria: Access semantics — one row ANDs its cells, rows are ORed.
    int rows = 0;
    for (const QueryColumn &c : design.columns) {
        rows = std::max(rows, int(c.orCriteria.size()) + 1);
    }
    QStringList whereRows;
    QStringList havingRows;
    for (int r = 0; r < rows; ++r) {
        QStringList where;
        QStringList having;
        for (int i = 0; i < design.columns.size(); ++i) {
            const QueryColumn &c = design.columns.at(i);
            const QString text = r == 0 ? c.criteria : c.orCriteria.value(r - 1);
            if (text.trimmed().isEmpty()) {
                continue;
            }
            if (cols.at(i).name == QLatin1String("*")) {
                return fail(QStringLiteral("Criteria need a single field, not *."));
            }
            const Expr::Compiled e =
                Expr::criteria(text, cols.at(i).sql, cols.at(i).type.value_or(FieldType::Text), ctx);
            if (!e.ok()) {
                return fail(QStringLiteral("Criteria for %1: %2").arg(cols.at(i).name, e.error));
            }
            const bool toHaving = design.totals && c.total != TotalKind::Where
                                  && (cols.at(i).aggregate || c.total == TotalKind::GroupBy);
            (toHaving ? having : where).append(e.sql);
        }
        if (!where.isEmpty()) {
            whereRows.append(where.join(QStringLiteral(" AND ")));
        }
        if (!having.isEmpty()) {
            havingRows.append(having.join(QStringLiteral(" AND ")));
        }
    }
    const auto disjunction = [](const QStringList &parts) {
        return parts.size() == 1 ? parts.first() : QStringLiteral("(") + parts.join(QStringLiteral(") OR (")) + QStringLiteral(")");
    };
    const QString where = whereRows.isEmpty() ? QString() : QStringLiteral("\nWHERE ") + disjunction(whereRows);

    // FROM with joins: each join attaches a table to the ones already placed.
    QString from = tableSql(tables.first());
    QStringList placed{tables.first().ref.toLower()};
    QList<QueryJoin> pending = design.joins;
    while (placed.size() < tables.size()) {
        bool progressed = false;
        for (int j = 0; j < pending.size(); ++j) {
            const QueryJoin &join = pending.at(j);
            const bool leftIn = placed.contains(join.leftTable.toLower());
            const bool rightIn = placed.contains(join.rightTable.toLower());
            if (leftIn == rightIn) {
                continue;
            }
            const QString addRef = leftIn ? join.rightTable : join.leftTable;
            const auto t = std::find_if(tables.cbegin(), tables.cend(),
                                        [&](const TableInfo &x) { return x.ref.compare(addRef, Qt::CaseInsensitive) == 0; });
            if (t == tables.cend()) {
                return fail(QStringLiteral("A join names %1, which is not in the query.").arg(addRef));
            }
            QString kind = join.kind == QLatin1String("left") ? s("LEFT JOIN")
                           : join.kind == QLatin1String("right") ? s("RIGHT JOIN") : s("INNER JOIN");
            if (!leftIn && kind != QLatin1String("INNER JOIN")) {
                kind = kind == QLatin1String("LEFT JOIN") ? s("RIGHT JOIN") : s("LEFT JOIN");
            }
            from += QStringLiteral("\n  %1 %2 ON %3.%4 = %5.%6")
                        .arg(kind, tableSql(*t), q(join.leftTable), q(join.leftField), q(join.rightTable),
                             q(join.rightField));
            placed.append(addRef.toLower());
            pending.removeAt(j);
            progressed = true;
            break;
        }
        if (!progressed) {
            // Unjoined tables multiply (Access does the same); say so.
            for (const TableInfo &t : tables) {
                if (!placed.contains(t.ref.toLower())) {
                    from += QStringLiteral("\n  CROSS JOIN %1").arg(tableSql(t));
                    placed.append(t.ref.toLower());
                }
            }
        }
    }

    if (design.kind == QueryKind::Select) {
        QStringList select;
        QStringList group;
        QStringList order;
        for (int i = 0; i < design.columns.size(); ++i) {
            const QueryColumn &c = design.columns.at(i);
            const Col &col = cols.at(i);
            const bool where = design.totals && c.total == TotalKind::Where;
            if (c.show && !where) {
                QString value = col.sql;
                if (col.type == FieldType::Currency && !col.aggregate && !c.expression.isEmpty()) {
                    value = QStringLiteral("CAST(round(%1) AS INTEGER)").arg(value); // money stays whole minor units
                }
                select.append(col.name == QLatin1String("*") ? value : QStringLiteral("%1 AS %2").arg(value, q(col.name)));
                if (col.name != QLatin1String("*")) {
                    out.columns.append({col.name, col.type.value_or(FieldType::Text), col.type.has_value(),
                                        col.sourceTable, col.sourceField});
                }
            }
            if (design.totals && c.total == TotalKind::GroupBy && !col.aggregate) {
                group.append(col.sql);
            }
            if (c.sort != 0) {
                order.append(col.sql + (c.sort < 0 ? QStringLiteral(" DESC") : QStringLiteral(" ASC")));
            }
        }
        if (select.isEmpty()) {
            return fail(QStringLiteral("Show at least one column."));
        }
        out.sql = QStringLiteral("SELECT %1%2\nFROM %3%4")
                      .arg(design.distinct ? QStringLiteral("DISTINCT ") : QString(), select.join(QStringLiteral(", ")),
                           from, where);
        if (!group.isEmpty()) {
            out.sql += QStringLiteral("\nGROUP BY ") + group.join(QStringLiteral(", "));
        }
        if (!havingRows.isEmpty()) {
            out.sql += QStringLiteral("\nHAVING ") + disjunction(havingRows);
        }
        if (!order.isEmpty()) {
            out.sql += QStringLiteral("\nORDER BY ") + order.join(QStringLiteral(", "));
        }
        if (design.top > 0) {
            out.sql += QStringLiteral("\nLIMIT %1").arg(design.top);
        }
        out.ok = true;
        return out;
    }

    out.action = true;
    const TableInfo &target = tables.first();
    if (design.kind == QueryKind::Update || design.kind == QueryKind::Delete) {
        if (!single) {
            return fail(QStringLiteral("Update and delete queries work on one table."));
        }
        if (design.kind == QueryKind::Delete) {
            out.sql = QStringLiteral("DELETE FROM %1%2").arg(q(target.name), where);
            out.ok = true;
            return out;
        }
        QStringList sets;
        for (int i = 0; i < design.columns.size(); ++i) {
            const QueryColumn &c = design.columns.at(i);
            if (c.updateTo.trimmed().isEmpty() || cols.at(i).sourceField.isEmpty()) {
                continue;
            }
            const FieldType type = cols.at(i).type.value_or(FieldType::Text);
            Expr::Compiled v = Expr::valueFor(c.updateTo, type, ctx);
            if (!v.ok()) {
                return fail(QStringLiteral("Update to for %1: %2").arg(c.field, v.error));
            }
            if (type == FieldType::Currency && v.type != FieldType::Currency) {
                v.sql = QStringLiteral("CAST(round(%1) AS INTEGER)").arg(v.sql);
            }
            sets.append(QStringLiteral("%1 = %2").arg(q(cols.at(i).sourceField), v.sql));
        }
        if (sets.isEmpty()) {
            return fail(QStringLiteral("Fill in \"Update to\" for at least one field."));
        }
        out.sql = QStringLiteral("UPDATE %1 SET %2%3").arg(q(target.name), sets.join(QStringLiteral(", ")), where);
        out.ok = true;
        return out;
    }
    // Append: INSERT INTO target(fields) SELECT values FROM … WHERE …
    const std::optional<TableDef> dest = readTable(file, design.appendTable);
    if (!dest) {
        return fail(QStringLiteral("Choose the table to append to."));
    }
    QStringList into;
    QStringList values;
    for (int i = 0; i < design.columns.size(); ++i) {
        const QueryColumn &c = design.columns.at(i);
        if (c.appendTo.trimmed().isEmpty()) {
            continue;
        }
        const FieldDef *f = dest->field(c.appendTo);
        if (f == nullptr) {
            return fail(QStringLiteral("%1 has no field %2.").arg(dest->name, c.appendTo));
        }
        into.append(q(f->name));
        values.append(cols.at(i).sql);
    }
    if (into.isEmpty()) {
        return fail(QStringLiteral("Choose \"Append to\" fields."));
    }
    out.sql = QStringLiteral("INSERT INTO %1(%2)\nSELECT %3\nFROM %4%5")
                  .arg(q(dest->name), into.join(QStringLiteral(", ")), values.join(QStringLiteral(", ")), from, where);
    out.ok = true;
    return out;
}

} // namespace QindaOffice::Base
