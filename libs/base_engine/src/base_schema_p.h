// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal helpers shared by the schema planners (base_schema_plan.cpp,
// base_schema_alter.cpp, base_schema_validate.cpp, base_schema_rules.cpp)
// and readTable. Not part of the engine's public boundary.

#include "base_schema.h"

namespace QindaOffice::Base::detail {

// qb_fields as readTable selects it: one entry per column below, in this
// order (systemFieldSelect writes NULL for a column an older file lacks).
enum MetaColumn {
    MetaFieldName,
    MetaType,
    MetaCaption,
    MetaDescription,
    MetaLookupTable,
    MetaLookupField,
    MetaLookupDisplay,
    MetaValidationRule,  // M80
    MetaValidationText,  // M80
    MetaInputMask,       // M80
    MetaExpression,      // M86: a calculated field's Access text
    MetaLookupValues,    // M86: a value list, as a JSON array
    MetaLimitToList,     // M86: 1 = Limit To List
    MetaAllowZeroLength, // M86: 0 = No (NULL = Yes)
    MetaDataMacros,      // M86, the table's row: data macros as JSON
    MetaLink,            // M86, the table's row: a linked table's source as JSON
};
[[nodiscard]] QStringList systemFieldSelect(const BaseFile &file);

[[nodiscard]] QString validateTable(const BaseFile &file, const TableDef &table, const QString &existingName);
[[nodiscard]] QString metaInsert(const QString &table, const FieldDef &field);
// The table's own row in qb_fields (empty field_name: its validation rule,
// M86 data macros and link), or "" when it has none.
[[nodiscard]] QString metaTableInsert(const TableDef &table);
// The columns qb_fields has in this file, and the ALTER statements that add
// the ones M80 and M86 introduced to an older file (empty when it has them all).
[[nodiscard]] QStringList systemFieldColumns(const BaseFile &file);
[[nodiscard]] QStringList systemColumnStatements(const BaseFile &file);
// Derived triggers (rules, M80; data macros and link guards, M86): when
// their SQL changes — or the table was rebuilt (`rebuilt`), which drops its
// triggers — the plan drops the ones `before` has first and creates
// `after`'s last, with the refusals that keep a new rule from passing while
// records break it. A data macro that cannot be compiled sets the plan's
// error (planDataMacro and validateTable check them first).
void appendDerivedStatements(SchemaPlan *plan, const BaseFile &file, const TableDef &before, const TableDef &after,
                             bool rebuilt);
void appendFieldIndexes(QStringList *out, const TableDef &table, const QString &name);
[[nodiscard]] QList<Relationship> incoming(const BaseFile &file, const QString &table);
[[nodiscard]] QString expectedStorage(FieldType type);
[[nodiscard]] bool lossy(FieldType from, FieldType to);

} // namespace QindaOffice::Base::detail
