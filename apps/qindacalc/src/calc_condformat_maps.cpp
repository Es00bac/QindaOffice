// SPDX-License-Identifier: GPL-3.0-or-later
// Conditional-format rules as dialog maps (calc_condformat_maps.h, M78,
// D-087).

#include "calc_condformat_maps.h"

#include "chart_spec.h"
#include "parser.h"

#include <QColor>
#include <QCoreApplication>
#include <QRegularExpression>

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;
using Op = CondFormatRule::Op;

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("CondFormatRules", text);
}

QString colorText(const std::optional<Rgb> &rgb)
{
    return rgb ? QColor(rgb->r, rgb->g, rgb->b).name() : QString();
}

std::optional<Rgb> colorFrom(const QVariant &value)
{
    const QColor color = QColor::fromString(value.toString());
    if (value.toString().isEmpty() || !color.isValid()) {
        return std::nullopt;
    }
    return Rgb{quint8(color.red()), quint8(color.green()), quint8(color.blue())};
}

Rgb rgb(quint8 r, quint8 g, quint8 b)
{
    return {r, g, b};
}

const char *periodName(CondFormatRule::Period period)
{
    static const char *names[] = {QT_TRANSLATE_NOOP("CondFormatRules", "Today"), QT_TRANSLATE_NOOP("CondFormatRules", "Yesterday"),
                                  QT_TRANSLATE_NOOP("CondFormatRules", "Tomorrow"), QT_TRANSLATE_NOOP("CondFormatRules", "In the last 7 days"),
                                  QT_TRANSLATE_NOOP("CondFormatRules", "This week"), QT_TRANSLATE_NOOP("CondFormatRules", "Last week"),
                                  QT_TRANSLATE_NOOP("CondFormatRules", "Next week"), QT_TRANSLATE_NOOP("CondFormatRules", "This month"),
                                  QT_TRANSLATE_NOOP("CondFormatRules", "Last month"), QT_TRANSLATE_NOOP("CondFormatRules", "Next month")};
    return names[qBound(0, int(period), 9)];
}

// An operand as the description shows it: the formula when there is one.
QString operand(const QString &formula, double number)
{
    return formula.isEmpty() ? QString::number(number) : QLatin1Char('=') + formula;
}

// The rest of `range` once `cut` is taken out: up to four rectangles.
QList<CellRange> subtract(const CellRange &range, const CellRange &cut)
{
    const bool apart = cut.bottomRight.row < range.topLeft.row || cut.topLeft.row > range.bottomRight.row
        || cut.bottomRight.col < range.topLeft.col || cut.topLeft.col > range.bottomRight.col;
    if (apart) {
        return {range};
    }
    QList<CellRange> out;
    if (cut.topLeft.row > range.topLeft.row) {
        out.append(CellRange{range.topLeft, {cut.topLeft.row - 1, range.bottomRight.col}});
    }
    if (cut.bottomRight.row < range.bottomRight.row) {
        out.append(CellRange{{cut.bottomRight.row + 1, range.topLeft.col}, range.bottomRight});
    }
    const qint32 top = qMax(range.topLeft.row, cut.topLeft.row);
    const qint32 bottom = qMin(range.bottomRight.row, cut.bottomRight.row);
    if (cut.topLeft.col > range.topLeft.col) {
        out.append(CellRange{{top, range.topLeft.col}, {bottom, cut.topLeft.col - 1}});
    }
    if (cut.bottomRight.col < range.bottomRight.col) {
        out.append(CellRange{{top, cut.bottomRight.col + 1}, {bottom, range.bottomRight.col}});
    }
    return out;
}

// A formula relative to `from` re-anchored at `to` (it means the same at every cell).
QString reanchored(const QString &formula, const CellAddr &from, const CellAddr &to)
{
    if (formula.isEmpty() || from == to) {
        return formula;
    }
    const ParseResult parsed = parseFormula(formula);
    return parsed.root && parsed.error.isEmpty() ? nodeText(shiftNode(parsed.root, to.row - from.row, to.col - from.col)) : formula;
}

} // namespace

