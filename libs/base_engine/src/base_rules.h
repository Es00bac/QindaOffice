// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Validation rules (M80, D-089): Access's Validation Rule and Validation
// Text, for a field ("> 0", "Between 1 And 5", "Is Null Or <= Date()") and
// for the whole record ("[ends] >= [starts]").
//
// AGENT-CONTRACT (enforcement, D-089 §1 — every writer of a .qbase file,
// other SQLite tools included, meets the same rule):
//  * A rule is enforced by SQLite triggers QindaBase derives from the design:
//    BEFORE INSERT and BEFORE UPDATE (OF the field, for a field rule) with
//    WHEN NOT (<rule over NEW>) raising ABORT with the validation text.
//    Triggers, not CHECK constraints, because a rule may use Date() and
//    Now(), which SQLite refuses in CHECK constraints.
//  * A rule that evaluates to Null passes, as in Access: "> 0" does not
//    stop an empty field (Required does).
//  * The triggers are named "qbv:<table>:<field>:i|u" ("qbv:<table>::i|u"
//    for the record rule). readTable lists them in TableDef::ruleTriggers,
//    never in triggerSql; every plan that touches a table with rules drops
//    them first and recreates them from the new design last.
//  * Field rules are Access criteria compiled by base_expression.cpp with
//    the field as the implicit left side, so literals convert by the field's
//    type ("> 100" on currency is 10000 minor units). A field rule can only
//    test its own field; the record rule is an expression over any field.
//  * A plan that adds or changes a rule is refused while existing records
//    break it (SchemaPlan::refusals, checked inside the plan's transaction).

#include "base_schema.h"

namespace QindaOffice::Base {

struct CompiledRule {
    QString sql;   // a boolean SQL expression
    QString error; // why the rule cannot be compiled
    [[nodiscard]] bool ok() const { return error.isEmpty(); }
};

[[nodiscard]] bool isRuleTrigger(const QString &name);
// `row` qualifies the columns: "NEW" in a trigger, a quoted table name in a check.
[[nodiscard]] CompiledRule compileFieldRule(const TableDef &table, const FieldDef &field, const QString &row,
                                            const CurrencyFormat &currency);
[[nodiscard]] CompiledRule compileTableRule(const TableDef &table, const QString &row, const CurrencyFormat &currency);
// The message a broken rule shows: the validation text, or a sentence
// naming the field and the rule.
[[nodiscard]] QString fieldRuleMessage(const FieldDef &field);
[[nodiscard]] QString tableRuleMessage(const TableDef &table);
// The first problem with the table's rules and input masks, or "".
[[nodiscard]] QString validateRules(const TableDef &table, const CurrencyFormat &currency);
[[nodiscard]] bool hasRules(const TableDef &table);
// CREATE TRIGGER statements enforcing `table`'s rules (the design must
// have passed validateRules).
[[nodiscard]] QStringList ruleTriggerStatements(const TableDef &table, const CurrencyFormat &currency);
// Count queries over the table as it is after a plan, one per rule that is
// new or changed between `before` (empty name: a new table) and `after`,
// with the message shown when the count is not zero ("%1" = the count).
[[nodiscard]] QList<QPair<QString, QString>> ruleRefusals(const TableDef &before, const TableDef &after,
                                                          const CurrencyFormat &currency);

} // namespace QindaOffice::Base
