// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal helpers shared by the schema planners (base_schema_plan.cpp,
// base_schema_alter.cpp). Not part of the engine's public boundary.

#include "base_schema.h"

namespace QindaOffice::Base::detail {

[[nodiscard]] QString validateTable(const BaseFile &file, const TableDef &table, const QString &existingName);
[[nodiscard]] QString metaInsert(const QString &table, const FieldDef &field);
void appendFieldIndexes(QStringList *out, const TableDef &table, const QString &name);
[[nodiscard]] QList<Relationship> incoming(const BaseFile &file, const QString &table);
[[nodiscard]] QString expectedStorage(FieldType type);
[[nodiscard]] bool lossy(FieldType from, FieldType to);

} // namespace QindaOffice::Base::detail
