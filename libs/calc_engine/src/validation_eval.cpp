// SPDX-License-Identifier: GPL-3.0-or-later
// M78 (D-087): data validation with the workbook in hand (sheet_rules.h,
// validationAcceptsValue) — every kind and operator, formula operands,
// Custom formulas, a list's live source; what Circle Invalid Data and typed
// input ask. The verdict is the rule's alone: whether an invalid entry is
// refused, questioned or let through (Stop / Warning / Information, and
// showError) is the caller's to apply.

#include "sheet_rules.h"

#include "cond_format_internal.h"
#include "eval.h"
#include "eval_array.h"

#include <cmath>

namespace QindaOffice::Calc {

namespace {

using Kind = ValidationRule::Kind;
using O = ValidationRule::Operator;

// A formula of the rule (relative to the top-left cell of its first range)
// at `addr`, as an argument: a range stays a range (a list's source).
Arg evalRuleArg(const QString &formula, const NodePtr &prepared, const ValidationRule &rule, const CellAddr &addr,
                const EvalHost &host, qint32 sheet)
{
    NodePtr ast = prepared;
    if (!ast) {
        const ParseResult parsed = parseFormula(formula);
        if (!parsed.error.isEmpty() || !parsed.root) {
            return Value(Err::Name);
        }
        ast = parsed.root;
    }
    const CellAddr anchor = rule.range.topLeft;
    const NodePtr shifted = addr == anchor ? ast : shiftNode(ast, addr.row - anchor.row, addr.col - anchor.col);
    EvalContext ctx;
    ctx.host = &host;
    ctx.sheet = sheet;
    ctx.here = addr;
    return evalArg(shifted, ctx);
}

Value evalRuleValue(const QString &formula, const NodePtr &prepared, const ValidationRule &rule,
                    const CellAddr &addr, const EvalHost &host, qint32 sheet)
{
    if (prepared) {
        return evalShifted(prepared, rule.range.topLeft, addr, host, sheet);
    }
    const Arg arg = evalRuleArg(formula, prepared, rule, addr, host, sheet);
    if (const auto *value = std::get_if<Value>(&arg)) {
        return *value;
    }
    if (const auto *view = std::get_if<RangeView>(&arg)) {
        return view->isValid() ? host.cellValue(view->sheet, view->range.topLeft.row, view->range.topLeft.col)
                               : Value(Err::Ref);
    }
    const auto &array = std::get<ArrayValue>(arg);
    return array.cells.isEmpty() ? Value(Err::Value) : array.cells.first();
}

// An operand: its formula's value, or the number.
std::optional<double> operand(const ValidationRule &rule, bool second, const CellAddr &addr, const EvalHost &host,
                              qint32 sheet)
{
    const QString &formula = second ? rule.formula2 : rule.formula1;
    if (formula.isEmpty()) {
        return second ? rule.max : rule.min;
    }
    const Value value = evalRuleValue(formula, second ? rule.ast2 : rule.ast1, rule, addr, host, sheet);
    if (isErr(value)) {
        return std::nullopt;
    }
    return toNumber(value);
}

bool passes(O op, double x, double a, double b)
{
    switch (op) {
    case O::Between:
        return x >= qMin(a, b) && x <= qMax(a, b);
    case O::NotBetween:
        return x < qMin(a, b) || x > qMax(a, b);
    case O::Equal:
        return x == a;
    case O::NotEqual:
        return x != a;
    case O::GreaterThan:
        return x > a;
    case O::LessThan:
        return x < a;
    case O::GreaterThanOrEqual:
        return x >= a;
    case O::LessThanOrEqual:
        return x <= a;
    }
    return true;
}

bool numberPasses(const ValidationRule &rule, double x, const CellAddr &addr, const EvalHost &host, qint32 sheet)
{
    const std::optional<double> a = operand(rule, false, addr, host, sheet);
    const bool two = rule.op == O::Between || rule.op == O::NotBetween;
    const std::optional<double> b = two ? operand(rule, true, addr, host, sheet) : std::optional<double>(0.0);
    return a.has_value() && b.has_value() && passes(rule.op, x, *a, *b);
}

bool truthy(const Value &value)
{
    if (const auto *flag = std::get_if<bool>(&value)) {
        return *flag;
    }
    if (const auto *number = std::get_if<double>(&value)) {
        return *number != 0.0;
    }
    return false;
}

// The items a list offers now: the typed ones, or the source's cells.
QStringList listItems(const ValidationRule &rule, const CellAddr &addr, const EvalHost &host, qint32 sheet)
{
    if (rule.listSource.isEmpty()) {
        return rule.list;
    }
    const Arg arg = evalRuleArg(rule.listSource, rule.ast1, rule, addr, host, sheet);
    EvalContext ctx;
    ctx.host = &host;
    ctx.sheet = sheet;
    ctx.here = addr;
    QVector<Value> values;
    if (isErr(flatArgValues(ctx, arg, &values))) {
        return rule.list; // the values read at load, when the source no longer answers
    }
    QStringList items;
    for (const Value &value : std::as_const(values)) {
        if (!isBlank(value)) {
            items.append(toText(value));
        }
    }
    return items;
}

} // namespace

bool validationAcceptsValue(const ValidationRule &rule, const CellAddr &addr, const Value &value,
                            const EvalHost &host, qint32 sheet)
{
    const bool blank = isBlank(value) || (isText(value) && std::get<QString>(value).isEmpty());
    if (blank) {
        // An empty cell is never invalid (clearing is never refused), but
        // for a NotEmpty rule; a Custom rule without "Ignore blank" asks its
        // formula, as Excel does.
        if (rule.kind == Kind::NotEmpty) {
            return false;
        }
        if (rule.kind != Kind::Custom || rule.allowBlank) {
            return true;
        }
    }
    switch (rule.kind) {
    case Kind::Any:
        return true;
    case Kind::NotEmpty:
        return !isErr(value) && !toText(value).trimmed().isEmpty();
    case Kind::Custom:
        return truthy(evalRuleValue(rule.formula1, rule.ast1, rule, addr, host, sheet));
    case Kind::List: {
        if (isErr(value)) {
            return false;
        }
        const QString text = toText(value);
        for (const QString &item : listItems(rule, addr, host, sheet)) {
            if (item.compare(text, Qt::CaseInsensitive) == 0) {
                return true;
            }
        }
        return false;
    }
    case Kind::TextLength:
        return !isErr(value) && numberPasses(rule, double(toText(value).size()), addr, host, sheet);
    case Kind::WholeNumber:
    case Kind::Decimal:
    case Kind::Date:
    case Kind::Time: {
        // Excel checks the stored value's type: text — even "5" — is not a
        // number, a date or a time.
        if (!isNumber(value)) {
            return false;
        }
        const double x = std::get<double>(value);
        if (rule.kind == Kind::WholeNumber && x != std::floor(x)) {
            return false;
        }
        return numberPasses(rule, x, addr, host, sheet);
    }
    }
    return true;
}

} // namespace QindaOffice::Calc
