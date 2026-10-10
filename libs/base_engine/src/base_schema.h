// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Table designs and relationships (M49, D-049 §3): reading a table's
// definition from SQLite introspection plus qb_fields, and planning every
// schema change as ordered single statements the user previews before
// they run (BaseFile::previewPlan / applyPlan).
//
// AGENT-NOTE (how a change is planned): a table rename and column renames
// run first as plain ALTER statements, because only those rewrite the
// references other tables, views and triggers hold; everything else SQLite
// cannot ALTER (types, required, defaults, keys, relationships, dropped
// columns) goes through the documented 12-step table rebuild
// (sqlite.org/lang_altertable.html §7) under legacy_alter_table=ON. Values
// the new type cannot hold are kept as they were (SQLite's flexible
// typing) and counted in the preview; nothing is silently lost. Changes
// that do lose data are `destructive`, which makes applyPlan take a backup.

#include "base_file.h"
#include "base_types.h"

#include <QVariantMap>

#include <optional>

namespace QindaOffice::Base {

enum class RefAction { NoAction, Restrict, Cascade, SetNull };

[[nodiscard]] QString refActionId(RefAction action); // "noAction" | "restrict" | "cascade" | "setNull"
[[nodiscard]] RefAction refActionFromId(const QString &id);

struct LookupDef {
    QString table;
    QString valueField;
    QString displayField;
    // M86 (D-094): Access's Value List lookup — the choices themselves
    // ("Check";"Card";"Cash"), and Limit To List, which the database
    // enforces like a validation rule (base_rules.h). A field looks up a
    // table or a value list, never both.
    QStringList values{};
    bool limitToList = false;
    [[nodiscard]] bool isSet() const { return !table.isEmpty() && !valueField.isEmpty(); }
    [[nodiscard]] bool isValueList() const { return !values.isEmpty(); }
};

// M86 (D-094, base_indexes.h): an index the Indexes dialog designs — over
// several fields, descending, ignoring Nulls (a partial index) or under a
// name of its own. A plain single-field index stays the field's Indexed
// property (FieldDef::indexed / unique).
struct IndexField {
    QString field;
    bool descending = false;
};

struct IndexDef {
    QString name;         // as the user sees it ("ByDonorDate")
    QString originalName; // the SQLite index it was read from; "" = new
    QList<IndexField> fields;
    bool unique = false;
    bool ignoreNulls = false;
    [[nodiscard]] QVariantMap toMap() const;
    static IndexDef fromMap(const QVariantMap &map);
};

struct FieldDef {
    QString name;
    QString originalName; // the column it was read from; "" = a new field
    FieldType type = FieldType::Text;
    QString declared;     // declared type as read (kept when the type is unchanged)
    bool primaryKey = false;
    bool required = false;
    QString defaultValue; // edit text, "Date()", "Now()" or "=<sql expression>" (kept verbatim)
    bool unique = false;
    bool indexed = false;
    QString caption;
    QString description;
    LookupDef lookup;
    // M80 (D-089, base_rules.h, base_input_mask.h): Access's Validation
    // Rule (criteria text, "> 0"), Validation Text (the message) and Input
    // Mask, kept in qb_fields.
    QString validationRule;
    QString validationText;
    QString inputMask;
    // M86 (D-094, base_calculated.h): a calculated field is a generated
    // column computing `expression` (Access text over the record's fields)
    // as `type` (the result type). `generatedSql` is a foreign file's own
    // generated-column SQL, kept verbatim while `expression` is empty.
    bool calculated = false;
    QString expression;
    QString generatedSql;
    // M86: Access's Allow Zero Length for text, long text and hyperlinks;
    // "No" is enforced by the database (base_rules.h).
    bool allowZeroLength = true;

    [[nodiscard]] QVariantMap toMap() const;
    static FieldDef fromMap(const QVariantMap &map);
};

struct Relationship {
    QString childTable;
    QString childField;
    QString parentTable;
    QString parentField;
    RefAction onDelete = RefAction::NoAction;
    RefAction onUpdate = RefAction::NoAction;

    [[nodiscard]] QString id() const; // "child.field>parent.field"
    [[nodiscard]] QVariantMap toMap() const;
    static Relationship fromMap(const QVariantMap &map);
    [[nodiscard]] bool sameLink(const Relationship &other) const;
};

struct TableDef {
    QString name;
    QString originalName; // "" = a new table
    QList<FieldDef> fields;
    QList<Relationship> relationships; // this table's foreign keys (it is the child)
    QStringList otherIndexSql;         // multi-column/partial indexes, kept through rebuilds
    QStringList triggerSql;            // triggers, kept through rebuilds
    bool autoIncrement = false;
    bool withoutRowid = false;
    bool complexKeys = false;          // a multi-column foreign key QindaBase cannot redesign
    // M80: the table's record rule ("[ends] >= [starts]") and its message,
    // and the names of the triggers that enforce the rules now (base_rules.h).
    QString validationRule;
    QString validationText;
    // The triggers QindaBase derives from the design (validation rules, M80;
    // M86: data macros and a linked table's guard): their names as read, so
    // a plan drops them before it makes them again (base_rules.h,
    // base_data_macro.h, base_links.h).
    QStringList derivedTriggers;
    // M86 (D-094): the Indexes dialog's indexes (base_indexes.h), the
    // table's data macros by event ({"beforeChange": Macro::toMap(), …},
    // base_data_macro.h) and, for a linked table, its source
    // (LinkSource::toMap, base_links.h — read-only here).
    QList<IndexDef> indexes;
    QVariantMap dataMacros;
    QVariantMap link;

    [[nodiscard]] int fieldIndex(const QString &name) const;
    [[nodiscard]] const FieldDef *field(const QString &name) const;
    [[nodiscard]] QStringList primaryKey() const;
    [[nodiscard]] QVariantMap toMap() const;
    static TableDef fromMap(const QVariantMap &map);
};

// ---- reading ----
[[nodiscard]] std::optional<TableDef> readTable(const BaseFile &file, const QString &name, QString *error = nullptr);
// Every single-column foreign key in the database.
[[nodiscard]] QList<Relationship> readRelationships(const BaseFile &file);
// The QindaBase type of each column of `table` (a cheap subset of readTable).
[[nodiscard]] QHash<QString, FieldType> fieldTypes(const BaseFile &file, const QString &table);
// The parts of a stored default as edit text ("Date()", "10.00", "abc").
[[nodiscard]] QString defaultToEditText(const QString &sqlDefault, FieldType type, const CurrencyFormat &currency);

// ---- validation and planning ----
[[nodiscard]] QString validateName(const QString &name, const QString &what);
[[nodiscard]] SchemaPlan planCreateTable(const BaseFile &file, const TableDef &table);
[[nodiscard]] SchemaPlan planAlterTable(const BaseFile &file, const TableDef &before, const TableDef &after);
[[nodiscard]] SchemaPlan planDropTable(const BaseFile &file, const QString &table);
[[nodiscard]] SchemaPlan planAddRelationship(const BaseFile &file, const Relationship &relationship);
[[nodiscard]] SchemaPlan planRemoveRelationship(const BaseFile &file, const Relationship &relationship);

// Column and table SQL (exposed for tests and the SQL preview).
[[nodiscard]] QString columnSql(const FieldDef &field, bool singlePrimaryKey, bool autoIncrement,
                                const CurrencyFormat &currency, QString *error);
[[nodiscard]] QString createTableSql(const TableDef &table, const QString &name, const CurrencyFormat &currency,
                                     QString *error);

} // namespace QindaOffice::Base
