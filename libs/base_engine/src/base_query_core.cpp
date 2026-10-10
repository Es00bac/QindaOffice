// SPDX-License-Identifier: GPL-3.0-or-later
// The query compiler's shared core (base_query_p.h): tables and field
// resolution, each grid column's SQL with its total, the criteria rows and
// the FROM clause with its joins.
#include "base_access_sql.h"
#include "base_query_p.h"

#include <QHash>
#include <QSet>

namespace QindaOffice::Base::detail {

namespace {

QString s(const char *text)
{
    return QString::fromLatin1(text);
}

QString q(const QString &identifier)
{
    return quoteIdentifier(identifier);
}

QString tableSql(const QueryTableInfo &t)
{
    return t.ref == t.name ? q(t.name) : QStringLiteral("%1 AS %2").arg(q(t.name), q(t.ref));
}

// FROM with joins: each join attaches a table to the ones already placed;
// tables no join reaches multiply (CROSS JOIN), as in Access.
QString fromClause(const QList<QueryTableInfo> &tables, const QList<QueryJoin> &joins, QString *error)
{
    QString from = tableSql(tables.first());
    QStringList placed{tables.first().ref.toLower()};
    QList<QueryJoin> pending = joins;
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
            const auto t = std::find_if(tables.cbegin(), tables.cend(), [&](const QueryTableInfo &x) {
                return x.ref.compare(addRef, Qt::CaseInsensitive) == 0;
            });
            if (t == tables.cend()) {
                *error = QStringLiteral("A join names %1, which is not in the query.").arg(addRef);
                return {};
            }
            QString kind = join.kind == QLatin1String("left")    ? s("LEFT JOIN")
                           : join.kind == QLatin1String("right") ? s("RIGHT JOIN")
                                                                 : s("INNER JOIN");
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
            for (const QueryTableInfo &t : tables) {
                if (!placed.contains(t.ref.toLower())) {
                    from += QStringLiteral("\n  CROSS JOIN %1").arg(tableSql(t));
                    placed.append(t.ref.toLower());
                }
            }
        }
    }
    return from;
}

} // namespace

QString disjunction(const QStringList &parts)
{
    return parts.size() == 1 ? parts.first()
                             : QStringLiteral("(") + parts.join(QStringLiteral(") OR (")) + QStringLiteral(")");
}

QString QueryCore::where() const
{
    return whereRows.isEmpty() ? QString() : QStringLiteral("\nWHERE ") + disjunction(whereRows);
}

QString QueryCore::having() const
{
    return havingRows.isEmpty() ? QString() : QStringLiteral("\nHAVING ") + disjunction(havingRows);
}

std::function<QString(const QString &, QString *)> subqueryCompiler(const BaseFile &file)
{
    return [&file](const QString &text, QString *error) -> QString {
        const AccessSqlParse parsed = parseAccessSql(file, text);
        if (!parsed.ok) {
            *error = parsed.error;
            return {};
        }
        if (parsed.design.kind != QueryKind::Select && parsed.design.kind != QueryKind::Union) {
            *error = QStringLiteral("a subquery is a SELECT.");
            return {};
        }
        const CompiledQuery compiled = compileQuery(file, parsed.design);
        if (!compiled.ok) {
            *error = compiled.error;
            return {};
        }
        if (!compiled.parameters.isEmpty()) {
            *error = QStringLiteral("a subquery cannot ask for values; give them in its text.");
            return {};
        }
        return compiled.sql;
    };
}

bool totalsOn(const QueryDesign &design)
{
    return design.kind == QueryKind::Crosstab
           || (design.totals && (design.kind == QueryKind::Select || design.kind == QueryKind::MakeTable));
}

