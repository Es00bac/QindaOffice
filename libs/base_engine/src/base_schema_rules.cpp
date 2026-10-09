// SPDX-License-Identifier: GPL-3.0-or-later
// Schema plans and M80's table metadata (D-089): the qb_fields columns an
// older file lacks, the table's own qb_fields row, and the rule triggers a
// plan drops and recreates (base_rules.h).
#include "base_rules.h"
#include "base_schema_p.h"

namespace QindaOffice::Base::detail {

namespace {

const char *const kM80Columns[] = {"validation_rule", "validation_text", "input_mask"};

QString orNull(const QString &text)
{
    return text.trimmed().isEmpty() ? QStringLiteral("NULL") : quoteLiteral(text.trimmed());
}

} // namespace

QStringList systemFieldColumns(const BaseFile &file)
{
    QStringList out;
    for (const QVariantList &row : file.db().rows(QStringLiteral("SELECT name FROM pragma_table_info('qb_fields')"))) {
        out.append(row.value(0).toString());
    }
    return out;
}

QStringList systemColumnStatements(const BaseFile &file)
{
    QStringList out;
    if (!file.hasSystemTables()) {
        return out;
    }
    const QStringList have = systemFieldColumns(file);
    for (const char *column : kM80Columns) {
        if (!have.contains(QString::fromLatin1(column))) {
            out.append(QStringLiteral("ALTER TABLE qb_fields ADD COLUMN %1 TEXT").arg(QString::fromLatin1(column)));
        }
    }
    return out;
}

QString metaTableInsert(const TableDef &table)
{
    if (table.validationRule.trimmed().isEmpty() && table.validationText.trimmed().isEmpty()) {
        return {};
    }
    return QStringLiteral("INSERT OR REPLACE INTO qb_fields(table_name, field_name, type, caption, description, "
                          "lookup_table, lookup_field, lookup_display, validation_rule, validation_text, input_mask) "
                          "VALUES(%1, '', NULL, NULL, NULL, NULL, NULL, NULL, %2, %3, NULL)")
        .arg(quoteLiteral(table.name), orNull(table.validationRule), orNull(table.validationText));
}

void appendRuleStatements(SchemaPlan *plan, const BaseFile &file, const TableDef &before, const TableDef &after,
                          bool rebuilt)
{
    const CurrencyFormat currency = file.currency();
    const QStringList creates = ruleTriggerStatements(after, currency);
    const QStringList existing = before.name.isEmpty() ? QStringList() : ruleTriggerStatements(before, currency);
    const bool same = creates == existing && before.ruleTriggers.size() == existing.size();
    if ((same && !rebuilt) || (creates.isEmpty() && before.ruleTriggers.isEmpty())) {
        return;
    }
    QStringList drops;
    for (const QString &name : before.ruleTriggers) {
        drops.append(QStringLiteral("DROP TRIGGER IF EXISTS %1").arg(quoteIdentifier(name)));
    }
    plan->statements = drops + plan->statements;
    plan->statements.append(creates);
    plan->refusals.append(ruleRefusals(before, after, currency));
    for (const FieldDef &f : after.fields) {
        const FieldDef *old = f.originalName.isEmpty() ? nullptr : before.field(f.originalName);
        if (!f.validationRule.trimmed().isEmpty()
            && (old == nullptr || old->validationRule.trimmed() != f.validationRule.trimmed())) {
            plan->notes.append(QStringLiteral("Checks %1 against its validation rule (%2) on every new or changed "
                                              "record.")
                                   .arg(f.name, f.validationRule.trimmed()));
        }
    }
    if (!after.validationRule.trimmed().isEmpty()
        && before.validationRule.trimmed() != after.validationRule.trimmed()) {
        plan->notes.append(QStringLiteral("Checks every new or changed record against the table's validation rule "
                                          "(%1).")
                               .arg(after.validationRule.trimmed()));
    }
}

} // namespace QindaOffice::Base::detail
