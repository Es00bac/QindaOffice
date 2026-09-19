// SPDX-License-Identifier: GPL-3.0-or-later
// Host-aware conditional-format matching and merging (sheet_rules.h):
// Expression rules evaluate their formula shifted from the range's top-left
// (the shared-formula trick), DataBar rules resolve their fraction over the
// range's numeric values, and the merge follows Excel's model — list order
// is descending precedence, the first matching rule to set a field wins it,
// and a stopIfTrue match ends the walk.

#include "sheet_rules.h"

#include "eval.h"

namespace QindaOffice::Calc {

namespace {

// Cache key for a rule's data-bar extent: the extent depends on the rule's
// range and nothing else, so two identical rules may share an entry; the
// workbook clears the cache on every recalculation.
quint64 extentKey(const CondFormatRule &rule)
{
    return (quint64(quint32(rule.range.topLeft.row)) << 40)
        ^ (quint64(quint32(rule.range.topLeft.col)) << 24)
        ^ (quint64(quint32(rule.range.bottomRight.row & 0xff)) << 12)
        ^ quint64(quint32(rule.range.bottomRight.col & 0xfff));
}

QPair<double, double> dataBarExtent(const CondFormatRule &rule, const EvalHost &host,
                                    qint32 sheet,
                                    QHash<quint64, QPair<double, double>> &extentCache)
{
    const quint64 key = extentKey(rule);
    if (extentCache.contains(key)) {
        return extentCache.value(key);
    }
    double min = std::numeric_limits<double>::max();
    double max = std::numeric_limits<double>::lowest();
    bool any = false;
    const QVector<QPair<CellAddr, Value>> cells = host.nonBlankCellsIn(sheet, rule.range);
    for (const auto &[addr, value] : cells) {
        const std::optional<double> number = toNumber(value);
        if (!number.has_value()) {
            continue;
        }
        any = true;
        min = qMin(min, *number);
        max = qMax(max, *number);
    }
    const QPair<double, double> extent = any ? qMakePair(min, max) : qMakePair(0.0, 0.0);
    extentCache.insert(key, extent);
    return extent;
}

} // namespace

bool condRuleMatchesHost(const CondFormatRule &rule, const CellAddr &addr,
                         const EvalHost &host, qint32 sheet)
{
    switch (rule.op) {
    case CondFormatRule::Op::Expression: {
        if (rule.ast == nullptr) {
            return false;
        }
        // The stored formula is relative to the range's top-left; shifting
        // it to the cell under test reproduces Excel's per-cell view.
        const NodePtr shifted = shiftNode(rule.ast, addr.row - rule.range.topLeft.row,
                                          addr.col - rule.range.topLeft.col);
        EvalContext ctx;
        ctx.host = &host;
        ctx.sheet = sheet;
        ctx.here = addr;
        const Value result = evalNode(shifted, ctx);
        if (isErr(result)) {
            return false;
        }
        if (const std::optional<double> number = toNumber(result)) {
            return *number != 0.0;
        }
        if (const auto *boolean = std::get_if<bool>(&result)) {
            return *boolean;
        }
        return false;
    }
    case CondFormatRule::Op::DataBar:
        // A bar only makes sense on a number (blank/text cells stay bare).
        return toNumber(host.cellValue(sheet, addr.row, addr.col)).has_value();
    default:
        return condRuleMatches(rule, host.cellValue(sheet, addr.row, addr.col));
    }
}

CondFormat condFormatForHost(const QList<CondFormatRule> &rules, qint32 sheet,
                             const CellAddr &addr, const EvalHost &host,
                             QHash<quint64, QPair<double, double>> &extentCache)
{
    CondFormat result;
    for (const CondFormatRule &rule : rules) {
        if (!rule.range.contains(addr) || !condRuleMatchesHost(rule, addr, host, sheet)) {
            continue;
        }
        // First-wins per field: the list is in precedence order, so a field
        // an earlier rule set is final.
        if (!result.background.has_value() && rule.format.background.has_value()) {
            result.background = rule.format.background;
        }
        if (!result.foreground.has_value() && rule.format.foreground.has_value()) {
            result.foreground = rule.format.foreground;
        }
        if (!result.bold.has_value() && rule.format.bold.has_value()) {
            result.bold = rule.format.bold;
        }
        if (!result.italic.has_value() && rule.format.italic.has_value()) {
            result.italic = rule.format.italic;
        }
        if (!result.dataBar.has_value() && rule.op == CondFormatRule::Op::DataBar) {
            const QPair<double, double> extent = dataBarExtent(rule, host, sheet, extentCache);
            const double value =
                toNumber(host.cellValue(sheet, addr.row, addr.col)).value_or(extent.first);
            const double span = extent.second - extent.first;
            const double fraction =
                span > 0.0 ? qBound(0.0, (value - extent.first) / span, 1.0) : 1.0;
            result.dataBar = DataBarFill{rule.barColor, fraction};
        }
        if (rule.stopIfTrue) {
            break;
        }
    }
    return result;
}

bool prepareCondRule(CondFormatRule &rule)
{
    if (rule.op != CondFormatRule::Op::Expression) {
        return true;
    }
    const ParseResult parsed = parseFormula(rule.formula);
    if (!parsed.error.isEmpty() || !parsed.root) {
        rule.ast.reset();
        return false;
    }
    rule.ast = parsed.root;
    return true;
}

} // namespace QindaOffice::Calc