QString sqrefText(const QList<CellRange> &ranges)
{
    QStringList parts;
    for (const CellRange &range : ranges) {
        parts.append(range.topLeft == range.bottomRight ? addrText(range.topLeft)
                                                        : addrText(range.topLeft) + QLatin1Char(':') + addrText(range.bottomRight));
    }
    return parts.join(QLatin1Char(' '));
}

QList<CellRange> sqrefFromText(const QString &text)
{
    QList<CellRange> ranges;
    for (QString part : text.split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts)) {
        part.remove(QLatin1Char('$'));
        const CellRange range = parseChartRange(part.contains(QLatin1Char(':')) ? part : part + QLatin1Char(':') + part);
        if (!range.isValid()) {
            return {};
        }
        ranges.append(range);
    }
    return ranges;
}

QVariantMap condRuleToMap(const CondFormatRule &rule)
{
    QVariantList thresholds;
    for (const CfThreshold &t : rule.thresholds) {
        thresholds.append(QVariantMap{{QStringLiteral("type"), int(t.type)}, {QStringLiteral("value"), t.value}, {QStringLiteral("gte"), t.gte}});
    }
    QVariantList colors;
    for (const Rgb &c : rule.colors) {
        colors.append(colorText(c));
    }
    return {
        {QStringLiteral("range"), sqrefText(rule.ranges())},
        {QStringLiteral("op"), int(rule.op)},
        {QStringLiteral("first"), rule.first},
        {QStringLiteral("second"), rule.second},
        {QStringLiteral("text"), rule.text},
        {QStringLiteral("formula"), rule.formula},
        {QStringLiteral("formula2"), rule.formula2},
        {QStringLiteral("rank"), rule.rank},
        {QStringLiteral("percent"), rule.percent},
        {QStringLiteral("bottom"), rule.bottom},
        {QStringLiteral("aboveAverage"), rule.aboveAverage},
        {QStringLiteral("equalAverage"), rule.equalAverage},
        {QStringLiteral("stdDev"), rule.stdDev},
        {QStringLiteral("period"), int(rule.period)},
        {QStringLiteral("thresholds"), thresholds},
        {QStringLiteral("colors"), colors},
        {QStringLiteral("iconSet"), rule.iconSet},
        {QStringLiteral("reverse"), rule.reverse},
        {QStringLiteral("showValue"), rule.showValue},
        {QStringLiteral("barColor"), colorText(rule.barColor)},
        {QStringLiteral("barGradient"), rule.bar.gradient},
        {QStringLiteral("barBorder"), colorText(rule.bar.border)},
        {QStringLiteral("barNegative"), colorText(rule.bar.negativeColor)},
        {QStringLiteral("barAxis"), int(rule.bar.axis)},
        {QStringLiteral("barDirection"), int(rule.bar.direction)},
        {QStringLiteral("background"), colorText(rule.format.background)},
        {QStringLiteral("foreground"), colorText(rule.format.foreground)},
        {QStringLiteral("bold"), rule.format.bold.value_or(false)},
        {QStringLiteral("italic"), rule.format.italic.value_or(false)},
        {QStringLiteral("underline"), rule.format.underline.value_or(false)},
        {QStringLiteral("strikeout"), rule.format.strikeout.value_or(false)},
        {QStringLiteral("stopIfTrue"), rule.stopIfTrue},
        {QStringLiteral("description"), condRuleDescription(rule)},
    };
}

