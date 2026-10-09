// SPDX-License-Identifier: GPL-3.0-or-later
// Host-aware conditional-format matching and merging (sheet_rules.h):
// formulas evaluate shifted from the rule's anchor — the top-left cell of
// the FIRST range of its sqref (the shared-formula trick) —, the kinds that
// measure a cell against its neighbours read the statistics of the whole
// sqref (cond_format_stats.cpp), and the merge follows Excel's model — list
// order is descending precedence, the first matching rule to set a field
// wins it, and a stopIfTrue match ends the walk. M78 (D-087): every kind
// Excel writes.

#include "sheet_rules.h"

#include "cond_format_internal.h"

#include "eval.h"
#include "functions/functions.h"

#include <QDate>

namespace QindaOffice::Calc {

namespace {

using RuleOp = CondFormatRule::Op;

// A cellIs operand: its formula evaluated at `addr`, an equal / not-equal
// rule's typed text, or the number.
Value operandAt(const CondFormatRule &rule, bool second, const CellAddr &addr, const EvalHost &host, qint32 sheet)
{
    const QString &formula = second ? rule.formula2 : rule.formula;
    const NodePtr &ast = second ? rule.ast2 : rule.ast;
    if (!formula.isEmpty()) {
        return ast ? evalShifted(ast, rule.range.topLeft, addr, host, sheet) : Value(Err::Name);
    }
    if (!second && !rule.text.isEmpty() && (rule.op == RuleOp::EqualTo || rule.op == RuleOp::NotEqualTo)) {
        return rule.text;
    }
    return second ? rule.second : rule.first;
}

bool truthy(const Value &value)
{
    if (isErr(value)) {
        return false;
    }
    if (const auto *flag = std::get_if<bool>(&value)) {
        return *flag;
    }
    if (const auto *number = std::get_if<double>(&value)) {
        return *number != 0.0;
    }
    return false; // text and blanks are not TRUE
}

double today()
{
    return serialFromDate(QDate::currentDate());
}

} // namespace

bool condRuleMatchesHost(const CondFormatRule &rule, const CellAddr &addr, const EvalHost &host, qint32 sheet,
                         CondFormatCache &cache)
{
    const Value value = host.cellValue(sheet, addr.row, addr.col);
    if (isCellIsOp(rule.op)) {
        const bool two = rule.op == RuleOp::Between || rule.op == RuleOp::NotBetween;
        return cellIsMatches(rule.op, value, operandAt(rule, false, addr, host, sheet),
                             two ? operandAt(rule, true, addr, host, sheet) : Value());
    }
    switch (rule.op) {
    case RuleOp::Expression:
        return rule.ast != nullptr && truthy(evalShifted(rule.ast, rule.range.topLeft, addr, host, sheet));
    case RuleOp::TimePeriod:
        return timePeriodMatches(rule.period, value, today());
    case RuleOp::Top10: {
        if (!isNumber(value)) {
            return false;
        }
        const auto stats = cfStatsFor(rule, sheet, host, cache);
        const double v = std::get<double>(value);
        return stats->cutValid && (rule.bottom ? v <= stats->cut : v >= stats->cut);
    }
    case RuleOp::AboveAverage: {
        if (!isNumber(value)) {
            return false;
        }
        const auto stats = cfStatsFor(rule, sheet, host, cache);
        if (!stats->hasNumbers) {
            return false;
        }
        const double v = std::get<double>(value);
        const double offset = rule.stdDev > 0 ? rule.stdDev * stats->stdDev : 0.0;
        if (rule.aboveAverage) {
            const double bound = stats->mean + offset;
            return rule.equalAverage ? v >= bound : v > bound;
        }
        const double bound = stats->mean - offset;
        return rule.equalAverage ? v <= bound : v < bound;
    }
    case RuleOp::Duplicate:
    case RuleOp::Unique: {
        const QString key = duplicateKey(value);
        if (key.isEmpty()) {
            return false;
        }
        const int seen = cfStatsFor(rule, sheet, host, cache)->counts.value(key);
        return rule.op == RuleOp::Duplicate ? seen > 1 : seen == 1;
    }
    case RuleOp::DataBar:
    case RuleOp::ColorScale:
    case RuleOp::IconSet:
        // Only numbers get a bar, a colour or an icon (dates are numbers).
        return isNumber(value);
    default:
        return textRuleMatches(rule.op, value, rule.text);
    }
}

CondFormat condFormatForHost(const QList<CondFormatRule> &rules, qint32 sheet, const CellAddr &addr,
                             const EvalHost &host, CondFormatCache &cache)
{
    CondFormat result;
    for (const CondFormatRule &rule : rules) {
        if (!rule.covers(addr) || !condRuleMatchesHost(rule, addr, host, sheet, cache)) {
            continue;
        }
        // First-wins per field: the list is in precedence order, so a field
        // an earlier rule set is final.
        if (rule.op == RuleOp::ColorScale) {
            if (!result.background.has_value()) {
                const auto stats = cfStatsFor(rule, sheet, host, cache);
                result.background = colorScaleColor(rule, std::get<double>(host.cellValue(sheet, addr.row, addr.col)),
                                                    *stats);
            }
        } else if (rule.op == RuleOp::DataBar) {
            if (!result.dataBar.has_value()) {
                const auto stats = cfStatsFor(rule, sheet, host, cache);
                result.dataBar = dataBarFill(rule, std::get<double>(host.cellValue(sheet, addr.row, addr.col)), *stats);
                result.hideValue = result.hideValue || (result.dataBar.has_value() && !rule.showValue);
            }
        } else if (rule.op == RuleOp::IconSet) {
            if (!result.icon.has_value()) {
                const auto stats = cfStatsFor(rule, sheet, host, cache);
                result.icon = iconPaintFor(rule, std::get<double>(host.cellValue(sheet, addr.row, addr.col)), *stats);
                result.hideValue = result.hideValue || (result.icon.has_value() && !rule.showValue);
            }
        } else {
            const CondFormat &format = rule.format;
            if (!result.background.has_value() && format.background.has_value()) {
                result.background = format.background;
            }
            if (!result.foreground.has_value() && format.foreground.has_value()) {
                result.foreground = format.foreground;
            }
            if (!result.bold.has_value() && format.bold.has_value()) {
                result.bold = format.bold;
            }
            if (!result.italic.has_value() && format.italic.has_value()) {
                result.italic = format.italic;
            }
            if (!result.underline.has_value() && format.underline.has_value()) {
                result.underline = format.underline;
            }
            if (!result.strikeout.has_value() && format.strikeout.has_value()) {
                result.strikeout = format.strikeout;
            }
        }
        if (rule.stopIfTrue) {
            break;
        }
    }
    return result;
}

bool prepareCondRule(CondFormatRule &rule)
{
    rule.ast.reset();
    rule.ast2.reset();
    // Expression: its formula; cellIs: operands that are not plain numbers.
    // Other kinds keep no formula of their own (cfvo formulas are parsed
    // when their statistics are worked out).
    const bool usesFormulas = rule.op == RuleOp::Expression || isCellIsOp(rule.op);
    if (!usesFormulas) {
        return true;
    }
    if (rule.op == RuleOp::Expression && rule.formula.isEmpty()) {
        return false;
    }
    for (const auto &[text, ast] : {std::pair{&rule.formula, &rule.ast}, std::pair{&rule.formula2, &rule.ast2}}) {
        if (text->isEmpty()) {
            continue;
        }
        const ParseResult parsed = parseFormula(*text);
        if (!parsed.error.isEmpty() || !parsed.root) {
            rule.ast.reset();
            rule.ast2.reset();
            return false;
        }
        *ast = parsed.root;
    }
    return true;
}

} // namespace QindaOffice::Calc
