// SPDX-License-Identifier: GPL-3.0-or-later
// M78 (D-087): the statistics conditional formats measure against and the
// colour scales, data bars and icon sets made from them
// (cond_format_internal.h). Excel's rules, worked from its documentation
// and the cfvo semantics of ECMA-376 §18.3.1.11: a percent threshold is
// min + (max − min) × p / 100, a percentile one is PERCENTILE.INC over the
// numbers, Excel 2010's automatic minimum and maximum include zero; only
// numbers (dates are numbers) get a colour, a bar or an icon.

#include "cond_format_internal.h"

#include "eval.h"

#include <QStringBuilder>

#include <algorithm>
#include <cmath>

namespace QindaOffice::Calc {

namespace {

using RuleOp = CondFormatRule::Op;
using Type = CfThreshold::Type;

// Everything the statistics of a rule depend on: the sheet, the cells, the
// rule's kind and its parameters (the thresholds are resolved into the
// statistics too).
quint64 statsKey(const CondFormatRule &rule, qint32 sheet)
{
    QString key = QString::number(sheet) % QLatin1Char('|') % QString::number(int(rule.op));
    for (const CellRange &range : rule.ranges()) {
        key += QStringLiteral("|%1,%2,%3,%4")
                   .arg(range.topLeft.row)
                   .arg(range.topLeft.col)
                   .arg(range.bottomRight.row)
                   .arg(range.bottomRight.col);
    }
    key += QStringLiteral("|%1,%2,%3,%4,%5,%6")
               .arg(rule.rank)
               .arg(int(rule.percent))
               .arg(int(rule.bottom))
               .arg(int(rule.aboveAverage))
               .arg(int(rule.equalAverage))
               .arg(rule.stdDev);
    for (const CfThreshold &threshold : rule.thresholds) {
        key += QLatin1Char('|') % QString::number(int(threshold.type)) % QLatin1Char(':') % threshold.value;
    }
    // An icon set without thresholds of its own takes as many default bands
    // as it has icons: the set is part of what the statistics are.
    key += QLatin1Char('|') % rule.iconSet;
    return quint64(qHash(key, 0x4d78));
}

// A cfvo value: a number, or a formula evaluated at the rule's anchor
// (Excel refuses relative references there, so no shift applies).
std::optional<double> numberOrFormula(const QString &raw, const CondFormatRule &rule, const EvalHost &host,
                                      qint32 sheet)
{
    QString text = raw.trimmed();
    bool ok = false;
    const double number = text.toDouble(&ok);
    if (ok) {
        return number;
    }
    if (text.startsWith(QLatin1Char('='))) {
        text.remove(0, 1);
    }
    const ParseResult parsed = parseFormula(text);
    if (!parsed.error.isEmpty() || !parsed.root) {
        return std::nullopt;
    }
    EvalContext ctx;
    ctx.host = &host;
    ctx.sheet = sheet;
    ctx.here = rule.range.topLeft;
    const Value value = evalNode(parsed.root, ctx);
    if (isErr(value)) {
        return std::nullopt;
    }
    return toNumber(value);
}

// PERCENTILE.INC over sorted numbers.
std::optional<double> percentileInc(const QVector<double> &sorted, double fraction)
{
    if (sorted.isEmpty()) {
        return std::nullopt;
    }
    const double clamped = std::clamp(fraction, 0.0, 1.0);
    const double rank = clamped * double(sorted.size() - 1);
    const qsizetype low = qsizetype(std::floor(rank));
    const qsizetype high = qMin(low + 1, sorted.size() - 1);
    return sorted.at(low) + (rank - double(low)) * (sorted.at(high) - sorted.at(low));
}

std::optional<double> resolveThreshold(const CfThreshold &threshold, const CondFormatRule &rule,
                                       const CfRangeStats &stats, const EvalHost &host, qint32 sheet)
{
    switch (threshold.type) {
    case Type::Min:
        return stats.hasNumbers ? std::optional<double>(stats.min) : std::nullopt;
    case Type::Max:
        return stats.hasNumbers ? std::optional<double>(stats.max) : std::nullopt;
    case Type::AutoMin:
        return stats.hasNumbers ? std::optional<double>(qMin(0.0, stats.min)) : std::nullopt;
    case Type::AutoMax:
        return stats.hasNumbers ? std::optional<double>(qMax(0.0, stats.max)) : std::nullopt;
    case Type::Number:
    case Type::Formula:
        return numberOrFormula(threshold.value, rule, host, sheet);
    case Type::Percent: {
        const std::optional<double> p = numberOrFormula(threshold.value, rule, host, sheet);
        if (!p.has_value() || !stats.hasNumbers) {
            return std::nullopt;
        }
        return stats.min + (stats.max - stats.min) * *p / 100.0;
    }
    case Type::Percentile: {
        const std::optional<double> p = numberOrFormula(threshold.value, rule, host, sheet);
        return p.has_value() ? percentileInc(stats.numbers, *p / 100.0) : std::nullopt;
    }
    }
    return std::nullopt;
}

// The thresholds a rule uses: its own, or Excel's defaults for its kind
// (min/max for scales and bars; equal percent bands for an icon set).
QList<CfThreshold> effectiveThresholds(const CondFormatRule &rule)
{
    if (!rule.thresholds.isEmpty()) {
        return rule.thresholds;
    }
    if (rule.op == RuleOp::IconSet) {
        QList<CfThreshold> bands;
        const int count = qMax(1, iconSetSize(rule.iconSet));
        for (int i = 0; i < count; ++i) {
            bands.append(CfThreshold{Type::Percent, QString::number(std::lround(100.0 * i / count)), true});
        }
        return bands;
    }
    return {CfThreshold{Type::Min, {}, true}, CfThreshold{Type::Max, {}, true}};
}

Rgb mix(const Rgb &a, const Rgb &b, double t)
{
    const auto channel = [t](quint8 x, quint8 y) {
        return quint8(std::clamp(qRound(double(x) + (double(y) - double(x)) * t), 0, 255));
    };
    return Rgb{channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b)};
}

} // namespace

