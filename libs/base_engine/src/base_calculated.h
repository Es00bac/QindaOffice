// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Calculated fields (M86, D-094): Access's Calculated data type — a field
// whose value is an expression over the other fields of its record
// ("[quantity] * [unit_price]", "[first] & \" \" & [last]").
//
// AGENT-CONTRACT (storage, with base_schema_*.cpp and every reader of a
// .qbase file):
//  * A calculated field is a SQLite generated column,
//    `"f" <declared result type> GENERATED ALWAYS AS (<sql>) VIRTUAL`, so
//    every SQLite tool (3.31 or newer) reads its value; nothing stores it
//    and nothing can write it. qb_fields keeps the Access text in
//    `expression` and the result type in `type`.
//  * The SQL is compiled by the query compiler (base_expression.h) with
//    the table's own fields as bare column names: literals are converted
//    and quoted, an unknown [name] is an error (nothing is asked for), and
//    totals, Date() and Now() are refused (a generated column must give the
//    same value every time it is read).
//  * The value is converted to the result type's storage: money in whole
//    minor units (an expression already in money is rounded; one in plain
//    numbers is scaled by the file's decimals — so changing the decimals
//    later needs the field to be saved again), whole numbers rounded,
//    dates through date()/datetime(), text as text.
//  * A foreign file's generated column (no Access text in qb_fields) keeps
//    its own SQL (FieldDef::generatedSql) through every rebuild.

#include "base_schema.h"

#include <QHash>

namespace QindaOffice::Base {

struct CompiledCalculation {
    QString sql;                      // the generated column's expression
    FieldType type = FieldType::Text; // the result type (worked out when the design left it open)
    QString error;
    [[nodiscard]] bool ok() const { return error.isEmpty(); }
};

[[nodiscard]] CompiledCalculation compileCalculatedField(const TableDef &table, const FieldDef &field,
                                                         const CurrencyFormat &currency);
// A compiled value `sql` of type `from` in the storage form of `to` (money
// in whole minor units, …) — shared with data macros' SetField.
[[nodiscard]] QString convertValueSql(const QString &sql, std::optional<FieldType> from, FieldType to,
                                      const CurrencyFormat &currency);
// " GENERATED ALWAYS AS (<sql>) VIRTUAL"
[[nodiscard]] QString generatedClause(const QString &sql);
// The generated columns of a CREATE TABLE statement: column name (lower
// case) → the SQL inside its AS ( … ).
[[nodiscard]] QHash<QString, QString> generatedColumnSql(const QString &createTableSql);
// Fills in the result type of calculated fields whose design left it open
// (FieldType::Other) from their expressions; the first problem, or "".
[[nodiscard]] QString resolveCalculatedTypes(TableDef *table, const CurrencyFormat &currency);

} // namespace QindaOffice::Base
