// SPDX-License-Identifier: GPL-3.0-or-later
#include "base_macro_flow.h"
#include "base_expression.h"
#include "base_records.h"
#include "base_schema.h"

#include <QHash>

namespace QindaOffice::Base {
namespace {
QString s(const char *text) { return QString::fromUtf8(text); }
FieldType typeOf(const QVariant &value)
{
    switch (value.typeId()) {
    case QMetaType::Bool: return FieldType::YesNo;
    case QMetaType::Int:
    case QMetaType::LongLong: return FieldType::Integer;
    case QMetaType::Double: return FieldType::Number;
    default: return FieldType::Text;
    }
}
QString expressionText(QString value)
{
    value = value.trimmed();
    if (value.startsWith(QLatin1Char('='))) value.remove(0, 1);
    return value;
}
}

MacroValue evaluateMacroValue(const BaseFile &file, const QString &table, const QString &expression,
                              const QVariantMap &record, const QVariantMap &temporaryValues,
                              const QString &targetField)
{
    MacroValue out;
    const auto columns = table.isEmpty() ? QList<ColumnInfo>() : Records::columns(file, table);
    if (!table.isEmpty() && columns.isEmpty()) {
        out.error = s("There is no table named %1.").arg(table);
        return out;
    }
    QHash<QString, Expr::FieldRef> fields, variables;
    QStringList values;
    QVariantList bindings;
    std::optional<FieldType> target;
    for (const ColumnInfo &column : columns) {
        const QString alias = s("value_%1").arg(values.size());
        fields.insert(column.name.toCaseFolded(), {quoteIdentifier(alias), column.type});
        if (column.name.compare(targetField, Qt::CaseInsensitive) == 0) {
            if (column.readOnly || column.calculated || column.linked) {
                out.error = s("%1 is read-only.").arg(column.label());
                return out;
            }
            target = column.type;
        }
        QVariant stored;
        const QVariant shown = record.value(column.name);
        if (shown.isValid() && !shown.isNull() && !shown.toString().isEmpty() && column.type != FieldType::File) {
            const Parsed parsed = Records::resolveInput(file, column, shown);
            if (!parsed.ok) {
                out.error = s("%1: %2").arg(column.label(), parsed.error);
                return out;
            }
            stored = parsed.value;
        }
        values.append(s("? AS %1").arg(quoteIdentifier(alias)));
        bindings.append(stored);
    }
    if (!targetField.isEmpty() && !target) {
        out.error = s("There is no field named %1.").arg(targetField);
        return out;
    }
    if (target && (*target == FieldType::File || *target == FieldType::AutoNumber)) {
        out.error = s("SetValue cannot change file or automatic-number fields.");
        return out;
    }
    for (auto it = temporaryValues.cbegin(); it != temporaryValues.cend(); ++it) {
        const QString alias = s("value_%1").arg(values.size());
        const QVariantMap boxed = it.value().toMap();
        const QVariant value = boxed.contains(s("value")) ? boxed.value(s("value")) : it.value();
        const FieldType kind = boxed.contains(s("type")) ? typeFromId(boxed.value(s("type")).toString()).value_or(typeOf(value))
                                                          : typeOf(value);
        variables.insert(it.key().toCaseFolded(), {quoteIdentifier(alias), kind});
        values.append(s("? AS %1").arg(quoteIdentifier(alias)));
        bindings.append(value);
    }
    Expr::Context context;
    context.currency = file.currency();
    context.resolve = [&](const QString &owner, const QString &name) -> std::optional<Expr::FieldRef> {
        const auto &map = owner.compare(s("TempVars"), Qt::CaseInsensitive) == 0 ? variables : fields;
        if (!owner.isEmpty() && owner.compare(s("TempVars"), Qt::CaseInsensitive) != 0
            && owner.compare(table, Qt::CaseInsensitive) != 0) return std::nullopt;
        const auto found = map.constFind(name.toCaseFolded());
        return found == map.cend() ? std::nullopt : std::optional<Expr::FieldRef>(*found);
    };
    const QString text = expressionText(expression);
    const Expr::Compiled compiled = target ? Expr::valueFor(text, *target, context) : Expr::expression(text, context);
    if (!compiled.ok() || compiled.aggregate) {
        out.error = compiled.ok() ? s("A form macro expression applies to one record, without aggregates.") : compiled.error;
        return out;
    }
    const QString from = values.isEmpty() ? QString() : s(" FROM (SELECT %1)").arg(values.join(s(", ")));
    const auto rows = file.db().rows(s("SELECT %1%2").arg(compiled.sql, from), bindings, &out.error);
    if (!out.error.isEmpty() || rows.isEmpty()) {
        if (out.error.isEmpty()) out.error = s("The expression did not return a value.");
        return out;
    }
    out.value = rows.first().value(0);
    const FieldType kind = target.value_or(compiled.type.value_or(typeOf(out.value)));
    out.text = out.value.isNull() ? QString() : displayText(kind, out.value, file.currency());
    out.type = kind;
    out.input = out.value.isNull() ? QVariant() : kind == FieldType::YesNo ? QVariant(out.value.toBool()) : QVariant(out.text);
    out.ok = true;
    return out;
}

QStringList checkMacroFlow(const Macro &macro)
{
    QStringList errors;
    QList<bool> branches;
    Expr::Context context;
    context.resolve = [](const QString &, const QString &) -> std::optional<Expr::FieldRef> {
        return Expr::FieldRef{s("field"), FieldType::Text};
    };
    if (macro.steps.size() > 1024) errors.append(s("A macro has at most 1024 actions."));
    for (int i = 0; i < macro.steps.size(); ++i) {
        const auto &step = macro.steps[i];
        const QString prefix = s("Step %1: ").arg(i + 1);
        if (step.action == s("if")) {
            branches.append(false);
            if (branches.size() > 20) errors.append(prefix + s("If blocks nest at most 20 levels."));
        } else if (step.action == s("else")) {
            if (branches.isEmpty() || branches.last()) errors.append(prefix + s("Else needs one matching If."));
            else branches.last() = true;
        } else if (step.action == s("endIf")) {
            if (branches.isEmpty()) errors.append(prefix + s("EndIf has no matching If."));
            else branches.removeLast();
        }
        const QString expression = step.arguments.value(step.action == s("if") ? s("condition") : s("expression")).toString();
        if (!expression.isEmpty()) {
            const auto compiled = Expr::expression(expressionText(expression), context);
            if (!compiled.ok()) errors.append(prefix + compiled.error);
            else if (compiled.aggregate) errors.append(prefix + s("An event expression cannot aggregate records."));
        }
    }
    if (!branches.isEmpty()) errors.append(s("An If block is missing EndIf."));
    return errors;
}
} // namespace QindaOffice::Base