QString duplicateKey(const Value &value)
{
    if (const auto *number = std::get_if<double>(&value)) {
        return QStringLiteral("n") + QString::number(*number, 'g', 15);
    }
    if (const auto *text = std::get_if<QString>(&value)) {
        return QStringLiteral("t") + text->toCaseFolded();
    }
    if (const auto *flag = std::get_if<bool>(&value)) {
        return *flag ? QStringLiteral("b1") : QStringLiteral("b0");
    }
    return {}; // blanks and errors count for neither kind
}

Value evalShifted(const NodePtr &ast, const CellAddr &anchor, const CellAddr &addr, const EvalHost &host,
                  qint32 sheet)
{
    if (!ast) {
        return Err::Value;
    }
    const NodePtr shifted = addr == anchor ? ast : shiftNode(ast, addr.row - anchor.row, addr.col - anchor.col);
    EvalContext ctx;
    ctx.host = &host;
    ctx.sheet = sheet;
    ctx.here = addr;
    return evalNode(shifted, ctx);
}

std::shared_ptr<const CfRangeStats> cfStatsFor(const CondFormatRule &rule, qint32 sheet, const EvalHost &host,
                                               CondFormatCache &cache)
{
    const quint64 key = statsKey(rule, sheet);
    if (const auto found = cache.stats.constFind(key); found != cache.stats.constEnd()) {
        return found.value();
    }
    auto stats = std::make_shared<CfRangeStats>();
    const bool counting = rule.op == RuleOp::Duplicate || rule.op == RuleOp::Unique;
    for (const CellRange &range : rule.ranges()) {
        for (const auto &[addr, value] : host.nonBlankCellsIn(sheet, range)) {
            Q_UNUSED(addr);
            if (counting) {
                if (const QString k = duplicateKey(value); !k.isEmpty()) {
                    ++stats->counts[k];
                }
            } else if (const auto *number = std::get_if<double>(&value)) {
                stats->numbers.append(*number);
            }
        }
    }
    std::sort(stats->numbers.begin(), stats->numbers.end());
    stats->hasNumbers = !stats->numbers.isEmpty();
    if (stats->hasNumbers) {
        stats->min = stats->numbers.first();
        stats->max = stats->numbers.last();
        double sum = 0;
        for (const double n : std::as_const(stats->numbers)) {
            sum += n;
        }
        const double count = double(stats->numbers.size());
        stats->mean = sum / count;
        if (stats->numbers.size() > 1) {
            double squares = 0;
            for (const double n : std::as_const(stats->numbers)) {
                squares += (n - stats->mean) * (n - stats->mean);
            }
            stats->stdDev = std::sqrt(squares / (count - 1.0));
        }
    }
    if (rule.op == RuleOp::ColorScale || rule.op == RuleOp::DataBar || rule.op == RuleOp::IconSet) {
        for (const CfThreshold &threshold : effectiveThresholds(rule)) {
            stats->thresholds.append(resolveThreshold(threshold, rule, *stats, host, sheet));
        }
    }
    if (rule.op == RuleOp::Top10 && stats->hasNumbers && rule.rank > 0) {
        // N items, or N percent of them (rounded down, at least one), ties
        // included: every value reaching the Nth one counts.
        const qsizetype count = stats->numbers.size();
        const qsizetype items = rule.percent ? qMax(qsizetype(1), qsizetype(std::floor(double(count) * rule.rank / 100.0)))
                                             : qsizetype(rule.rank);
        const qsizetype n = qMin(items, count);
        stats->cut = rule.bottom ? stats->numbers.at(n - 1) : stats->numbers.at(count - n);
        stats->cutValid = true;
    }
    cache.stats.insert(key, stats);
    return stats;
}

