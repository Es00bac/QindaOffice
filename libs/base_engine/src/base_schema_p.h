// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal helpers shared by the schema planners (base_schema_plan.cpp,
// base_schema_alter.cpp). Not part of the engine's public boundary.

#include "base_schema.h"

namespace QindaOffice::Base::detail {

[[nodiscard]] QString validateTable(const BaseFile &file, const TableDef &table, const QString &existingName);
[[nodiscard]] QString metaInsert(const QString &table, const FieldDef &field);
// The table's own row in qb_fields (empty field_name: its validation rule),
// or "" when it has none.
[[nodiscard]] QString metaTableInsert(const TableDef &table);
// The columns qb_fields has in this file, and the ALTER statements that add
// the ones M80 introduced to an older file (empty when it has them all).
[[nodiscard]] QStringList systemFieldColumns(const BaseFile &file);
[[nodiscard]] QStringList systemColumnStatements(const BaseFile &file);
// Rule triggers (base_rules.h): when the rules' SQL changes — or the table
// was rebuilt (`rebuilt`), which drops its triggers — the plan drops the
// ones `before` has first and creates `after`'s last, with the refusals
// that keep a new rule from passing while records break it.
void appendRuleStatements(SchemaPlan *plan, const BaseFile &file, const TableDef &before, const TableDef &after,
                          bool rebuilt);
void appendFieldIndexes(QStringList *out, const TableDef &table, const QString &name);
[[nodiscard]] QList<Relationship> incoming(const BaseFile &file, const QString &table);
[[nodiscard]] QString expectedStorage(FieldType type);
[[nodiscard]] bool lossy(FieldType from, FieldType to);

} // namespace QindaOffice::Base::detail
