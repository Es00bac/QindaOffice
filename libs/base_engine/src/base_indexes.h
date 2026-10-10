// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The Indexes dialog (M86, D-094): Access's indexes over several fields,
// in either direction, unique or not, ignoring Nulls.
//
// AGENT-CONTRACT (with readTable, the planners and qml/IndexesDialog.qml):
//  * A plain single-field ascending index is the field's Indexed property
//    (FieldDef::indexed / unique, SQLite name qbx_<table>_<field>) as since
//    M49. Every other index QindaBase can model is an IndexDef: SQLite name
//    "qbi:<table>:<name>" (the user's name after the second ':'), its
//    fields with their direction, UNIQUE, and Ignore Nulls as a partial
//    index "WHERE "a" IS NOT NULL AND "b" IS NOT NULL" (Access leaves
//    records with a Null key out of such an index).
//  * An index of another tool (any name, not partial, plain columns) reads
//    as an IndexDef under its own name and is dropped by that name; one
//    QindaBase cannot model (expressions, other WHERE clauses) stays
//    verbatim in TableDef::otherIndexSql.
//  * A multi-field UNIQUE constraint written in a foreign CREATE TABLE
//    (sqlite_autoindex_…) reads as a unique IndexDef; changing or removing
//    it rebuilds the table, since SQLite cannot drop such an index.
//  * A unique index is refused while records repeat its values (records
//    with a Null in any of its fields never clash, as in SQLite and Access).

#include "base_schema.h"

namespace QindaOffice::Base {

constexpr int kIndexFieldLimit = 10; // Access's limit

[[nodiscard]] QString indexSqlName(const QString &table, const QString &index);
// The name a user sees for an index read from the file.
[[nodiscard]] QString indexDisplayName(const QString &table, const QString &sqlName);
// Reads `table`'s indexes into field flags, IndexDefs and otherIndexSql.
void readIndexes(const Connection &db, TableDef *table);
// CREATE [UNIQUE] INDEX for a designed index of `table` named `tableName`.
[[nodiscard]] QString createIndexSql(const QString &tableName, const IndexDef &index);
// The first problem with the table's designed indexes, or "".
[[nodiscard]] QString validateIndexes(const TableDef &table);
// Whether going from `before`'s indexes to `after`'s needs the table rebuilt
// (a foreign UNIQUE constraint changed or went).
[[nodiscard]] bool indexesNeedRebuild(const TableDef &before, const TableDef &after);
// Without a rebuild: DROP INDEX for indexes gone or changed and CREATE for
// new or changed ones (all of them again when the table was renamed).
// After a rebuild: CREATE for every designed index.
[[nodiscard]] QStringList indexStatements(const TableDef &before, const TableDef &after, bool rebuilt);
// "2 record(s) repeat … so the index ByDate cannot be unique." for a new or
// changed unique index the records break; "" otherwise.
[[nodiscard]] QString uniqueIndexProblem(const Connection &db, const TableDef &before, const TableDef &after);

} // namespace QindaOffice::Base