QString buildQueryCore(const BaseFile &file, const QueryDesign &design, QList<QueryParameter> *parameters,
                       QueryCore *core)
{
    if (design.tables.isEmpty()) {
        return QStringLiteral("Add a table to the query.");
    }
    QSet<QString> refs;
    for (const QueryTable &t : design.tables) {
        std::optional<TableDef> def = readTable(file, t.name);
        if (!def) {
            return QStringLiteral("The query uses a missing table %1.").arg(t.name);
        }
        if (refs.contains(t.ref().toLower())) {
            return QStringLiteral("%1 is in the query twice; give one an alias.").arg(t.ref());
        }
        refs.insert(t.ref().toLower());
        core->tables.append({t.ref(), def->name, *def});
    }
    core->currency = file.currency();
    // Field references: [field] (unique across the tables) or [table].[field].
    // Update and delete queries over one table name the column bare.
    const bool bare = core->tables.size() == 1
                      && (design.kind == QueryKind::Update || design.kind == QueryKind::Delete);
    const QList<QueryTableInfo> tables = core->tables;
    core->resolve = [tables, bare](const QString &table, const QString &field) -> std::optional<Expr::FieldRef> {
        std::optional<Expr::FieldRef> found;
        for (const QueryTableInfo &t : tables) {
            if (!table.isEmpty() && t.ref.compare(table, Qt::CaseInsensitive) != 0) {
                continue;
            }
            if (const FieldDef *f = t.def.field(field)) {
                if (found && table.isEmpty()) {
                    return found; // ambiguous: first table wins, as in the grid
                }
                found = Expr::FieldRef{bare ? q(f->name) : QStringLiteral("%1.%2").arg(q(t.ref), q(f->name)), f->type};
            }
        }
        return found;
    };
    // Declared parameters come first and keep their type (Query Parameters).
    for (const QueryParameter &p : design.parameters) {
        if (p.prompt.trimmed().isEmpty()) {
            continue;
        }
        QueryParameter declared = p;
        declared.prompt = p.prompt.trimmed();
        declared.name = Expr::parameterNameFor(declared.prompt, *parameters);
        declared.declared = true;
        if (std::none_of(parameters->cbegin(), parameters->cend(), [&](const QueryParameter &x) {
                return x.prompt.compare(declared.prompt, Qt::CaseInsensitive) == 0;
            })) {
            parameters->append(declared);
        }
    }
    const Expr::Context ctx{core->resolve, core->currency, parameters, subqueryCompiler(file)};
    const bool totals = totalsOn(design);
    for (int i = 0; i < design.columns.size(); ++i) {
        const QueryColumn &c = design.columns.at(i);
        QueryCol col;
        if (!c.expression.trimmed().isEmpty()) {
            const Expr::Compiled e = Expr::expression(c.expression, ctx);
            if (!e.ok()) {
                return QStringLiteral("Column %1: %2").arg(i + 1).arg(e.error);
            }
            col.sql = e.sql;
            col.type = e.type;
            col.aggregate = e.aggregate;
            col.name = c.alias.isEmpty() ? QStringLiteral("Expr%1").arg(i + 1) : c.alias;
        } else if (c.field == QLatin1String("*")) {
            col.sql = c.table.isEmpty() ? QStringLiteral("*") : QStringLiteral("%1.*").arg(q(c.table));
            col.name = QStringLiteral("*");
        } else {
            const std::optional<Expr::FieldRef> ref = core->resolve(c.table, c.field);
            if (!ref) {
                return QStringLiteral("Column %1: there is no field %2.").arg(i + 1).arg(c.field);
            }
            col.sql = ref->sql;
            col.type = ref->type;
            col.name = c.alias.isEmpty() ? c.field : c.alias;
            col.sourceTable = c.table;
            col.sourceField = c.field;
            for (const QueryTableInfo &t : core->tables) {
                if (c.table.isEmpty() ? t.def.field(c.field) != nullptr
                                      : t.ref.compare(c.table, Qt::CaseInsensitive) == 0) {
                    col.sourceTable = t.name;
                    col.sourceField = t.def.field(c.field) != nullptr ? t.def.field(c.field)->name : c.field;
                    break;
                }
            }
        }
        if (totals) {
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
                col.inner = col.sql;
                col.function = fn.value(c.total);
                col.sql = QStringLiteral("%1(%2)").arg(col.function, col.sql);
                col.aggregate = true;
                if (c.total == TotalKind::Count) {
                    col.type = FieldType::Integer;
                } else if (c.total == TotalKind::Average && col.type != FieldType::Currency) {
                    col.type = FieldType::Number;
                }
                col.sourceTable.clear();
            }
        }
        core->cols.append(col);
    }

    // Criteria: Access semantics — one row ANDs its cells, rows are ORed.
    int rows = 0;
    for (const QueryColumn &c : design.columns) {
        rows = std::max(rows, int(c.orCriteria.size()) + 1);
    }
    const bool crosstab = design.kind == QueryKind::Crosstab;
    for (int r = 0; r < rows; ++r) {
        QStringList where;
        QStringList having;
        for (int i = 0; i < design.columns.size(); ++i) {
            const QueryColumn &c = design.columns.at(i);
            const QString text = r == 0 ? c.criteria : c.orCriteria.value(r - 1);
            if (text.trimmed().isEmpty()) {
                continue;
            }
            const QueryCol &col = core->cols.at(i);
            if (col.name == QLatin1String("*")) {
                return QStringLiteral("Criteria need a single field, not *.");
            }
            if (crosstab && c.crosstab == QLatin1String("value")) {
                return QStringLiteral("A crosstab's value takes no criteria; add the field again as a Where column.");
            }
            const Expr::Compiled e = Expr::criteria(text, col.sql, col.type.value_or(FieldType::Text), ctx);
            if (!e.ok()) {
                return QStringLiteral("Criteria for %1: %2").arg(col.name, e.error);
            }
            // A crosstab groups by its row headings only: criteria on the
            // column heading filter the rows before they are pivoted.
            const bool toHaving = totals && c.total != TotalKind::Where && (col.aggregate || c.total == TotalKind::GroupBy)
                                  && !(crosstab && c.crosstab == QLatin1String("column"));
            (toHaving ? having : where).append(e.sql);
        }
        if (!where.isEmpty()) {
            core->whereRows.append(where.join(QStringLiteral(" AND ")));
        }
        if (!having.isEmpty()) {
            core->havingRows.append(having.join(QStringLiteral(" AND ")));
        }
    }
    QString error;
    core->from = fromClause(core->tables, design.joins, &error);
    return error;
}