CondFormatRule condRuleFromMap(const QVariantMap &map, const CondFormatRule &base)
{
    CondFormatRule rule = base;
    const auto has = [&map](const char *key) { return map.contains(QString::fromLatin1(key)); };
    const auto get = [&map](const char *key) { return map.value(QString::fromLatin1(key)); };
    if (has("range")) {
        const QList<CellRange> ranges = sqrefFromText(get("range").toString());
        rule.range = ranges.value(0);
        rule.moreRanges = ranges.mid(1);
    }
    if (has("op")) {
        rule.op = Op(qBound(0, get("op").toInt(), int(Op::IconSet)));
    }
    if (has("first")) rule.first = get("first").toDouble();
    if (has("second")) rule.second = get("second").toDouble();
    if (has("text")) rule.text = get("text").toString();
    if (has("formula")) rule.formula = get("formula").toString().trimmed().remove(QRegularExpression(QStringLiteral("^=")));
    if (has("formula2")) rule.formula2 = get("formula2").toString().trimmed().remove(QRegularExpression(QStringLiteral("^=")));
    if (has("rank")) rule.rank = qBound(1, get("rank").toInt(), 1000);
    if (has("percent")) rule.percent = get("percent").toBool();
    if (has("bottom")) rule.bottom = get("bottom").toBool();
    if (has("aboveAverage")) rule.aboveAverage = get("aboveAverage").toBool();
    if (has("equalAverage")) rule.equalAverage = get("equalAverage").toBool();
    if (has("stdDev")) rule.stdDev = qBound(0, get("stdDev").toInt(), 3);
    if (has("period")) rule.period = CondFormatRule::Period(qBound(0, get("period").toInt(), 9));
    if (has("thresholds")) {
        rule.thresholds.clear();
        for (const QVariant &entry : get("thresholds").toList()) {
            const QVariantMap t = entry.toMap();
            rule.thresholds.append({CfThreshold::Type(qBound(0, t.value(QStringLiteral("type")).toInt(), int(CfThreshold::Type::AutoMax))),
                                    t.value(QStringLiteral("value")).toString(), t.value(QStringLiteral("gte"), true).toBool()});
        }
    }
    if (has("colors")) {
        rule.colors.clear();
        for (const QVariant &entry : get("colors").toList()) {
            rule.colors.append(colorFrom(entry).value_or(Rgb{}));
        }
    }
    if (has("iconSet")) rule.iconSet = get("iconSet").toString();
    if (has("reverse")) rule.reverse = get("reverse").toBool();
    if (has("showValue")) rule.showValue = get("showValue").toBool();
    if (has("barColor")) rule.barColor = colorFrom(get("barColor")).value_or(rule.barColor);
    if (has("barGradient")) rule.bar.gradient = get("barGradient").toBool();
    if (has("barBorder")) rule.bar.border = colorFrom(get("barBorder"));
    if (has("barNegative")) rule.bar.negativeColor = colorFrom(get("barNegative")).value_or(rule.bar.negativeColor);
    if (has("barAxis")) rule.bar.axis = DataBarStyle::Axis(qBound(0, get("barAxis").toInt(), 2));
    if (has("barDirection")) rule.bar.direction = DataBarStyle::Direction(qBound(0, get("barDirection").toInt(), 2));
    if (has("background")) rule.format.background = colorFrom(get("background"));
    if (has("foreground")) rule.format.foreground = colorFrom(get("foreground"));
    const auto flag = [&](const char *key, std::optional<bool> &into) {
        if (has(key)) {
            into = get(key).toBool() ? std::optional<bool>(true) : std::nullopt;
        }
    };
    flag("bold", rule.format.bold);
    flag("italic", rule.format.italic);
    flag("underline", rule.format.underline);
    flag("strikeout", rule.format.strikeout);
    if (has("stopIfTrue")) rule.stopIfTrue = get("stopIfTrue").toBool();
    return rule;
}

