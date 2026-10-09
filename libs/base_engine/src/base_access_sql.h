// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The query designer's SQL view in Access SQL (M80, D-089 §4): a design
// printed as the statement Access shows for it, and Access SQL parsed back
// into a design the grid shows — so the SQL view and the design grid are
// two views of one query, as in Access.
//
// AGENT-CONTRACT (with qml/QuerySqlPane.qml and tst_base_access_sql):
//  * accessSql(design) prints PARAMETERS, SELECT [DISTINCT] [TOP n
//    [PERCENT]] … [INTO t] FROM … [WHERE] [GROUP BY] [HAVING] [ORDER BY],
//    UPDATE … SET, DELETE FROM, INSERT INTO … SELECT and TRANSFORM … PIVOT
//    [IN (…)]. Criteria print as predicates on their column; the grid's
//    rows print as (row 1) OR (row 2).
//  * parseAccessSql(file, accessSql(design)) gives a design that compiles
//    to a query returning the same records (tst_base_access_sql checks the
//    SQL, and the records where only parentheses differ).
//  * Statements the grid cannot hold — UNION, subqueries, joins over
//    several fields, INSERT … VALUES, WHERE shapes that do not split into
//    per-column criteria — are refused with the reason; the designer then
//    offers to keep the text as SQLite SQL (QueryKind::Sql, pass-through).
//  * Text is never evaluated here: expressions, criteria and values are
//    carried as Access text for the compiler (base_expression.h), which
//    quotes and converts every literal.

#include "base_query.h"

namespace QindaOffice::Base {

[[nodiscard]] QString accessSql(const QueryDesign &design);

struct AccessSqlParse {
    bool ok = false;
    QString error;
    QueryDesign design;
};

// `file` supplies the tables' fields (bare [field] names, INSERT INTO
// without a field list).
[[nodiscard]] AccessSqlParse parseAccessSql(const BaseFile &file, const QString &sql);

} // namespace QindaOffice::Base
