// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Queries (M49, D-049 §6): the designer's model (tables, joins, a column
// grid with totals, sort, show, criteria and Or rows — the grid people know
// from Access), its compilation to SQL, and running selects and action
// queries.
//
// AGENT-CONTRACT (safety, D-049 §6):
//  * Criteria and expressions are Access-style text ("> 100", "Between
//    #1/1/2026# And #3/31/2026#", "Like \"A*\"", "[Year]", "Year([given])")
//    compiled by base_expression.cpp: field names resolve against the
//    query's tables, literals are converted by the compared field's type
//    (so "> 100" on currency means 100.00, i.e. 10000 minor units) and
//    written as quoted SQL literals — user text is never pasted into SQL
//    unquoted. An unknown [name] is a parameter the user is asked for when
//    the query runs; it becomes a named SQL parameter (:name).
//  * The compiled SQL uses only core SQLite (no qb_ functions), so other
//    tools can run a saved query's `sql`.
//  * Action queries (update, delete, append, or hand-written DML) never run
//    without a count: previewAction runs them in a rolled-back savepoint and
//    reports the rows affected; runAction takes that confirmed count and
//    refuses if the data changed in between.

#include "base_file.h"
#include "base_schema.h"

#include <QVariantMap>

namespace QindaOffice::Base {

// M80 (D-089): Crosstab (Access's TRANSFORM … PIVOT) and MakeTable
// (SELECT … INTO). Ids: select | update | delete | append | sql |
// crosstab | makeTable.
enum class QueryKind { Select, Update, Delete, Append, Sql, Crosstab, MakeTable };
// groupBy | sum | average | count | min | max | where | expression
enum class TotalKind { GroupBy, Sum, Average, Count, Min, Max, Where, Expression };

[[nodiscard]] QString queryKindId(QueryKind kind);
[[nodiscard]] QueryKind queryKindFromId(const QString &id);
[[nodiscard]] QString totalKindId(TotalKind kind);
[[nodiscard]] TotalKind totalKindFromId(const QString &id);

struct QueryTable {
    QString name;
    QString alias; // "" = the name
    [[nodiscard]] QString ref() const { return alias.isEmpty() ? name : alias; }
};

struct QueryJoin {
    QString leftTable; // table refs (alias or name)
    QString leftField;
    QString rightTable;
    QString rightField;
    QString kind = QStringLiteral("inner"); // inner | left | right
};

struct QueryColumn {
    QString table;      // table ref; "" for an expression column
    QString field;      // a field name or "*"
    QString expression; // Access-style expression; takes precedence over table/field
    QString alias;      // output name
    TotalKind total = TotalKind::GroupBy;
    int sort = 0;       // 0 none, 1 ascending, -1 descending
    bool show = true;
    QString criteria;
    QStringList orCriteria;
    QString updateTo;   // update queries
    QString appendTo;   // append queries: the target field
    QString crosstab;   // crosstab queries: "row" | "column" | "value" | "" (a Where column)
};

struct QueryParameter {
    QString name;   // the SQL parameter name without its ':' prefix
    QString prompt; // what the user is asked
    FieldType type = FieldType::Text;
    // M80: declared in Query Parameters (Access's PARAMETERS clause): asked
    // first, in this order, and its type is never re-derived from a field.
    bool declared = false;
    [[nodiscard]] QVariantMap toMap() const;
};

struct QueryDesign {
    QueryKind kind = QueryKind::Select;
    QList<QueryTable> tables;
    QList<QueryJoin> joins;
    QList<QueryColumn> columns;
    bool totals = false;
    bool distinct = false;
    int top = 0;
    bool topPercent = false; // M80: Top Values "25%"
    QString appendTable;
    QString makeTable;           // M80: the new table of a make-table query
    QStringList columnHeadings;  // M80: a crosstab's fixed column headings, in order
    QList<QueryParameter> parameters; // M80: declared parameters (prompt, type)
    QString sql; // hand-written SQL (kind Sql)
    QVariantMap extra; // members this version does not know, kept on save

    [[nodiscard]] QVariantMap toMap() const;
    static QueryDesign fromMap(const QVariantMap &map);
};

struct OutputColumn {
    QString name;
    FieldType type = FieldType::Text;
    bool typeKnown = false;
    QString sourceTable;
    QString sourceField;
};

struct CompiledQuery {
    bool ok = false;
    QString error;
    QString sql;
    bool action = false; // changes data
    QList<QueryParameter> parameters;
    QList<OutputColumn> columns; // select queries
    QString makeTable;           // a make-table query: `sql` is its SELECT
    bool crosstab = false;       // `sql` pivots the headings below
    QStringList headings;        // a crosstab's column headings as shown
};

[[nodiscard]] CompiledQuery compileQuery(const BaseFile &file, const QueryDesign &design);
// A crosstab's column headings are the distinct values its rows hold, so
// they depend on the parameters: with `parameters` (name → user text) the
// headings are those of exactly the rows that will run; without, those of
// every row the criteria allow (parameters ignored), for previews.
[[nodiscard]] CompiledQuery compileQuery(const BaseFile &file, const QueryDesign &design,
                                         const QVariantMap &parameters);

struct QueryResult {
    bool ok = false;
    QString error;
    QList<OutputColumn> columns;
    QList<QVariantList> rows;
    bool truncated = false;
};

struct SqlInfo {
    bool ok = false;
    QString error;
    QString keyword;        // SELECT, WITH, UPDATE, …
    bool readOnly = true;
    bool schema = false;    // CREATE / ALTER / DROP
    QList<QueryParameter> parameters;
};

// One statement of hand-written SQL: what it is and which parameters it has.
[[nodiscard]] SqlInfo inspectSql(const BaseFile &file, const QString &sql);
// `parameters`: name → user text, parsed with each parameter's type.
// `hints` (compileQuery's columns) type the columns SQLite cannot trace to
// a table field (expressions, aggregates).
[[nodiscard]] QueryResult runSelect(const BaseFile &file, const QString &sql, const QList<QueryParameter> &declared,
                                    const QVariantMap &parameters, int limit = 100000,
                                    const QList<OutputColumn> &hints = {});
Result previewAction(BaseFile &file, const QString &sql, const QList<QueryParameter> &declared,
                     const QVariantMap &parameters, qint64 *affected);
Result runAction(BaseFile &file, const QString &sql, const QList<QueryParameter> &declared, const QVariantMap &parameters,
                 qint64 confirmedCount, Change *change, qint64 *affected);

// ---- make-table queries (M80, base_query_maketable.cpp) ----
// How many records the make-table query's SELECT returns now.
[[nodiscard]] qint64 countRows(const BaseFile &file, const CompiledQuery &compiled, const QVariantMap &parameters,
                               QString *error);
// The schema plan that (re)creates `compiled.makeTable` with the SELECT's
// records and types: replacing an existing table drops it first (a backup is
// taken), and is refused while a relationship or the query itself uses it.
// Parameter values are written into the plan as quoted literals. `rows`
// receives the record count the plan copies.
[[nodiscard]] SchemaPlan planMakeTable(const BaseFile &file, const CompiledQuery &compiled,
                                       const QVariantMap &parameters, qint64 *rows);

} // namespace QindaOffice::Base