QString selectSql(const QueryDesign &design, const QueryCore &core, QList<OutputColumn> *columns, QString *error)
{
    const bool totals = totalsOn(design);
    QStringList select;
    QStringList group;
    QStringList order;
    for (int i = 0; i < design.columns.size(); ++i) {
        const QueryColumn &c = design.columns.at(i);
        const QueryCol &col = core.cols.at(i);
        const bool whereOnly = totals && c.total == TotalKind::Where;
        if (c.show && !whereOnly) {
            QString value = col.sql;
            if (col.type == FieldType::Currency && !col.aggregate && !c.expression.isEmpty()) {
                value = QStringLiteral("CAST(round(%1) AS INTEGER)").arg(value); // money stays whole minor units
            }
            select.append(col.name == QLatin1String("*") ? value : QStringLiteral("%1 AS %2").arg(value, q(col.name)));
            if (col.name != QLatin1String("*")) {
                columns->append({col.name, col.type.value_or(FieldType::Text), col.type.has_value(), col.sourceTable,
                                 col.sourceField});
            }
        }
        if (totals && c.total == TotalKind::GroupBy && !col.aggregate) {
            group.append(col.sql);
        }
        if (c.sort != 0) {
            order.append(col.sql + (c.sort < 0 ? QStringLiteral(" DESC") : QStringLiteral(" ASC")));
        }
    }
    if (select.isEmpty()) {
        *error = QStringLiteral("Show at least one column.");
        return {};
    }
    QString body = QStringLiteral("SELECT %1%2\nFROM %3%4")
                       .arg(design.distinct ? QStringLiteral("DISTINCT ") : QString(), select.join(QStringLiteral(", ")),
                            core.from, core.where());
    if (!group.isEmpty()) {
        body += QStringLiteral("\nGROUP BY ") + group.join(QStringLiteral(", "));
    }
    body += core.having();
    QString sql = body;
    if (!order.isEmpty()) {
        sql += QStringLiteral("\nORDER BY ") + order.join(QStringLiteral(", "));
    }
    if (design.top > 0 && design.topPercent) {
        // Top n percent: the share of the rows the query returns, rounded up
        // (core SQLite only: integer arithmetic instead of ceil()).
        sql += QStringLiteral("\nLIMIT (SELECT (count(*) * %1 + 99) / 100 FROM (%2))").arg(qMin(design.top, 100)).arg(body);
    } else if (design.top > 0) {
        sql += QStringLiteral("\nLIMIT %1").arg(design.top);
    }
    return sql;
}

} // namespace QindaOffice::Base::detail
