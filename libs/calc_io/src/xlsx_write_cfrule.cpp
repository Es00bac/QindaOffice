// SPDX-License-Identifier: GPL-3.0-or-later
// One conditional-format rule in Excel's markup (xlsx_rules_internal.h,
// M78, D-087): the 2007 <cfRule> of every kind — with the formulas Excel
// itself writes for the text, blank, error and date-occurring kinds, so a
// consumer that only evaluates formulas agrees —, and Excel 2010's x14 rule
// for a data bar's settings and for the icon sets Excel 2007 lacks.

#include "xlsx_formula.h"
#include "xlsx_rules_internal.h"

#include <cmath>

namespace QindaOffice::Calc::IO::Rules {

namespace {

using Op = CondFormatRule::Op;
using Period = CondFormatRule::Period;
using Type = CfThreshold::Type;

QString quoted(const QString &text)
{
    QString escaped = text;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

QString formulaElement(const QString &formula)
{
    return QStringLiteral("<formula>") + xmlEscaped(formula) + QStringLiteral("</formula>");
}

bool valued(Type type)
{
    return type == Type::Number || type == Type::Percent || type == Type::Percentile || type == Type::Formula;
}

// A cfvo value in the file's spelling.
QString fileValue(const CfThreshold &threshold)
{
    bool number = false;
    static_cast<void>(threshold.value.toDouble(&number));
    return number ? threshold.value.trimmed() : formulaForFile(threshold.value);
}

QString cfvo2007(const CfThreshold &threshold)
{
    QString out = QStringLiteral("<cfvo type=\"%1\"").arg(cfvoTypeName(threshold.type, false));
    if (valued(threshold.type)) {
        out += QStringLiteral(" val=\"%1\"").arg(attrEscaped(fileValue(threshold)));
    }
    if (!threshold.gte) {
        out += QStringLiteral(" gte=\"0\"");
    }
    return out + QStringLiteral("/>");
}

QString cfvoX14(const CfThreshold &threshold)
{
    QString out = QStringLiteral("<x14:cfvo type=\"%1\"").arg(cfvoTypeName(threshold.type, true));
    if (!threshold.gte) {
        out += QStringLiteral(" gte=\"0\"");
    }
    if (!valued(threshold.type)) {
        return out + QStringLiteral("/>");
    }
    return out + QStringLiteral("><xm:f>") + xmlEscaped(fileValue(threshold)) + QStringLiteral("</xm:f></x14:cfvo>");
}

QString colorElement(const QString &name, const Rgb &color)
{
    return QStringLiteral("<%1 rgb=\"%2\"/>").arg(name, rgbText(color));
}

QString periodFormula(Period period, const QString &a)
{
    switch (period) {
    case Period::Today:
        return QStringLiteral("FLOOR(%1,1)=TODAY()").arg(a);
    case Period::Yesterday:
        return QStringLiteral("FLOOR(%1,1)=TODAY()-1").arg(a);
    case Period::Tomorrow:
        return QStringLiteral("FLOOR(%1,1)=TODAY()+1").arg(a);
    case Period::Last7Days:
        return QStringLiteral("AND(TODAY()-FLOOR(%1,1)<=6,FLOOR(%1,1)<=TODAY())").arg(a);
    case Period::ThisWeek:
        return QStringLiteral("AND(TODAY()-ROUNDDOWN(%1,0)<=WEEKDAY(TODAY())-1,ROUNDDOWN(%1,0)-TODAY()<=7-WEEKDAY(TODAY()))")
            .arg(a);
    case Period::LastWeek:
        return QStringLiteral("AND(TODAY()-ROUNDDOWN(%1,0)>=(WEEKDAY(TODAY())),TODAY()-ROUNDDOWN(%1,0)<(WEEKDAY(TODAY())+7))")
            .arg(a);
    case Period::NextWeek:
        return QStringLiteral("AND(ROUNDDOWN(%1,0)-TODAY()>(7-WEEKDAY(TODAY())),ROUNDDOWN(%1,0)-TODAY()<(15-WEEKDAY(TODAY())))")
            .arg(a);
    case Period::ThisMonth:
        return QStringLiteral("AND(MONTH(%1)=MONTH(TODAY()),YEAR(%1)=YEAR(TODAY()))").arg(a);
    case Period::LastMonth:
        return QStringLiteral("AND(MONTH(%1)=MONTH(EDATE(TODAY(),0-1)),YEAR(%1)=YEAR(EDATE(TODAY(),0-1)))").arg(a);
    case Period::NextMonth:
        return QStringLiteral("AND(MONTH(%1)=MONTH(EDATE(TODAY(),0+1)),YEAR(%1)=YEAR(EDATE(TODAY(),0+1)))").arg(a);
    }
    return {};
}

// A cellIs operand in the file's spelling.
QString operandText(const CondFormatRule &rule, bool second)
{
    const QString &formula = second ? rule.formula2 : rule.formula;
    if (!formula.isEmpty()) {
        return formulaForFile(formula);
    }
    if (!second && !rule.text.isEmpty() && (rule.op == Op::EqualTo || rule.op == Op::NotEqualTo)) {
        return quoted(rule.text);
    }
    return numberText(second ? rule.second : rule.first);
}

QString iconSetAttributes(const CondFormatRule &rule)
{
    QString out;
    if (rule.iconSet != QLatin1String("3TrafficLights1")) {
        out += QStringLiteral(" iconSet=\"%1\"").arg(rule.iconSet);
    }
    if (!rule.showValue) {
        out += QStringLiteral(" showValue=\"0\"");
    }
    if (rule.reverse) {
        out += QStringLiteral(" reverse=\"1\"");
    }
    return out;
}

// The thresholds a scale, bar or icon set is written with (its own, or
// Excel's defaults).
QList<CfThreshold> thresholdsOf(const CondFormatRule &rule)
{
    if (!rule.thresholds.isEmpty()) {
        return rule.thresholds;
    }
    if (rule.op == Op::IconSet) {
        QList<CfThreshold> bands;
        const int count = qMax(1, iconSetSize(rule.iconSet));
        for (int i = 0; i < count; ++i) {
            bands.append(CfThreshold{Type::Percent, QString::number(std::lround(100.0 * i / count)), true});
        }
        return bands;
    }
    return {CfThreshold{Type::Min, {}, true}, CfThreshold{Type::Max, {}, true}};
}

} // namespace

bool isX14Only(const CondFormatRule &rule)
{
    return rule.op == Op::IconSet && isX14OnlyIconSet(rule.iconSet);
}

bool needsX14Twin(const CondFormatRule &rule)
{
    if (rule.op != Op::DataBar) {
        return false;
    }
    const DataBarStyle &bar = rule.bar;
    for (const CfThreshold &threshold : rule.thresholds) {
        if (threshold.type == Type::AutoMin || threshold.type == Type::AutoMax) {
            return true;
        }
    }
    return bar.x14 || !bar.gradient || bar.border.has_value() || bar.axis != DataBarStyle::Axis::Automatic
        || bar.direction != DataBarStyle::Direction::Context || bar.negativeSameAsPositive
        || bar.negativeBorder.has_value() || bar.negativeColor != Rgb{255, 0, 0} || bar.axisColor != Rgb{0, 0, 0};
}

QByteArray cfRuleXml(const CondFormatRule &rule, int priority, int dxfId, const QString &x14Id)
{
    const QString a = addrText(rule.range.topLeft); // the rule's anchor, relative
    QString type;
    QString attributes;
    QString body;
    switch (rule.op) {
    case Op::GreaterThan:
    case Op::LessThan:
    case Op::Between:
    case Op::NotBetween:
    case Op::EqualTo:
    case Op::NotEqualTo:
    case Op::GreaterThanOrEqual:
    case Op::LessThanOrEqual:
        type = QStringLiteral("cellIs");
        attributes = QStringLiteral(" operator=\"%1\"").arg(cellIsOperatorName(rule.op));
        body = formulaElement(operandText(rule, false));
        if (rule.op == Op::Between || rule.op == Op::NotBetween) {
            body += formulaElement(operandText(rule, true));
        }
        break;
    case Op::Expression:
        type = QStringLiteral("expression");
        body = formulaElement(formulaForFile(rule.formula));
        break;
    case Op::ContainsText:
        type = QStringLiteral("containsText");
        attributes = QStringLiteral(" operator=\"containsText\" text=\"%1\"").arg(attrEscaped(rule.text));
        body = formulaElement(QStringLiteral("NOT(ISERROR(SEARCH(%1,%2)))").arg(quoted(rule.text), a));
        break;
    case Op::NotContainsText:
        type = QStringLiteral("notContainsText");
        attributes = QStringLiteral(" operator=\"notContains\" text=\"%1\"").arg(attrEscaped(rule.text));
        body = formulaElement(QStringLiteral("ISERROR(SEARCH(%1,%2))").arg(quoted(rule.text), a));
        break;
    case Op::BeginsWith:
        type = QStringLiteral("beginsWith");
        attributes = QStringLiteral(" operator=\"beginsWith\" text=\"%1\"").arg(attrEscaped(rule.text));
        body = formulaElement(QStringLiteral("LEFT(%2,LEN(%1))=%1").arg(quoted(rule.text), a));
        break;
    case Op::EndsWith:
        type = QStringLiteral("endsWith");
        attributes = QStringLiteral(" operator=\"endsWith\" text=\"%1\"").arg(attrEscaped(rule.text));
        body = formulaElement(QStringLiteral("RIGHT(%2,LEN(%1))=%1").arg(quoted(rule.text), a));
        break;
    case Op::IsEmpty:
        type = QStringLiteral("containsBlanks");
        body = formulaElement(QStringLiteral("LEN(TRIM(%1))=0").arg(a));
        break;
    case Op::NotEmpty:
        type = QStringLiteral("notContainsBlanks");
        body = formulaElement(QStringLiteral("LEN(TRIM(%1))>0").arg(a));
        break;
    case Op::ContainsErrors:
        type = QStringLiteral("containsErrors");
        body = formulaElement(QStringLiteral("ISERROR(%1)").arg(a));
        break;
    case Op::NotContainsErrors:
        type = QStringLiteral("notContainsErrors");
        body = formulaElement(QStringLiteral("NOT(ISERROR(%1))").arg(a));
        break;
    case Op::Top10:
        type = QStringLiteral("top10");
        attributes = (rule.percent ? QStringLiteral(" percent=\"1\"") : QString())
            + (rule.bottom ? QStringLiteral(" bottom=\"1\"") : QString()) + QStringLiteral(" rank=\"%1\"").arg(rule.rank);
        break;
    case Op::AboveAverage:
        type = QStringLiteral("aboveAverage");
        attributes = (rule.aboveAverage ? QString() : QStringLiteral(" aboveAverage=\"0\""))
            + (rule.equalAverage ? QStringLiteral(" equalAverage=\"1\"") : QString())
            + (rule.stdDev > 0 ? QStringLiteral(" stdDev=\"%1\"").arg(rule.stdDev) : QString());
        break;
    case Op::Duplicate:
        type = QStringLiteral("duplicateValues");
        break;
    case Op::Unique:
        type = QStringLiteral("uniqueValues");
        break;
    case Op::TimePeriod:
        type = QStringLiteral("timePeriod");
        attributes = QStringLiteral(" timePeriod=\"%1\"").arg(timePeriodName(rule.period));
        body = formulaElement(periodFormula(rule.period, a));
        break;
    case Op::ColorScale: {
        type = QStringLiteral("colorScale");
        const QList<CfThreshold> thresholds = thresholdsOf(rule);
        body = QStringLiteral("<colorScale>");
        for (const CfThreshold &threshold : thresholds) {
            body += cfvo2007(threshold);
        }
        for (qsizetype i = 0; i < thresholds.size(); ++i) {
            body += colorElement(QStringLiteral("color"), rule.colors.value(i, rule.colors.isEmpty() ? Rgb{} : rule.colors.last()));
        }
        body += QStringLiteral("</colorScale>");
        break;
    }
    case Op::DataBar: {
        type = QStringLiteral("dataBar");
        const QList<CfThreshold> thresholds = thresholdsOf(rule);
        body = QStringLiteral("<dataBar");
        // With a twin, Excel keeps 2007's lengths here and the real ones there.
        if (x14Id.isEmpty() && rule.bar.minLength != 10) {
            body += QStringLiteral(" minLength=\"%1\"").arg(rule.bar.minLength);
        }
        if (x14Id.isEmpty() && rule.bar.maxLength != 90) {
            body += QStringLiteral(" maxLength=\"%1\"").arg(rule.bar.maxLength);
        }
        if (!rule.showValue) {
            body += QStringLiteral(" showValue=\"0\"");
        }
        body += QLatin1Char('>') + cfvo2007(thresholds.value(0)) + cfvo2007(thresholds.value(1))
            + colorElement(QStringLiteral("color"), rule.barColor) + QStringLiteral("</dataBar>");
        if (!x14Id.isEmpty()) {
            body += QStringLiteral("<extLst><ext uri=\"%1\" xmlns:x14=\"%2\"><x14:id>%3</x14:id></ext></extLst>")
                        .arg(QString::fromUtf16(kRuleIdExtUri), QString::fromUtf16(kX14Ns), x14Id);
        }
        break;
    }
    case Op::IconSet: {
        type = QStringLiteral("iconSet");
        body = QStringLiteral("<iconSet") + iconSetAttributes(rule) + QLatin1Char('>');
        for (const CfThreshold &threshold : thresholdsOf(rule)) {
            body += cfvo2007(threshold);
        }
        body += QStringLiteral("</iconSet>");
        break;
    }
    }
    QString out = QStringLiteral("<cfRule type=\"%1\"").arg(type);
    if (dxfId >= 0) {
        out += QStringLiteral(" dxfId=\"%1\"").arg(dxfId);
    }
    out += QStringLiteral(" priority=\"%1\"").arg(priority);
    if (rule.stopIfTrue) {
        out += QStringLiteral(" stopIfTrue=\"1\"");
    }
    out += attributes;
    out += body.isEmpty() ? QStringLiteral("/>") : QLatin1Char('>') + body + QStringLiteral("</cfRule>");
    return out.toUtf8();
}

QByteArray x14RuleXml(const CondFormatRule &rule, int priority, const QString &id)
{
    const QList<CfThreshold> thresholds = thresholdsOf(rule);
    if (rule.op == Op::IconSet) {
        QString out = QStringLiteral("<x14:cfRule type=\"iconSet\" priority=\"%1\"").arg(priority);
        if (rule.stopIfTrue) {
            out += QStringLiteral(" stopIfTrue=\"1\"");
        }
        out += QStringLiteral(" id=\"%1\"><x14:iconSet iconSet=\"%2\"").arg(id, rule.iconSet);
        if (!rule.showValue) {
            out += QStringLiteral(" showValue=\"0\"");
        }
        if (rule.reverse) {
            out += QStringLiteral(" reverse=\"1\"");
        }
        out += QLatin1Char('>');
        for (const CfThreshold &threshold : thresholds) {
            out += cfvoX14(threshold);
        }
        return (out + QStringLiteral("</x14:iconSet></x14:cfRule>")).toUtf8();
    }
    // A data bar's twin: no priority of its own (its 2007 rule has it).
    const DataBarStyle &bar = rule.bar;
    QString out = QStringLiteral("<x14:cfRule type=\"dataBar\" id=\"%1\"><x14:dataBar minLength=\"%2\" maxLength=\"%3\"")
                      .arg(id)
                      .arg(bar.minLength)
                      .arg(bar.maxLength);
    if (bar.border.has_value()) {
        out += QStringLiteral(" border=\"1\"");
    }
    if (!bar.gradient) {
        out += QStringLiteral(" gradient=\"0\"");
    }
    if (bar.direction != DataBarStyle::Direction::Context) {
        out += bar.direction == DataBarStyle::Direction::LeftToRight ? QStringLiteral(" direction=\"leftToRight\"")
                                                                     : QStringLiteral(" direction=\"rightToLeft\"");
    }
    if (bar.negativeSameAsPositive) {
        out += QStringLiteral(" negativeBarColorSameAsPositive=\"1\"");
    }
    if (bar.border.has_value() && bar.negativeBorder.has_value()) {
        out += QStringLiteral(" negativeBarBorderColorSameAsPositive=\"0\"");
    }
    if (bar.axis != DataBarStyle::Axis::Automatic) {
        out += bar.axis == DataBarStyle::Axis::Middle ? QStringLiteral(" axisPosition=\"middle\"")
                                                      : QStringLiteral(" axisPosition=\"none\"");
    }
    out += QLatin1Char('>') + cfvoX14(thresholds.value(0)) + cfvoX14(thresholds.value(1));
    if (bar.border.has_value()) {
        out += colorElement(QStringLiteral("x14:borderColor"), *bar.border);
    }
    out += colorElement(QStringLiteral("x14:negativeFillColor"), bar.negativeColor);
    if (bar.border.has_value() && bar.negativeBorder.has_value()) {
        out += colorElement(QStringLiteral("x14:negativeBorderColor"), *bar.negativeBorder);
    }
    out += colorElement(QStringLiteral("x14:axisColor"), bar.axisColor);
    return (out + QStringLiteral("</x14:dataBar></x14:cfRule>")).toUtf8();
}

} // namespace QindaOffice::Calc::IO::Rules
