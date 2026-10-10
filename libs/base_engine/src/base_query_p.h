// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal parts of the query compiler shared by base_query_*.cpp (M80
// split the M49 compiler so new query kinds live in their own files). Not
// part of the engine's public boundary.

#include "base_expression.h"

#include <functional>

namespace QindaOffice::Base::detail {

struct QueryTableInfo {
    QString ref;  // alias or name
    QString name; // the table
    TableDef def;
};

// One grid column as SQL.
struct QueryCol {
    QString sql;          // the value (aggregated when the column totals)
    QString inner;        // the value before its total ("" when it does not total)
    QString function;     // sum | avg | count | min | max, or ""
    std::optional<FieldType> type;
    bool aggregate = false;
    QString name;         // the output name ("SumOfamount", "Expr2", an alias)
    QString sourceTable;
    QString sourceField;
};

// What every query kind shares: the tables, field resolution, each grid
// column's SQL, the criteria split into WHERE and HAVING rows (Access
// semantics: a row ANDs its cells, rows are ORed) and the FROM clause.
struct QueryCore {
    QList<QueryTableInfo> tables;
    QList<QueryCol> cols;
    QString from;
    QStringList whereRows;
    QStringList havingRows;
    std::function<std::optional<Expr::FieldRef>(const QString &, const QString &)> resolve;
    CurrencyFormat currency;
    [[nodiscard]] QString where() const;  // "\nWHERE …" or ""
    [[nodiscard]] QString having() const; // "\nHAVING …" or ""
};

[[nodiscard]] QString disjunction(const QStringList &parts);
// Whether the design's grid totals (a totals select or make-table, or any crosstab).
[[nodiscard]] bool totalsOn(const QueryDesign &design);
// Fills `core` for `design`, declared parameters first (in `parameters`);
// returns the error that stops compilation, or "".
[[nodiscard]] QString buildQueryCore(const BaseFile &file, const QueryDesign &design,
                                     QList<QueryParameter> *parameters, QueryCore *core);
// The SELECT of a select or make-table design over a built core.
[[nodiscard]] QString selectSql(const QueryDesign &design, const QueryCore &core, QList<OutputColumn> *columns,
                                QString *error);
// TRANSFORM … PIVOT as plain SQL (base_query_crosstab.cpp); `values` are
// the parameter values (nullptr: headings from every row).
[[nodiscard]] CompiledQuery compileCrosstab(const BaseFile &file, const QueryDesign &design, const QVariantMap *values);
// M86 (D-094): what Expr::Context::subquery calls in a query — the
// subquery's Access SQL parsed (base_access_sql.h) and compiled like a
// saved select; one that asks for parameters is refused.
[[nodiscard]] std::function<QString(const QString &, QString *)> subqueryCompiler(const BaseFile &file);
// SELECT … UNION [ALL] SELECT … (M86, base_query_union.cpp).
[[nodiscard]] CompiledQuery compileUnion(const BaseFile &file, const QueryDesign &design, const QVariantMap *values);
// Binds a statement's :name parameters from user text, typed per parameter.
[[nodiscard]] Result bindParameters(const BaseFile &file, Statement &statement, const QList<QueryParameter> &declared,
                                    const QVariantMap &values);
// The SQL with every :name parameter outside quotes replaced by its value
// as a quoted literal (make-table plans run without bindings).
[[nodiscard]] QString inlineParameters(const BaseFile &file, const QString &sql, const QList<QueryParameter> &declared,
                                       const QVariantMap &values, QString *error);

} // namespace QindaOffice::Base::detail
