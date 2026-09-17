// SPDX-License-Identifier: GPL-3.0-or-later
#include "sheet_rules.h"

namespace QindaOffice::Calc {

bool condRuleMatches(const CondFormatRule &rule, const Value &value)
{
    using Op = CondFormatRule::Op;
    const bool blank = isBlank(value);
    if (rule.op == Op::IsEmpty) {
        return blank;
    }
    if (rule.op == Op::NotEmpty) {
        return !blank;
    }
    if (blank) {
        // A blank cell satisfies no comparison. Treating it as zero would
        // paint every empty cell of a "less than 10" rule.
        return false;
    }
    if (rule.op == Op::ContainsText) {
        return !rule.text.isEmpty()
            && toText(value).contains(rule.text, Qt::CaseInsensitive);
    }
    const std::optional<double> number = toNumber(value);
    if (!number.has_value()) {
        // Non-numeric text against a numeric rule: EqualTo/NotEqualTo still
        // mean something as a text comparison, the orderings do not.
        if (rule.op == Op::EqualTo || rule.op == Op::NotEqualTo) {
            const bool same = toText(value).compare(rule.text, Qt::CaseInsensitive) == 0;
            return rule.op == Op::EqualTo ? same : !same;
        }
        return false;
    }
    switch (rule.op) {
    case Op::GreaterThan:
        return *number > rule.first;
    case Op::LessThan:
        return *number < rule.first;
    case Op::Between:
        return *number >= qMin(rule.first, rule.second)
            && *number <= qMax(rule.first, rule.second);
    case Op::EqualTo:
        return qFuzzyCompare(*number + 1.0, rule.first + 1.0);
    case Op::NotEqualTo:
        return !qFuzzyCompare(*number + 1.0, rule.first + 1.0);
    case Op::ContainsText:
    case Op::IsEmpty:
    case Op::NotEmpty:
        break;
    }
    return false;
}

bool validationAccepts(const ValidationRule &rule, const QString &input)
{
    using Kind = ValidationRule::Kind;
    if (input.isEmpty()) {
        // AGENT-GUARD: clearing a cell is never refused. A rule that blocks
        // the empty string traps a mistyped value in the sheet — the user
        // can neither fix it nor delete it.
        return rule.kind != Kind::NotEmpty;
    }
    switch (rule.kind) {
    case Kind::NotEmpty:
        return !input.trimmed().isEmpty();
    case Kind::List:
        for (const QString &item : rule.list) {
            if (item.compare(input, Qt::CaseInsensitive) == 0) {
                return true;
            }
        }
        return false;
    case Kind::TextLength: {
        const double length = input.size();
        return length >= rule.min && length <= rule.max;
    }
    case Kind::WholeNumber:
    case Kind::Decimal: {
        bool ok = false;
        const double number = input.toDouble(&ok);
        if (!ok) {
            return false;
        }
        if (rule.kind == Kind::WholeNumber && !qFuzzyCompare(number, qRound(number) * 1.0)) {
            return false;
        }
        return number >= rule.min && number <= rule.max;
    }
    }
    return true;
}

QString validationDescription(const ValidationRule &rule)
{
    using Kind = ValidationRule::Kind;
    if (!rule.message.isEmpty()) {
        return rule.message;
    }
    switch (rule.kind) {
    case Kind::WholeNumber:
        return QStringLiteral("a whole number between %1 and %2")
            .arg(rule.min)
            .arg(rule.max);
    case Kind::Decimal:
        return QStringLiteral("a number between %1 and %2").arg(rule.min).arg(rule.max);
    case Kind::List:
        return QStringLiteral("one of: %1").arg(rule.list.join(QStringLiteral(", ")));
    case Kind::TextLength:
        return QStringLiteral("text of %1 to %2 characters")
            .arg(int(rule.min))
            .arg(int(rule.max));
    case Kind::NotEmpty:
        return QStringLiteral("a value (this cell cannot be left empty)");
    }
    return {};
}

} // namespace QindaOffice::Calc
