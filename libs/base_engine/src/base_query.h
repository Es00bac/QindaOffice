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

enum class QueryKind { Select, Update, Delete, Append, Sql };
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
};

struct QueryDesign {
    QueryKind kind = QueryKind::Select;
    QList<QueryTable> tables;
    QList<QueryJoin> joins;
    QList<QueryColumn> columns;
    bool totals = false;
    bool distinct = false;
    int top = 0;
    QString appendTable;
    QString sql; // hand-written SQL (kind Sql)
    QVariantMap extra; // members this version does not know, kept on save

    [[nodiscard]] QVariantMap toMap() const;
    static QueryDesign fromMap(const QVariantMap &map);
};

struct QueryParameter {
    QString name;   // the SQL parameter name without its ':' prefix
    QString prompt; // what the user is asked
    FieldType type = FieldType::Text;
    [[nodiscard]] QVariantMap toMap() const;
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
};

[[nodiscard]] CompiledQuery compileQuery(const BaseFile &file, const QueryDesign &design);

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

} // namespace QindaOffice::Base