QString condRuleDescription(const CondFormatRule &rule)
{
    const QString a = operand(rule.formula, rule.first);
    const QString b = operand(rule.formula2, rule.second);
    switch (rule.op) {
    case Op::GreaterThan: return tr("Cell Value > %1").arg(a);
    case Op::LessThan: return tr("Cell Value < %1").arg(a);
    case Op::GreaterThanOrEqual: return tr("Cell Value >= %1").arg(a);
    case Op::LessThanOrEqual: return tr("Cell Value <= %1").arg(a);
    case Op::EqualTo: return tr("Cell Value = %1").arg(rule.formula.isEmpty() && !rule.text.isEmpty() ? rule.text : a);
    case Op::NotEqualTo: return tr("Cell Value <> %1").arg(rule.formula.isEmpty() && !rule.text.isEmpty() ? rule.text : a);
    case Op::Between: return tr("Cell Value between %1 and %2").arg(a, b);
    case Op::NotBetween: return tr("Cell Value not between %1 and %2").arg(a, b);
    case Op::ContainsText: return tr("Cell Value contains \"%1\"").arg(rule.text);
    case Op::NotContainsText: return tr("Cell Value does not contain \"%1\"").arg(rule.text);
    case Op::BeginsWith: return tr("Cell Value begins with \"%1\"").arg(rule.text);
    case Op::EndsWith: return tr("Cell Value ends with \"%1\"").arg(rule.text);
    case Op::IsEmpty: return tr("Blanks");
    case Op::NotEmpty: return tr("No Blanks");
    case Op::ContainsErrors: return tr("Errors");
    case Op::NotContainsErrors: return tr("No Errors");
    case Op::Expression: return tr("Formula: =%1").arg(rule.formula);
    case Op::DataBar: return tr("Data Bar");
    case Op::ColorScale: return rule.thresholds.size() >= 3 ? tr("Graded Color Scale (3 colors)") : tr("Graded Color Scale");
    case Op::IconSet: return tr("Icon Set (%1)").arg(rule.iconSet);
    case Op::Top10:
        return (rule.bottom ? tr("Bottom %1") : tr("Top %1")).arg(rule.rank) + (rule.percent ? QStringLiteral("%") : QString());
    case Op::AboveAverage:
        if (rule.stdDev > 0) {
            return (rule.aboveAverage ? tr("%1 std dev above Average") : tr("%1 std dev below Average")).arg(rule.stdDev);
        }
        return rule.aboveAverage ? (rule.equalAverage ? tr("Equal to or above Average") : tr("Above Average"))
                                 : (rule.equalAverage ? tr("Equal to or below Average") : tr("Below Average"));
    case Op::Duplicate: return tr("Duplicate Values");
    case Op::Unique: return tr("Unique Values");
    case Op::TimePeriod: return tr("Dates occurring: %1").arg(tr(periodName(rule.period)));
    }
    return {};
}

CondFormatRule defaultDataBar(const CellRange &range)
{
    CondFormatRule rule;
    rule.range = range;
    rule.op = Op::DataBar;
    rule.barColor = rgb(0x63, 0x8e, 0xc6);
    rule.thresholds = {{CfThreshold::Type::AutoMin, {}, true}, {CfThreshold::Type::AutoMax, {}, true}};
    rule.bar.x14 = true; // Excel 2010's bar: from the axis, full length
    rule.bar.minLength = 0;
    rule.bar.maxLength = 100;
    rule.bar.border = rgb(0x63, 0x8e, 0xc6);
    return rule;
}

CondFormatRule defaultColorScale(const CellRange &range)
{
    CondFormatRule rule;
    rule.range = range;
    rule.op = Op::ColorScale;
    rule.thresholds = {{CfThreshold::Type::Min, {}, true},
                       {CfThreshold::Type::Percentile, QStringLiteral("50"), true},
                       {CfThreshold::Type::Max, {}, true}};
    rule.colors = {rgb(0xf8, 0x69, 0x6b), rgb(0xff, 0xeb, 0x84), rgb(0x63, 0xbe, 0x7b)};
    return rule;
}

CondFormatRule defaultIconSet(const CellRange &range)
{
    CondFormatRule rule;
    rule.range = range;
    rule.op = Op::IconSet;
    rule.iconSet = QStringLiteral("3TrafficLights1");
    rule.thresholds = {{CfThreshold::Type::Percent, QStringLiteral("0"), true},
                       {CfThreshold::Type::Percent, QStringLiteral("33"), true},
                       {CfThreshold::Type::Percent, QStringLiteral("67"), true}};
    return rule;
}

QList<CondFormatRule> rulesWithout(const QList<CondFormatRule> &rules, const CellRange &cleared)
{
    QList<CondFormatRule> out;
    for (CondFormatRule rule : rules) {
        QList<CellRange> left;
        for (const CellRange &range : rule.ranges()) {
            left += subtract(range, cleared);
        }
        if (left.isEmpty()) {
            continue;
        }
        const CellAddr from = rule.range.topLeft;
        rule.range = left.takeFirst();
        rule.moreRanges = left;
        if (rule.range.topLeft != from) {
            rule.formula = reanchored(rule.formula, from, rule.range.topLeft);
            rule.formula2 = reanchored(rule.formula2, from, rule.range.topLeft);
            static_cast<void>(prepareCondRule(rule));
        }
        out.append(rule);
    }
    return out;
}

} // namespace QindaOffice::Apps::QindaCalc
