// SPDX-License-Identifier: GPL-3.0-or-later
// One conditional-format rule into the model (xlsx_rules_internal.h, M78,
// D-087): a 2007 <cfRule> of any kind — with its Excel 2010 twin merged
// for a data bar — or an x14 rule without a 2007 partner. Anything the
// model would not hold exactly (an attribute it does not know, a custom
// icon, a formula the engine cannot parse, a dxf the package lacks) answers
// nullopt, and the caller keeps the rule as written.

#include "xlsx_formula.h"
#include "xlsx_parts.h"
#include "xlsx_rules_internal.h"

#include <QRegularExpression>

namespace QindaOffice::Calc::IO::Rules {

namespace {

using Op = CondFormatRule::Op;
using Type = CfThreshold::Type;

bool isRevision(const QXmlStreamAttribute &attribute)
{
    const QStringView uri = attribute.namespaceUri();
    return uri.startsWith(QLatin1String("http://schemas.microsoft.com/office/spreadsheetml/20"))
        && uri.contains(QLatin1String("/revision"));
}

// The node carries no attribute the model would drop.
bool onlyKnown(const RawNode &node, std::initializer_list<const char *> known)
{
    for (const QXmlStreamAttribute &attribute : node.attributes) {
        if (!attribute.namespaceUri().isEmpty()) {
            if (!isRevision(attribute)) {
                return false;
            }
            continue;
        }
        bool found = false;
        for (const char *name : known) {
            found = found || attribute.name() == QLatin1String(name);
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

// A cfvo: its type, the value (a number, or a formula in the engine's
// spelling), gte. 2007: val attribute; x14: an <xm:f> child.
std::optional<CfThreshold> threshold(const RawNode &node, bool x14)
{
    if (!onlyKnown(node, {"type", "val", "gte"})) {
        return std::nullopt;
    }
    const std::optional<Type> type = cfvoType(node.attr(u"type"));
    if (!type.has_value() || (!x14 && (*type == Type::AutoMin || *type == Type::AutoMax))) {
        return std::nullopt;
    }
    QString raw = node.attr(u"val");
    for (const RawNode &child : node.children) {
        if (!x14 || child.name != QLatin1String("f") || child.ns != QStringView(kXmNs)) {
            return std::nullopt; // an extLst we do not read
        }
        raw = child.text;
    }
    CfThreshold out;
    out.type = *type;
    out.gte = boolAttr(node.attr(u"gte"), true);
    const bool valued = *type == Type::Number || *type == Type::Percent || *type == Type::Percentile
        || *type == Type::Formula;
    if (!valued) {
        return out;
    }
    raw = raw.trimmed();
    if (raw.isEmpty()) {
        return std::nullopt;
    }
    bool number = false;
    static_cast<void>(raw.toDouble(&number));
    if (number) {
        out.value = raw;
        return out;
    }
    out.value = formulaFromFile(raw.startsWith(QLatin1Char('=')) ? raw.mid(1) : raw);
    const ParseResult parsed = parseFormula(out.value);
    if (!parsed.error.isEmpty() || !parsed.root) {
        return std::nullopt;
    }
    return out;
}

std::optional<Rgb> colour(const RawNode &node, const ThemePalette &palette)
{
    return colorFromAttributes(node.attributes, palette);
}

// "\"Yes\"" → Yes; nullopt for anything but one string literal.
std::optional<QString> stringLiteral(const QString &formula)
{
    static const QRegularExpression literal(QStringLiteral("^\"((?:[^\"]|\"\")*)\"$"));
    const QRegularExpressionMatch match = literal.match(formula.trimmed());
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    return match.captured(1).replace(QStringLiteral("\"\""), QStringLiteral("\""));
}

// A cellIs operand: a number, an equal rule's text, or a formula.
void setOperand(CondFormatRule *rule, int index, const QString &raw)
{
    const QString text = raw.trimmed();
    bool number = false;
    const double value = text.toDouble(&number);
    if (number) {
        (index == 0 ? rule->first : rule->second) = value;
        return;
    }
    if (index == 0 && (rule->op == Op::EqualTo || rule->op == Op::NotEqualTo)) {
        if (const std::optional<QString> literal = stringLiteral(text); literal.has_value() && !literal->isEmpty()) {
            rule->text = *literal;
            return;
        }
    }
    (index == 0 ? rule->formula : rule->formula2) = formulaFromFile(text);
}

// Excel 2010's data-bar settings (x14:dataBar) onto `rule`.
bool readX14DataBar(const RawNode &bar, const ThemePalette &palette, CondFormatRule *rule)
{
    if (!onlyKnown(bar, {"minLength", "maxLength", "border", "gradient", "direction", "negativeBarColorSameAsPositive",
                         "negativeBarBorderColorSameAsPositive", "axisPosition"})) {
        return false;
    }
    DataBarStyle &style = rule->bar;
    style.x14 = true;
    style.minLength = bar.has(u"minLength") ? bar.attr(u"minLength").toInt() : 10;
    style.maxLength = bar.has(u"maxLength") ? bar.attr(u"maxLength").toInt() : 90;
    style.gradient = boolAttr(bar.attr(u"gradient"), true);
    const bool border = boolAttr(bar.attr(u"border"), false);
    const QString direction = bar.attr(u"direction");
    if (direction == QLatin1String("leftToRight")) {
        style.direction = DataBarStyle::Direction::LeftToRight;
    } else if (direction == QLatin1String("rightToLeft")) {
        style.direction = DataBarStyle::Direction::RightToLeft;
    } else if (!direction.isEmpty() && direction != QLatin1String("context")) {
        return false;
    }
    const QString axis = bar.attr(u"axisPosition");
    if (axis == QLatin1String("middle")) {
        style.axis = DataBarStyle::Axis::Middle;
    } else if (axis == QLatin1String("none")) {
        style.axis = DataBarStyle::Axis::None;
    } else if (!axis.isEmpty() && axis != QLatin1String("automatic")) {
        return false;
    }
    style.negativeSameAsPositive = boolAttr(bar.attr(u"negativeBarColorSameAsPositive"), false);
    const bool negativeBorderSame = boolAttr(bar.attr(u"negativeBarBorderColorSameAsPositive"), true);
    QList<CfThreshold> thresholds;
    std::optional<Rgb> borderColor;
    std::optional<Rgb> negativeBorder;
    for (const RawNode &child : bar.children) {
        const std::optional<Rgb> color = child.name == QLatin1String("cfvo") ? std::nullopt : colour(child, palette);
        if (child.name == QLatin1String("cfvo")) {
            const std::optional<CfThreshold> t = threshold(child, true);
            if (!t.has_value()) {
                return false;
            }
            thresholds.append(*t);
        } else if (!color.has_value()) {
            return false;
        } else if (child.name == QLatin1String("fillColor")) {
            rule->barColor = *color;
        } else if (child.name == QLatin1String("borderColor")) {
            borderColor = color;
        } else if (child.name == QLatin1String("negativeFillColor")) {
            style.negativeColor = *color;
        } else if (child.name == QLatin1String("negativeBorderColor")) {
            negativeBorder = color;
        } else if (child.name == QLatin1String("axisColor")) {
            style.axisColor = *color;
        } else {
            return false;
        }
    }
    if (thresholds.size() != 2) {
        return false;
    }
    rule->thresholds = thresholds;
    style.border = border ? std::optional<Rgb>(borderColor.value_or(rule->barColor)) : std::nullopt;
    style.negativeBorder = border && !negativeBorderSame ? negativeBorder : std::nullopt;
    return true;
}

bool readIconSet(const RawNode &set, bool x14, CondFormatRule *rule)
{
    if (!onlyKnown(set, {"iconSet", "showValue", "percent", "reverse", "custom"}) || boolAttr(set.attr(u"custom"), false)
        || !boolAttr(set.attr(u"percent"), true)) {
        return false; // custom icons per threshold are not modelled
    }
    rule->iconSet = set.has(u"iconSet") ? set.attr(u"iconSet") : QStringLiteral("3TrafficLights1");
    if (!isKnownIconSet(rule->iconSet) || (!x14 && isX14OnlyIconSet(rule->iconSet))) {
        return false;
    }
    rule->showValue = boolAttr(set.attr(u"showValue"), true);
    rule->reverse = boolAttr(set.attr(u"reverse"), false);
    for (const RawNode &child : set.children) {
        const std::optional<CfThreshold> t = child.name == QLatin1String("cfvo") ? threshold(child, x14) : std::nullopt;
        if (!t.has_value()) {
            return false;
        }
        rule->thresholds.append(*t);
    }
    return rule->thresholds.size() == iconSetSize(rule->iconSet);
}

// A 2007 cfRule into the model; nullopt when it must be kept. `twin` gets
// the id of its Excel 2010 twin.
std::optional<CondFormatRule> modelCfRuleImpl(const RuleContext &r, const RawNode &node, const QList<CellRange> &ranges,
                                              QString *twin)
{
    if (!onlyKnown(node, {"type", "dxfId", "priority", "stopIfTrue", "aboveAverage", "percent", "bottom", "operator",
                          "text", "timePeriod", "rank", "stdDev", "equalAverage"})) {
        return std::nullopt;
    }
    CondFormatRule rule;
    rule.range = ranges.first();
    rule.moreRanges = ranges.mid(1);
    rule.priority = node.attr(u"priority").toInt();
    rule.stopIfTrue = boolAttr(node.attr(u"stopIfTrue"), false);
    if (node.has(u"dxfId")) {
        bool ok = false;
        const int id = node.attr(u"dxfId").toInt(&ok);
        if (!ok || id < 0 || id >= r.dxfs.size()) {
            return std::nullopt; // a dxf the package does not have: as written
        }
        rule.dxfId = id;
        rule.format = decodeDxf(r.dxfs.at(id), r.palette);
    }
    QStringList formulas;
    const RawNode *special = nullptr;
    for (const RawNode &child : node.children) {
        if (child.name == QLatin1String("formula")) {
            formulas.append(child.text);
        } else if (child.name == QLatin1String("extLst")) {
            for (const RawNode &ext : child.children) {
                const RawNode *id = ext.child(u"id");
                if (ext.attr(u"uri").compare(QStringView(kRuleIdExtUri), Qt::CaseInsensitive) != 0 || id == nullptr
                    || ext.children.size() != 1) {
                    return std::nullopt;
                }
                *twin = id->text.trimmed();
            }
        } else if (special == nullptr) {
            special = &child;
        } else {
            return std::nullopt;
        }
    }
    const QString type = node.attr(u"type");
    const auto needs = [&formulas](qsizetype count) { return formulas.size() >= count; };
    if (type == QLatin1String("cellIs")) {
        const std::optional<Op> op = cellIsOperator(node.attr(u"operator"));
        if (!op.has_value() || !needs(*op == Op::Between || *op == Op::NotBetween ? 2 : 1)) {
            return std::nullopt;
        }
        rule.op = *op;
        setOperand(&rule, 0, formulas.at(0));
        if (*op == Op::Between || *op == Op::NotBetween) {
            setOperand(&rule, 1, formulas.at(1));
        }
    } else if (type == QLatin1String("expression")) {
        if (!needs(1)) {
            return std::nullopt;
        }
        rule.op = Op::Expression;
        rule.formula = formulaFromFile(formulas.at(0));
    } else if (type == QLatin1String("containsText") || type == QLatin1String("notContainsText")
               || type == QLatin1String("beginsWith") || type == QLatin1String("endsWith")) {
        if (!node.has(u"text")) {
            return std::nullopt;
        }
        rule.op = type == QLatin1String("containsText")      ? Op::ContainsText
                  : type == QLatin1String("notContainsText") ? Op::NotContainsText
                  : type == QLatin1String("beginsWith")      ? Op::BeginsWith
                                                             : Op::EndsWith;
        rule.text = node.attr(u"text");
    } else if (type == QLatin1String("containsBlanks")) {
        rule.op = Op::IsEmpty;
    } else if (type == QLatin1String("notContainsBlanks")) {
        rule.op = Op::NotEmpty;
    } else if (type == QLatin1String("containsErrors")) {
        rule.op = Op::ContainsErrors;
    } else if (type == QLatin1String("notContainsErrors")) {
        rule.op = Op::NotContainsErrors;
    } else if (type == QLatin1String("top10")) {
        bool ok = false;
        rule.op = Op::Top10;
        rule.rank = node.attr(u"rank").toInt(&ok);
        if (!ok || rule.rank < 1) {
            return std::nullopt;
        }
        rule.percent = boolAttr(node.attr(u"percent"), false);
        rule.bottom = boolAttr(node.attr(u"bottom"), false);
    } else if (type == QLatin1String("aboveAverage")) {
        rule.op = Op::AboveAverage;
        rule.aboveAverage = boolAttr(node.attr(u"aboveAverage"), true);
        rule.equalAverage = boolAttr(node.attr(u"equalAverage"), false);
        rule.stdDev = node.attr(u"stdDev").toInt();
        if (rule.stdDev < 0 || rule.stdDev > 3) {
            return std::nullopt;
        }
    } else if (type == QLatin1String("duplicateValues") || type == QLatin1String("uniqueValues")) {
        rule.op = type == QLatin1String("duplicateValues") ? Op::Duplicate : Op::Unique;
    } else if (type == QLatin1String("timePeriod")) {
        const std::optional<CondFormatRule::Period> period = timePeriodFromName(node.attr(u"timePeriod"));
        if (!period.has_value()) {
            return std::nullopt;
        }
        rule.op = Op::TimePeriod;
        rule.period = *period;
    } else if (type == QLatin1String("colorScale") && special != nullptr && special->name == QLatin1String("colorScale")
               && onlyKnown(*special, {})) {
        rule.op = Op::ColorScale;
        for (const RawNode &child : special->children) {
            if (child.name == QLatin1String("cfvo")) {
                const std::optional<CfThreshold> t = threshold(child, false);
                if (!t.has_value()) {
                    return std::nullopt;
                }
                rule.thresholds.append(*t);
            } else if (child.name == QLatin1String("color")) {
                const std::optional<Rgb> color = colour(child, r.palette);
                if (!color.has_value()) {
                    return std::nullopt;
                }
                rule.colors.append(*color);
            } else {
                return std::nullopt;
            }
        }
        if (rule.thresholds.size() != rule.colors.size() || rule.colors.size() < 2 || rule.colors.size() > 3) {
            return std::nullopt;
        }
    } else if (type == QLatin1String("dataBar") && special != nullptr && special->name == QLatin1String("dataBar")
               && onlyKnown(*special, {"minLength", "maxLength", "showValue"})) {
        rule.op = Op::DataBar;
        rule.showValue = boolAttr(special->attr(u"showValue"), true);
        rule.bar.minLength = special->has(u"minLength") ? special->attr(u"minLength").toInt() : 10;
        rule.bar.maxLength = special->has(u"maxLength") ? special->attr(u"maxLength").toInt() : 90;
        for (const RawNode &child : special->children) {
            if (child.name == QLatin1String("cfvo")) {
                const std::optional<CfThreshold> t = threshold(child, false);
                if (!t.has_value()) {
                    return std::nullopt;
                }
                rule.thresholds.append(*t);
            } else if (child.name == QLatin1String("color")) {
                const std::optional<Rgb> color = colour(child, r.palette);
                if (!color.has_value()) {
                    return std::nullopt;
                }
                rule.barColor = *color;
            } else {
                return std::nullopt;
            }
        }
        if (rule.thresholds.size() != 2) {
            return std::nullopt;
        }
        if (!twin->isEmpty()) {
            const RawNode *partner = nullptr;
            const TwinLookup found = r.twin(*twin, &partner);
            if (found != TwinLookup::Missing) {
                const RawNode *bar = partner != nullptr ? partner->child(u"dataBar") : nullptr;
                if (found == TwinLookup::Kept || bar == nullptr || partner->children.size() != 1
                    || !onlyKnown(*partner, {"type", "id"}) || !readX14DataBar(*bar, r.palette, &rule)) {
                    return std::nullopt;
                }
                rule.x14Id = *twin;
            } else {
                twin->clear(); // a twin the file does not have: the bar stands alone
            }
        }
    } else if (type == QLatin1String("iconSet") && special != nullptr && special->name == QLatin1String("iconSet")) {
        rule.op = Op::IconSet;
        if (!readIconSet(*special, false, &rule)) {
            return std::nullopt;
        }
    } else {
        return std::nullopt; // a kind (or a shape of one) the model has no place for
    }
    if (rule.op != Op::DataBar && !twin->isEmpty()) {
        return std::nullopt; // an extension the model does not know for this kind
    }
    if (special != nullptr && rule.op != Op::ColorScale && rule.op != Op::DataBar && rule.op != Op::IconSet) {
        return std::nullopt;
    }
    if (!prepareCondRule(rule)) {
        return std::nullopt;
    }
    return rule;
}

// An Excel 2010 rule without a 2007 partner: x14 icon sets and bars.
std::optional<CondFormatRule> modelX14RuleImpl(const RuleContext &r, const RawNode &node, const QList<CellRange> &ranges)
{
    if (!onlyKnown(node, {"type", "priority", "id", "stopIfTrue"}) || node.children.size() != 1) {
        return std::nullopt;
    }
    CondFormatRule rule;
    rule.range = ranges.first();
    rule.moreRanges = ranges.mid(1);
    rule.priority = node.attr(u"priority").toInt();
    rule.stopIfTrue = boolAttr(node.attr(u"stopIfTrue"), false);
    rule.x14Id = node.attr(u"id");
    const QString type = node.attr(u"type");
    const RawNode &body = node.children.first();
    if (type == QLatin1String("iconSet") && body.name == QLatin1String("iconSet")) {
        rule.op = Op::IconSet;
        if (!readIconSet(body, true, &rule)) {
            return std::nullopt;
        }
    } else if (type == QLatin1String("dataBar") && body.name == QLatin1String("dataBar")) {
        rule.op = Op::DataBar;
        if (!readX14DataBar(body, r.palette, &rule)) {
            return std::nullopt;
        }
    } else {
        return std::nullopt; // x14 formula rules (other sheets) and the rest stay as written
    }
    return rule;
}

} // namespace

bool onlyKnownAttributes(const RawNode &node, std::initializer_list<const char *> known)
{
    return onlyKnown(node, known);
}

std::optional<CondFormatRule> modelCfRule(const RuleContext &context, const RawNode &node, const QList<CellRange> &ranges,
                                          QString *twin)
{
    return modelCfRuleImpl(context, node, ranges, twin);
}

std::optional<CondFormatRule> modelX14CfRule(const RuleContext &context, const RawNode &node,
                                             const QList<CellRange> &ranges)
{
    return modelX14RuleImpl(context, node, ranges);
}

} // namespace QindaOffice::Calc::IO::Rules
