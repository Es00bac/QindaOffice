// SPDX-License-Identifier: GPL-3.0-or-later
// The value-level rule tests every path shares (cond_format_internal.h):
// cellIs comparisons, the text, blank, error and date-occurring kinds,
// the classic no-workbook path (condRuleMatches), and typed-input
// validation with its description (sheet_rules.h). M78 (D-087): Excel's
// semantics for every kind — see the AGENT-NOTE in cond_format_internal.h.

#include "sheet_rules.h"

#include "cond_format_internal.h"
#include "eval_array.h"
#include "functions/functions.h"

#include <QDate>
#include <QLocale>
#include <QRegularExpression>
#include <QTime>

#include <cmath>

namespace QindaOffice::Calc {

using RuleOp = CondFormatRule::Op;

namespace {

bool isTrue(const Value &value)
{
    return std::holds_alternative<bool>(value) && std::get<bool>(value);
}

// Excel's comparison of the cell's value with an operand; numbers are
// equal within Excel's 15 significant digits.
bool compareTrue(Op op, const Value &value, const Value &operand)
{
    if ((op == Op::Eq || op == Op::Ne) && isNumber(value) && isNumber(operand)) {
        const double a = std::get<double>(value);
        const double b = std::get<double>(operand);
        const bool same = a == b || std::abs(a - b) <= 1e-14 * std::max(std::abs(a), std::abs(b));
        return op == Op::Eq ? same : !same;
    }
    return isTrue(applyBinaryOp(op, value, operand));
}

// Excel's SEARCH: case-insensitive, `*` and `?` wildcards, `~` escapes.
qsizetype searchText(const QString &needle, const QString &haystack)
{
    if (needle.isEmpty()) {
        return 0;
    }
    if (!needle.contains(QLatin1Char('*')) && !needle.contains(QLatin1Char('?')) && !needle.contains(QLatin1Char('~'))) {
        return haystack.indexOf(needle, 0, Qt::CaseInsensitive);
    }
    QString pattern;
    for (qsizetype i = 0; i < needle.size(); ++i) {
        const QChar c = needle.at(i);
        if (c == QLatin1Char('~') && i + 1 < needle.size()) {
            pattern += QRegularExpression::escape(needle.at(++i));
        } else if (c == QLatin1Char('*')) {
            pattern += QStringLiteral(".*");
        } else if (c == QLatin1Char('?')) {
            pattern += QLatin1Char('.');
        } else {
            pattern += QRegularExpression::escape(c);
        }
    }
    const QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption
                                             | QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch match = re.match(haystack);
    return match.hasMatch() ? match.capturedStart() : -1;
}

// Excel's TRIM removes spaces only (not tabs or line breaks).
QString excelTrim(const QString &text)
{
    qsizetype first = 0;
    qsizetype last = text.size();
    while (first < last && text.at(first) == QLatin1Char(' ')) {
        ++first;
    }
    while (last > first && text.at(last - 1) == QLatin1Char(' ')) {
        --last;
    }
    return text.mid(first, last - first);
}

// Excel's WEEKDAY: Sunday is 1.
int weekday(double serial)
{
    return dateFromSerial(serial).dayOfWeek() % 7 + 1;
}

bool sameMonth(double serial, const QDate &month)
{
    const QDate date = dateFromSerial(serial);
    return date.month() == month.month() && date.year() == month.year();
}

bool numberPasses(ValidationRule::Operator op, double x, double a, double b)
{
    using O = ValidationRule::Operator;
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

} // namespace

bool isCellIsOp(RuleOp op)
{
    switch (op) {
    case RuleOp::GreaterThan:
    case RuleOp::LessThan:
    case RuleOp::Between:
    case RuleOp::NotBetween:
    case RuleOp::EqualTo:
    case RuleOp::NotEqualTo:
    case RuleOp::GreaterThanOrEqual:
    case RuleOp::LessThanOrEqual:
        return true;
    default:
        return false;
    }
}

bool condRuleNeedsHost(const CondFormatRule &rule)
{
    switch (rule.op) {
    case RuleOp::Expression:
    case RuleOp::DataBar:
    case RuleOp::Top10:
    case RuleOp::AboveAverage:
    case RuleOp::Duplicate:
    case RuleOp::Unique:
    case RuleOp::ColorScale:
    case RuleOp::IconSet:
        return true;
    default:
        // A cellIs operand that is a formula needs the workbook too.
        return isCellIsOp(rule.op) && (!rule.formula.isEmpty() || !rule.formula2.isEmpty());
    }
}

bool cellIsMatches(RuleOp op, const Value &value, const Value &first, const Value &second)
{
    if (isErr(value) || isErr(first)) {
        return false;
    }
    switch (op) {
    case RuleOp::GreaterThan:
        return compareTrue(Op::Gt, value, first);
    case RuleOp::GreaterThanOrEqual:
        return compareTrue(Op::Ge, value, first);
    case RuleOp::LessThan:
        return compareTrue(Op::Lt, value, first);
    case RuleOp::LessThanOrEqual:
        return compareTrue(Op::Le, value, first);
    case RuleOp::EqualTo:
        return compareTrue(Op::Eq, value, first);
    case RuleOp::NotEqualTo:
        return compareTrue(Op::Ne, value, first);
    case RuleOp::Between:
    case RuleOp::NotBetween: {
        if (isErr(second)) {
            return false;
        }
        // Excel takes the two bounds in either order.
        const bool swapped = isTrue(applyBinaryOp(Op::Gt, first, second));
        const Value &low = swapped ? second : first;
        const Value &high = swapped ? first : second;
        const bool inside = compareTrue(Op::Ge, value, low) && compareTrue(Op::Le, value, high);
        return op == RuleOp::Between ? inside : !inside;
    }
    default:
        return false;
    }
}

bool textRuleMatches(RuleOp op, const Value &value, const QString &text)
{
    const bool error = isErr(value);
    const QString shown = error ? QString() : toText(value);
    switch (op) {
    case RuleOp::ContainsText: // NOT(ISERROR(SEARCH(text,A1)))
        return !error && searchText(text, shown) >= 0;
    case RuleOp::NotContainsText: // ISERROR(SEARCH(text,A1))
        return error || searchText(text, shown) < 0;
    case RuleOp::BeginsWith: // LEFT(A1,LEN(text))=text
        return !error && shown.startsWith(text, Qt::CaseInsensitive);
    case RuleOp::EndsWith: // RIGHT(A1,LEN(text))=text
        return !error && shown.endsWith(text, Qt::CaseInsensitive);
    case RuleOp::IsEmpty: // LEN(TRIM(A1))=0
        return !error && excelTrim(shown).isEmpty();
    case RuleOp::NotEmpty: // LEN(TRIM(A1))>0
        return !error && !excelTrim(shown).isEmpty();
    case RuleOp::ContainsErrors:
        return error;
    case RuleOp::NotContainsErrors:
        return !error;
    default:
        return false;
    }
}

bool timePeriodMatches(CondFormatRule::Period period, const Value &value, double today)
{
    using P = CondFormatRule::Period;
    if (!isNumber(value)) {
        return false;
    }
    const double day = std::floor(std::get<double>(value));
    const int wd = weekday(today);
    const QDate now = dateFromSerial(today);
    switch (period) {
    case P::Today:
        return day == today;
    case P::Yesterday:
        return day == today - 1;
    case P::Tomorrow:
        return day == today + 1;
    case P::Last7Days:
        return today - day <= 6 && day <= today;
    case P::ThisWeek:
        return today - day <= wd - 1 && day - today <= 7 - wd;
    case P::LastWeek:
        return today - day >= wd && today - day < wd + 7;
    case P::NextWeek:
        return day - today > 7 - wd && day - today < 15 - wd;
    case P::ThisMonth:
        return sameMonth(day, now);
    case P::LastMonth:
        return sameMonth(day, now.addMonths(-1));
    case P::NextMonth:
        return sameMonth(day, now.addMonths(1));
    }
    return false;
}

bool condRuleMatches(const CondFormatRule &rule, const Value &value)
{
    if (condRuleNeedsHost(rule)) {
        return false;
    }
    if (isCellIsOp(rule.op)) {
        // An equal / not-equal rule typed with text compares with the text.
        const bool textOperand = (rule.op == RuleOp::EqualTo || rule.op == RuleOp::NotEqualTo) && !rule.text.isEmpty();
        return cellIsMatches(rule.op, value, textOperand ? Value(rule.text) : Value(rule.first), Value(rule.second));
    }
    if (rule.op == RuleOp::TimePeriod) {
        return timePeriodMatches(rule.period, value, serialFromDate(QDate::currentDate()));
    }
    return textRuleMatches(rule.op, value, rule.text);
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
    // Operands that are formulas need the workbook (validationAcceptsValue).
    const bool literalOperands = rule.formula1.isEmpty() && rule.formula2.isEmpty();
    switch (rule.kind) {
    case Kind::NotEmpty:
        return !input.trimmed().isEmpty();
    case Kind::Any:
        return true;
    case Kind::List:
        if (rule.list.isEmpty() && !rule.listSource.isEmpty()) {
            return true;
        }
        for (const QString &item : rule.list) {
            if (item.compare(input, Qt::CaseInsensitive) == 0) {
                return true;
            }
        }
        return false;
    case Kind::TextLength:
        return !literalOperands || numberPasses(rule.op, double(input.size()), rule.min, rule.max);
    case Kind::WholeNumber:
    case Kind::Decimal: {
        bool ok = false;
        const double number = input.toDouble(&ok);
        if (!ok) {
            return false;
        }
        if (rule.kind == Kind::WholeNumber && number != std::floor(number)) {
            return false;
        }
        return !literalOperands || numberPasses(rule.op, number, rule.min, rule.max);
    }
    case Kind::Date:
    case Kind::Time:
    case Kind::Custom:
        // The input's date or time reading and a custom formula need the
        // workbook: validationAcceptsValue decides those.
        return true;
    }
    return true;
}

QString validationDescription(const ValidationRule &rule)
{
    using Kind = ValidationRule::Kind;
    using O = ValidationRule::Operator;
    if (!rule.message.isEmpty()) {
        return rule.message;
    }
    const auto operand = [&rule](const QString &formula, double number) {
        if (!formula.isEmpty()) {
            return QLatin1Char('=') + formula;
        }
        if (rule.kind == Kind::Date) {
            return QLocale().toString(dateFromSerial(number), QLocale::ShortFormat);
        }
        if (rule.kind == Kind::Time) {
            const int seconds = int(std::llround((number - std::floor(number)) * 86400.0));
            return QTime(0, 0).addSecs(seconds).toString(QStringLiteral("HH:mm"));
        }
        return QString::number(number);
    };
    const QString a = operand(rule.formula1, rule.min);
    const QString b = operand(rule.formula2, rule.max);
    QString condition;
    switch (rule.op) {
    case O::Between: condition = QStringLiteral("between %1 and %2").arg(a, b); break;
    case O::NotBetween: condition = QStringLiteral("not between %1 and %2").arg(a, b); break;
    case O::Equal: condition = QStringLiteral("equal to %1").arg(a); break;
    case O::NotEqual: condition = QStringLiteral("other than %1").arg(a); break;
    case O::GreaterThan: condition = QStringLiteral("greater than %1").arg(a); break;
    case O::LessThan: condition = QStringLiteral("less than %1").arg(a); break;
    case O::GreaterThanOrEqual: condition = QStringLiteral("greater than or equal to %1").arg(a); break;
    case O::LessThanOrEqual: condition = QStringLiteral("less than or equal to %1").arg(a); break;
    }
    switch (rule.kind) {
    case Kind::WholeNumber:
        return QStringLiteral("a whole number %1").arg(condition);
    case Kind::Decimal:
        return QStringLiteral("a number %1").arg(condition);
    case Kind::Date:
        return QStringLiteral("a date %1").arg(condition);
    case Kind::Time:
        return QStringLiteral("a time %1").arg(condition);
    case Kind::List:
        if (rule.list.isEmpty() && !rule.listSource.isEmpty()) {
            return QStringLiteral("one of the values in %1").arg(rule.listSource);
        }
        return QStringLiteral("one of: %1").arg(rule.list.join(QStringLiteral(", ")));
    case Kind::TextLength:
        if (rule.op == O::Between && rule.formula1.isEmpty() && rule.formula2.isEmpty()) {
            return QStringLiteral("text of %1 to %2 characters").arg(int(rule.min)).arg(int(rule.max));
        }
        return QStringLiteral("text whose length is %1").arg(condition);
    case Kind::NotEmpty:
        return QStringLiteral("a value (this cell cannot be left empty)");
    case Kind::Custom:
        return QStringLiteral("a value for which =%1 is TRUE").arg(rule.formula1);
    case Kind::Any:
        return QStringLiteral("any value");
    }
    return {};
}

bool prepareValidationRule(ValidationRule &rule)
{
    rule.ast1.reset();
    rule.ast2.reset();
    // A list's live source is parsed into ast1 when it has no formula1 of
    // its own (validationAcceptsValue reads the source's current cells).
    const QString first = rule.formula1.isEmpty() && rule.kind == ValidationRule::Kind::List ? rule.listSource
                                                                                            : rule.formula1;
    for (const auto &[text, ast] : {std::pair<const QString *, NodePtr *>{&first, &rule.ast1},
                                    std::pair<const QString *, NodePtr *>{&rule.formula2, &rule.ast2}}) {
        if (text->isEmpty()) {
            continue;
        }
        const ParseResult parsed = parseFormula(*text);
        if (!parsed.error.isEmpty() || !parsed.root) {
            rule.ast1.reset();
            rule.ast2.reset();
            return false;
        }
        *ast = parsed.root;
    }
    return true;
}

} // namespace QindaOffice::Calc
