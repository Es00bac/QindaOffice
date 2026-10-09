// SPDX-License-Identifier: GPL-3.0-or-later
// Validation rules: compiling a field's or a record's rule to SQL, the
// triggers that enforce them, and the checks that refuse a new rule while
// records break it. See base_rules.h for the contract.
#include "base_rules.h"

#include "base_expression.h"
#include "base_input_mask.h"

namespace QindaOffice::Base {

namespace {

const QString kPrefix = QStringLiteral("qbv:");

QString q(const QString &identifier)
{
    return quoteIdentifier(identifier);
}

// Field references inside a rule: the table's own fields, qualified by `row`.
Expr::Context ruleContext(const TableDef &table, const QString &row, const CurrencyFormat &currency,
                          QStringList *referenced)
{
    Expr::Context ctx;
    ctx.currency = currency;
    ctx.parameters = nullptr; // a rule asks for nothing: an unknown [name] is an error
    ctx.resolve = [&table, row, referenced](const QString &owner, const QString &name) -> std::optional<Expr::FieldRef> {
        if (!owner.isEmpty() && owner.compare(table.name, Qt::CaseInsensitive) != 0) {
            return std::nullopt;
        }
        const FieldDef *f = table.field(name);
        if (f == nullptr) {
            return std::nullopt;
        }
        if (referenced != nullptr) {
            referenced->append(f->name);
        }
        return Expr::FieldRef{QStringLiteral("%1.%2").arg(row, q(f->name)), f->type};
    };
    return ctx;
}

QString triggerName(const QString &table, const QString &field, const char *when)
{
    return kPrefix + table + QLatin1Char(':') + field + QLatin1Char(':') + QLatin1String(when);
}

QString trigger(const QString &name, const QString &event, const QString &table, const QString &rule,
                const QString &message)
{
    return QStringLiteral("CREATE TRIGGER %1 BEFORE %2 ON %3 FOR EACH ROW WHEN NOT (%4) BEGIN SELECT RAISE(ABORT, %5); "
                          "END")
        .arg(q(name), event, q(table), rule, quoteLiteral(message));
}

} // namespace

bool isRuleTrigger(const QString &name)
{
    return name.startsWith(kPrefix, Qt::CaseInsensitive);
}

CompiledRule compileFieldRule(const TableDef &table, const FieldDef &field, const QString &row,
                              const CurrencyFormat &currency)
{
    CompiledRule out;
    const QString text = field.validationRule.trimmed();
    if (text.isEmpty()) {
        return out;
    }
    if (field.type == FieldType::File) {
        out.error = QStringLiteral("The file field %1 cannot have a validation rule.").arg(field.name);
        return out;
    }
    QStringList referenced;
    const Expr::Context ctx = ruleContext(table, row, currency, &referenced);
    const Expr::Compiled c =
        Expr::criteria(text, QStringLiteral("%1.%2").arg(row, q(field.name)), field.type, ctx);
    if (!c.ok()) {
        out.error = QStringLiteral("The validation rule of %1: %2").arg(field.name, c.error);
        return out;
    }
    if (c.aggregate) {
        out.error = QStringLiteral("The validation rule of %1 cannot total records.").arg(field.name);
        return out;
    }
    for (const QString &other : std::as_const(referenced)) {
        if (other.compare(field.name, Qt::CaseInsensitive) != 0) {
            out.error = QStringLiteral("The validation rule of %1 names %2: a field's rule tests only its own value. "
                                       "Compare fields in the table's validation rule.")
                            .arg(field.name, other);
            return out;
        }
    }
    out.sql = c.sql;
    return out;
}

CompiledRule compileTableRule(const TableDef &table, const QString &row, const CurrencyFormat &currency)
{
    CompiledRule out;
    const QString text = table.validationRule.trimmed();
    if (text.isEmpty()) {
        return out;
    }
    const Expr::Context ctx = ruleContext(table, row, currency, nullptr);
    const Expr::Compiled c = Expr::expression(text, ctx);
    if (!c.ok()) {
        out.error = QStringLiteral("The table's validation rule: %1").arg(c.error);
        return out;
    }
    if (c.aggregate) {
        out.error = QStringLiteral("The table's validation rule cannot total records.");
        return out;
    }
    out.sql = c.sql;
    return out;
}

QString fieldRuleMessage(const FieldDef &field)
{
    if (!field.validationText.trimmed().isEmpty()) {
        return field.validationText.trimmed();
    }
    const QString label = field.caption.isEmpty() ? field.name : field.caption;
    return QStringLiteral("%1 must meet its validation rule: %2").arg(label, field.validationRule.trimmed());
}

QString tableRuleMessage(const TableDef &table)
{
    if (!table.validationText.trimmed().isEmpty()) {
        return table.validationText.trimmed();
    }
    return QStringLiteral("The record breaks the validation rule of %1: %2")
        .arg(table.name, table.validationRule.trimmed());
}

bool hasRules(const TableDef &table)
{
    if (!table.validationRule.trimmed().isEmpty()) {
        return true;
    }
    return std::any_of(table.fields.cbegin(), table.fields.cend(),
                       [](const FieldDef &f) { return !f.validationRule.trimmed().isEmpty(); });
}

QString validateRules(const TableDef &table, const CurrencyFormat &currency)
{
    for (const FieldDef &f : table.fields) {
        if (const CompiledRule r = compileFieldRule(table, f, QStringLiteral("NEW"), currency); !r.ok()) {
            return r.error;
        }
        if (f.validationText.size() > 255) {
            return QStringLiteral("The validation text of %1 is longer than 255 characters.").arg(f.name);
        }
        if (f.inputMask.trimmed().isEmpty()) {
            continue;
        }
        // AGENT-GUARD (base_input_mask.h): masks shape typed text, so a field
        // that takes no typing (or takes a related record's name) has none.
        if (!isTypable(f.type) || f.type == FieldType::YesNo || f.lookup.isSet()) {
            return QStringLiteral("%1 cannot have an input mask: masks shape what is typed into text, number, "
                                  "money and date fields.")
                .arg(f.name);
        }
        QString problem;
        if (!InputMask::parse(f.inputMask, &problem)) {
            return QStringLiteral("The input mask of %1: %2").arg(f.name, problem);
        }
    }
    if (const CompiledRule r = compileTableRule(table, QStringLiteral("NEW"), currency); !r.ok()) {
        return r.error;
    }
    if (table.validationText.size() > 255) {
        return QStringLiteral("The table's validation text is longer than 255 characters.");
    }
    return {};
}

QStringList ruleTriggerStatements(const TableDef &table, const CurrencyFormat &currency)
{
    QStringList out;
    for (const FieldDef &f : table.fields) {
        const CompiledRule r = compileFieldRule(table, f, QStringLiteral("NEW"), currency);
        if (!r.ok() || r.sql.isEmpty()) {
            continue;
        }
        const QString message = fieldRuleMessage(f);
        out.append(trigger(triggerName(table.name, f.name, "i"), QStringLiteral("INSERT"), table.name, r.sql, message));
        out.append(trigger(triggerName(table.name, f.name, "u"), QStringLiteral("UPDATE OF %1").arg(q(f.name)),
                           table.name, r.sql, message));
    }
    const CompiledRule record = compileTableRule(table, QStringLiteral("NEW"), currency);
    if (record.ok() && !record.sql.isEmpty()) {
        const QString message = tableRuleMessage(table);
        out.append(trigger(triggerName(table.name, QString(), "i"), QStringLiteral("INSERT"), table.name, record.sql,
                           message));
        out.append(trigger(triggerName(table.name, QString(), "u"), QStringLiteral("UPDATE"), table.name, record.sql,
                           message));
    }
    return out;
}

QList<QPair<QString, QString>> ruleRefusals(const TableDef &before, const TableDef &after,
                                            const CurrencyFormat &currency)
{
    QList<QPair<QString, QString>> out;
    if (before.name.isEmpty()) {
        return out; // a new table has no records yet
    }
    const QString row = q(after.name);
    bool retyped = false;
    for (const FieldDef &f : after.fields) {
        const FieldDef *old = f.originalName.isEmpty() ? nullptr : before.field(f.originalName);
        retyped = retyped || (old != nullptr && old->type != f.type);
        if (f.validationRule.trimmed().isEmpty()) {
            continue;
        }
        const bool changed = old == nullptr || old->validationRule.trimmed() != f.validationRule.trimmed()
                             || old->type != f.type;
        const CompiledRule r = compileFieldRule(after, f, row, currency);
        if (changed && r.ok() && !r.sql.isEmpty()) {
            out.append({QStringLiteral("SELECT count(*) FROM %1 WHERE NOT (%2)").arg(row, r.sql),
                        QStringLiteral("%1 record(s) break the validation rule of %2 (%3). Correct them, or change the "
                                       "rule, first.")
                            .arg(QStringLiteral("%1"), f.name, f.validationRule.trimmed())});
        }
    }
    const bool tableChanged = before.validationRule.trimmed() != after.validationRule.trimmed() || retyped;
    const CompiledRule record = compileTableRule(after, row, currency);
    if (tableChanged && record.ok() && !record.sql.isEmpty()) {
        out.append({QStringLiteral("SELECT count(*) FROM %1 WHERE NOT (%2)").arg(row, record.sql),
                    QStringLiteral("%1 record(s) break the table's validation rule (%2). Correct them, or change the "
                                   "rule, first.")
                        .arg(QStringLiteral("%1"), after.validationRule.trimmed())});
    }
    return out;
}

} // namespace QindaOffice::Base
