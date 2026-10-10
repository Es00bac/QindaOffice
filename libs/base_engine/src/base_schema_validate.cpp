// SPDX-License-Identifier: GPL-3.0-or-later
// Table designs: what a design must be before it is planned (split from
// base_schema_plan.cpp in M86): names, keys, file fields, M86's calculated
// fields, lookups, relationships (never to a linked table) and the Indexes
// dialog's indexes. The rules and masks are base_rules.h's validateRules.
#include "base_indexes.h"
#include "base_schema_p.h"

#include <QSet>

namespace QindaOffice::Base::detail {

QString validateTable(const BaseFile &file, const TableDef &table, const QString &existingName)
{
    if (QString e = validateName(table.name, QStringLiteral("table")); !e.isEmpty()) {
        return e;
    }
    if (BaseFile::isSystemName(table.name)) {
        return QStringLiteral("Table names starting with qb_ or sqlite_ are reserved.");
    }
    if (table.name.compare(existingName, Qt::CaseInsensitive) != 0 && file.tableExists(table.name)) {
        return QStringLiteral("There is already a table named %1.").arg(table.name);
    }
    if (table.name.compare(existingName, Qt::CaseInsensitive) != 0
        && !file.db().scalar(QStringLiteral("SELECT name FROM sqlite_schema WHERE name=? COLLATE NOCASE"), {table.name})
                .toString()
                .isEmpty()) {
        return QStringLiteral("The name %1 is already used by a view or index.").arg(table.name);
    }
    if (table.complexKeys) {
        return QStringLiteral("%1 has a relationship over several fields, which QindaBase cannot redesign; use the "
                              "SQL view.")
            .arg(table.name);
    }
    if (table.fields.isEmpty()) {
        return QStringLiteral("A table needs at least one field.");
    }
    QSet<QString> names;
    int autoNumbers = 0;
    int keys = 0;
    for (const FieldDef &f : table.fields) {
        if (QString e = validateName(f.name, QStringLiteral("field")); !e.isEmpty()) {
            return e;
        }
        if (names.contains(f.name.toLower())) {
            return QStringLiteral("Two fields are named %1.").arg(f.name);
        }
        names.insert(f.name.toLower());
        autoNumbers += f.type == FieldType::AutoNumber ? 1 : 0;
        keys += f.primaryKey ? 1 : 0;
        if (f.type == FieldType::AutoNumber && !f.primaryKey) {
            return QStringLiteral("The AutoNumber field %1 must be the primary key.").arg(f.name);
        }
        if (f.type == FieldType::Other && f.originalName.isEmpty() && !f.calculated) {
            return QStringLiteral("Choose a type for %1.").arg(f.name);
        }
        // AGENT-GUARD (D-074): a file field holds bytes the user inserts after
        // the record exists — it can be no key, index, default or lookup, and
        // not required (no record could be saved before its file).
        if (f.type == FieldType::File
            && (f.primaryKey || f.unique || f.indexed || f.required || !f.defaultValue.trimmed().isEmpty()
                || f.lookup.isSet())) {
            return QStringLiteral("The file field %1 cannot be a key, unique, indexed, required, looked up or have a "
                                  "default value.")
                .arg(f.name);
        }
        // AGENT-GUARD (M86, base_calculated.h): a calculated field is a
        // generated column — nothing writes it, so it is no key, has no
        // default, is never required and looks nothing up; its rules would
        // test a value no one typed.
        if (f.calculated
            && (f.primaryKey || f.required || !f.defaultValue.trimmed().isEmpty() || f.lookup.isSet()
                || f.lookup.isValueList() || !f.validationRule.trimmed().isEmpty() || !f.inputMask.trimmed().isEmpty())) {
            return QStringLiteral("The calculated field %1 cannot be a key, required, looked up, have a default value, a "
                                  "validation rule or an input mask.")
                .arg(f.name);
        }
        if (f.lookup.isSet()) {
            const bool self = f.lookup.table.compare(table.name, Qt::CaseInsensitive) == 0;
            if (!self && !file.tableExists(f.lookup.table)) {
                return QStringLiteral("The lookup of %1 names a missing table %2.").arg(f.name, f.lookup.table);
            }
            if (!self && !fieldTypes(file, f.lookup.table).contains(f.lookup.valueField)) {
                return QStringLiteral("%1 has no field %2 to look up.").arg(f.lookup.table, f.lookup.valueField);
            }
        }
    }
    if (autoNumbers > 1) {
        return QStringLiteral("A table can have only one AutoNumber field.");
    }
    if (autoNumbers == 1 && keys > 1) {
        return QStringLiteral("An AutoNumber key must be the only key field.");
    }
    if (table.withoutRowid && keys == 0) {
        return QStringLiteral("This table needs a primary key.");
    }
    for (const Relationship &r : table.relationships) {
        if (table.fieldIndex(r.childField) < 0) {
            return QStringLiteral("The relationship names a missing field %1.").arg(r.childField);
        }
        const bool self = r.parentTable.compare(table.name, Qt::CaseInsensitive) == 0;
        std::optional<TableDef> parent = self ? std::optional<TableDef>(table) : readTable(file, r.parentTable);
        if (!parent) {
            return QStringLiteral("The relationship names a missing table %1.").arg(r.parentTable);
        }
        const FieldDef *key = parent->field(r.parentField);
        if (key == nullptr) {
            return QStringLiteral("%1 has no field %2.").arg(r.parentTable, r.parentField);
        }
        if (!(key->primaryKey && parent->primaryKey().size() == 1) && !key->unique) {
            return QStringLiteral("%1.%2 must be the primary key or unique to be on the one side of a relationship.")
                .arg(r.parentTable, r.parentField);
        }
        if (!parent->link.isEmpty()) {
            return QStringLiteral("%1 is linked to another file, so it cannot be on the one side of a relationship "
                                  "(its copy is replaced when it refreshes).")
                .arg(r.parentTable);
        }
        const FieldType childType = table.field(r.childField)->type;
        const bool compatible = childType == key->type || (isWholeNumber(childType) && isWholeNumber(key->type));
        if (!compatible) {
            return QStringLiteral("%1 (%2) and %3.%4 (%5) have different types.")
                .arg(r.childField, typeLabel(childType), r.parentTable, r.parentField, typeLabel(key->type));
        }
        if (table.field(r.childField)->calculated) {
            return QStringLiteral("The calculated field %1 cannot point to another table.").arg(r.childField);
        }
        if (r.onDelete == RefAction::SetNull && table.field(r.childField)->required) {
            return QStringLiteral("%1 is required, so deleting a %2 record cannot set it to empty.")
                .arg(r.childField, r.parentTable);
        }
    }
    if (QString e = validateIndexes(table); !e.isEmpty()) {
        return e;
    }
    return {};
}

} // namespace QindaOffice::Base::detail
