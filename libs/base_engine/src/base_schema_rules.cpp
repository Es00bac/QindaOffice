// SPDX-License-Identifier: GPL-3.0-or-later
// Schema plans and the table metadata M80 and M86 added (D-089, D-094):
// the qb_fields columns an older file lacks, the table's own qb_fields row,
// and the derived triggers a plan drops and recreates — validation rules
// (base_rules.h), data macros (base_data_macro.h) and a linked table's guard
// (base_links.h).
#include "base_data_macro.h"
#include "base_links.h"
#include "base_rules.h"
#include "base_schema_p.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace QindaOffice::Base::detail {

namespace {

struct AddedColumn {
    const char *name;
    const char *type;
};

// AGENT-CONTRACT (system tables v1, base_file.cpp kSystemSchema): columns are
// only ever added, never renamed; an older file gains them in its first plan
// that writes qb_fields.
constexpr AddedColumn kAddedColumns[] = {
    {"validation_rule", "TEXT"}, {"validation_text", "TEXT"}, {"input_mask", "TEXT"},            // M80
    {"expression", "TEXT"},      {"lookup_values", "TEXT"},   {"limit_to_list", "INTEGER"},       // M86
    {"allow_zero_length", "INTEGER"}, {"data_macros", "TEXT"}, {"link_source", "TEXT"}};          // M86

// The qb_fields columns in MetaColumn order.
constexpr const char *kMetaColumns[] = {"field_name",      "type",           "caption",          "description",
                                        "lookup_table",    "lookup_field",   "lookup_display",   "validation_rule",
                                        "validation_text", "input_mask",     "expression",       "lookup_values",
                                        "limit_to_list",   "allow_zero_length", "data_macros",   "link_source"};

QString orNull(const QString &text)
{
    return text.trimmed().isEmpty() ? QStringLiteral("NULL") : quoteLiteral(text.trimmed());
}

QString json(const QVariantMap &map)
{
    return map.isEmpty() ? QStringLiteral("NULL")
                         : quoteLiteral(QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(map)).toJson(QJsonDocument::Compact)));
}

QString jsonList(const QStringList &list)
{
    return list.isEmpty() ? QStringLiteral("NULL")
                          : quoteLiteral(QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(list)).toJson(QJsonDocument::Compact)));
}

const QString kInsert = QStringLiteral(
    "INSERT OR REPLACE INTO qb_fields(table_name, field_name, type, caption, description, lookup_table, lookup_field, "
    "lookup_display, validation_rule, validation_text, input_mask, expression, lookup_values, limit_to_list, "
    "allow_zero_length, data_macros, link_source) VALUES(");

// Every derived trigger of `table`, in the order a plan creates them.
QStringList derivedStatements(const TableDef &table, const CurrencyFormat &currency, QString *error)
{
    QStringList out = ruleTriggerStatements(table, currency);
    out.append(dataMacroStatements(table, currency, error));
    out.append(linkGuardStatements(table));
    return out;
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

QStringList systemFieldSelect(const BaseFile &file)
{
    const QStringList have = systemFieldColumns(file);
    QStringList out;
    for (const char *column : kMetaColumns) {
        const QString name = QString::fromLatin1(column);
        out.append(have.contains(name) ? name : QStringLiteral("NULL"));
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
    for (const AddedColumn &column : kAddedColumns) {
        if (!have.contains(QString::fromLatin1(column.name))) {
            out.append(QStringLiteral("ALTER TABLE qb_fields ADD COLUMN %1 %2")
                           .arg(QString::fromLatin1(column.name), QString::fromLatin1(column.type)));
        }
    }
    return out;
}

QString metaInsert(const QString &table, const FieldDef &f)
{
    const bool lookup = f.lookup.isSet();
    QStringList values{quoteLiteral(table),
                       quoteLiteral(f.name),
                       f.type == FieldType::Other && f.calculated ? QStringLiteral("NULL") : quoteLiteral(typeId(f.type)),
                       f.caption.isEmpty() ? QStringLiteral("NULL") : quoteLiteral(f.caption),
                       f.description.isEmpty() ? QStringLiteral("NULL") : quoteLiteral(f.description),
                       lookup ? quoteLiteral(f.lookup.table) : QStringLiteral("NULL"),
                       lookup ? quoteLiteral(f.lookup.valueField) : QStringLiteral("NULL"),
                       lookup && !f.lookup.displayField.isEmpty() ? quoteLiteral(f.lookup.displayField)
                                                                  : QStringLiteral("NULL"),
                       orNull(f.validationRule),
                       orNull(f.validationText),
                       orNull(f.inputMask),
                       f.calculated ? orNull(f.expression) : QStringLiteral("NULL"),
                       jsonList(f.lookup.values),
                       f.lookup.isValueList() && f.lookup.limitToList ? QStringLiteral("1") : QStringLiteral("NULL"),
                       f.allowZeroLength ? QStringLiteral("NULL") : QStringLiteral("0"),
                       QStringLiteral("NULL"),
                       QStringLiteral("NULL")};
    return kInsert + values.join(QStringLiteral(", ")) + QLatin1Char(')');
}

QString metaTableInsert(const TableDef &table)
{
    if (table.validationRule.trimmed().isEmpty() && table.validationText.trimmed().isEmpty() && table.dataMacros.isEmpty()
        && table.link.isEmpty()) {
        return {};
    }
    QStringList values{quoteLiteral(table.name), QStringLiteral("''")};
    for (int i = 0; i < 6; ++i) {
        values.append(QStringLiteral("NULL")); // type … lookup_display
    }
    values << orNull(table.validationRule) << orNull(table.validationText);
    for (int i = 0; i < 5; ++i) {
        values.append(QStringLiteral("NULL")); // input_mask … allow_zero_length
    }
    values << json(table.dataMacros) << json(table.link);
    return kInsert + values.join(QStringLiteral(", ")) + QLatin1Char(')');
}

void appendDerivedStatements(SchemaPlan *plan, const BaseFile &file, const TableDef &before, const TableDef &after,
                             bool rebuilt)
{
    const CurrencyFormat currency = file.currency();
    QString error;
    const QStringList creates = derivedStatements(after, currency, &error);
    if (!error.isEmpty()) {
        plan->error = error;
        return;
    }
    QString ignored;
    const QStringList existing = before.name.isEmpty() ? QStringList() : derivedStatements(before, currency, &ignored);
    // Statements that are not triggers (the application log's table) do not
    // count against the triggers the table has now.
    const qsizetype triggers = std::count_if(existing.cbegin(), existing.cend(), [](const QString &statement) {
        return statement.startsWith(QLatin1String("CREATE TRIGGER"));
    });
    const bool same = creates == existing && before.derivedTriggers.size() == triggers;
    if ((same && !rebuilt) || (creates.isEmpty() && before.derivedTriggers.isEmpty())) {
        return;
    }
    QStringList drops;
    for (const QString &name : before.derivedTriggers) {
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
    if (before.dataMacros != after.dataMacros) {
        QStringList events;
        for (const QString &event : dataMacroEvents()) {
            if (!dataMacro(after, event).steps.isEmpty()) {
                events.append(dataMacroEventLabel(event));
            }
        }
        plan->notes.append(events.isEmpty() ? QStringLiteral("Removes the table's data macros.")
                                            : QStringLiteral("Runs the data macros %1 for every writer of the table.")
                                                  .arg(events.join(QStringLiteral(", "))));
    }
}

} // namespace QindaOffice::Base::detail