int iconSetSize(const QString &set)
{
    if (set == QLatin1String("NoIcons")) {
        return 0;
    }
    const QChar first = set.isEmpty() ? QLatin1Char('3') : set.at(0);
    return first.isDigit() ? qBound(3, first.digitValue(), 5) : 3;
}

std::optional<Rgb> colorScaleColor(const CondFormatRule &rule, double value, const CfRangeStats &stats)
{
    const qsizetype count = qMin(stats.thresholds.size(), rule.colors.size());
    if (count < 2) {
        return std::nullopt;
    }
    for (qsizetype i = 0; i < count; ++i) {
        if (!stats.thresholds.at(i).has_value()) {
            return std::nullopt;
        }
    }
    if (value <= *stats.thresholds.first()) {
        return rule.colors.first();
    }
    for (qsizetype i = 0; i + 1 < count; ++i) {
        const double low = *stats.thresholds.at(i);
        const double high = *stats.thresholds.at(i + 1);
        if (value <= high) {
            const double span = high - low;
            return mix(rule.colors.at(i), rule.colors.at(i + 1), span > 0 ? (value - low) / span : 1.0);
        }
    }
    return rule.colors.at(count - 1);
}

std::optional<DataBarFill> dataBarFill(const CondFormatRule &rule, double value, const CfRangeStats &stats)
{
    const std::optional<double> lowEnd = stats.thresholds.value(0);
    const std::optional<double> highEnd = stats.thresholds.value(1);
    if (!lowEnd.has_value() || !highEnd.has_value()) {
        return std::nullopt;
    }
    const DataBarStyle &style = rule.bar;
    const double lo = qMin(*lowEnd, *highEnd);
    const double hi = qMax(*lowEnd, *highEnd);
    const double shortest = std::clamp(style.minLength, 0, 100) / 100.0;
    const double longest = std::clamp(style.maxLength, 0, 100) / 100.0;
    const auto length = [shortest, longest](double share) {
        return shortest + std::clamp(share, 0.0, 1.0) * (longest - shortest);
    };
    DataBarFill fill;
    fill.color = rule.barColor;
    fill.gradient = style.gradient;
    fill.border = style.border;
    fill.rightToLeft = style.direction == DataBarStyle::Direction::RightToLeft;
    // Excel 2010's axis (x14): negative values run left from it in their own
    // colour. An Excel 2007 bar has none — every value grows from the left.
    const bool axis = style.x14 && style.axis != DataBarStyle::Axis::None && lo < 0;
    if (!axis) {
        fill.fraction = length(hi > lo ? (std::clamp(value, lo, hi) - lo) / (hi - lo) : 1.0);
        if (value < 0 && style.x14) {
            fill.negative = true;
            fill.color = style.negativeSameAsPositive ? rule.barColor : style.negativeColor;
            fill.border = style.negativeBorder.has_value() ? style.negativeBorder : style.border;
        }
        return fill;
    }
    double origin = 0;
    double positiveSpan = 0;
    double negativeSpan = 0;
    if (style.axis == DataBarStyle::Axis::Middle) {
        origin = 0.5;
        positiveSpan = negativeSpan = qMax(std::abs(lo), std::abs(hi));
    } else {
        positiveSpan = qMax(hi, 0.0);
        negativeSpan = std::abs(lo);
        origin = negativeSpan / (negativeSpan + positiveSpan);
    }
    fill.origin = origin;
    fill.axisColor = style.axisColor;
    if (value >= 0) {
        fill.fraction = (1.0 - origin) * (positiveSpan > 0 ? length(value / positiveSpan) : 0.0);
    } else {
        fill.negative = true;
        fill.fraction = origin * (negativeSpan > 0 ? length(-value / negativeSpan) : 0.0);
        fill.color = style.negativeSameAsPositive ? rule.barColor : style.negativeColor;
        fill.border = style.negativeBorder.has_value() ? style.negativeBorder : style.border;
    }
    return fill;
}

std::optional<IconPaint> iconPaintFor(const CondFormatRule &rule, double value, const CfRangeStats &stats)
{
    const qsizetype count = qMin(qsizetype(iconSetSize(rule.iconSet)), stats.thresholds.size());
    if (count < 2) {
        return std::nullopt;
    }
    const QList<CfThreshold> thresholds = effectiveThresholds(rule);
    int band = 0;
    for (qsizetype i = 1; i < count; ++i) {
        const std::optional<double> threshold = stats.thresholds.at(i);
        if (!threshold.has_value()) {
            continue;
        }
        const bool gte = thresholds.value(i).gte;
        if (gte ? value >= *threshold : value > *threshold) {
            band = int(i);
        }
    }
    return IconPaint{rule.iconSet, rule.reverse ? int(count) - 1 - band : band};
}

} // namespace QindaOffice::Calc
