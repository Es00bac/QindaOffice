// SPDX-License-Identifier: GPL-3.0-or-later
// Excel's names for the rule kinds, sqref text and the small text helpers
// of the rule markup (xlsx_rules_internal.h, M78, D-087). The spellings are
// ECMA-376's (ST_ConditionalFormattingOperator, ST_TimePeriod, ST_CfvoType,
// ST_IconSetType, ST_DataValidationType, ST_DataValidationOperator) and
// Excel 2010's x14 additions (autoMin, autoMax, 3Stars, 3Triangles, 5Boxes,
// NoIcons).

#include "xlsx_rules_internal.h"

#include "xlsx_parts.h"

#include "sheet.h"

#include <QRegularExpression>

namespace QindaOffice::Calc::IO::Rules {

namespace {

using Op = CondFormatRule::Op;
using Period = CondFormatRule::Period;
using Type = CfThreshold::Type;
using Kind = ValidationRule::Kind;
using VOp = ValidationRule::Operator;

template<typename T>
struct Named {
    T value;
    const char *name;
};

const Named<Op> kCellIs[] = {
    {Op::LessThan, "lessThan"},       {Op::LessThanOrEqual, "lessThanOrEqual"},
    {Op::EqualTo, "equal"},           {Op::NotEqualTo, "notEqual"},
    {Op::GreaterThanOrEqual, "greaterThanOrEqual"}, {Op::GreaterThan, "greaterThan"},
    {Op::Between, "between"},         {Op::NotBetween, "notBetween"},
};

const Named<Period> kPeriods[] = {
    {Period::Today, "today"},         {Period::Yesterday, "yesterday"}, {Period::Tomorrow, "tomorrow"},
    {Period::Last7Days, "last7Days"}, {Period::ThisMonth, "thisMonth"}, {Period::LastMonth, "lastMonth"},
    {Period::NextMonth, "nextMonth"}, {Period::ThisWeek, "thisWeek"},   {Period::LastWeek, "lastWeek"},
    {Period::NextWeek, "nextWeek"},
};

const Named<Type> kCfvo[] = {
    {Type::Number, "num"},           {Type::Percent, "percent"}, {Type::Max, "max"},
    {Type::Min, "min"},              {Type::Formula, "formula"}, {Type::Percentile, "percentile"},
    {Type::AutoMin, "autoMin"},      {Type::AutoMax, "autoMax"},
};

const Named<Kind> kKinds[] = {
    {Kind::WholeNumber, "whole"}, {Kind::Decimal, "decimal"},       {Kind::List, "list"},
    {Kind::Date, "date"},         {Kind::Time, "time"},             {Kind::TextLength, "textLength"},
    {Kind::Custom, "custom"},     {Kind::Any, "none"},
};

const Named<VOp> kOperators[] = {
    {VOp::Between, "between"},     {VOp::NotBetween, "notBetween"},   {VOp::Equal, "equal"},
    {VOp::NotEqual, "notEqual"},   {VOp::LessThan, "lessThan"},       {VOp::LessThanOrEqual, "lessThanOrEqual"},
    {VOp::GreaterThan, "greaterThan"}, {VOp::GreaterThanOrEqual, "greaterThanOrEqual"},
};

template<typename T, std::size_t N>
std::optional<T> byName(const Named<T> (&table)[N], const QString &name)
{
    for (const Named<T> &entry : table) {
        if (name == QLatin1String(entry.name)) {
            return entry.value;
        }
    }
    return std::nullopt;
}

template<typename T, std::size_t N>
QString nameOf(const Named<T> (&table)[N], T value)
{
    for (const Named<T> &entry : table) {
        if (entry.value == value) {
            return QLatin1String(entry.name);
        }
    }
    return {};
}

// A whole-column ("A:C") or whole-row ("3:5") reference, which sqref may
// hold; nullopt for anything else.
std::optional<CellRange> wholeLines(const QString &text)
{
    static const QRegularExpression columns(QStringLiteral("^\\$?([A-Za-z]{1,3}):\\$?([A-Za-z]{1,3})$"));
    static const QRegularExpression rows(QStringLiteral("^\\$?(\\d+):\\$?(\\d+)$"));
    if (const QRegularExpressionMatch m = columns.match(text); m.hasMatch()) {
        const auto first = colFromName(m.captured(1).toUpper());
        const auto last = colFromName(m.captured(2).toUpper());
        if (first && last) {
            return CellRange{{0, *first}, {Sheet::kMaxRows - 1, *last}};
        }
    }
    if (const QRegularExpressionMatch m = rows.match(text); m.hasMatch()) {
        const int first = m.captured(1).toInt();
        const int last = m.captured(2).toInt();
        if (first >= 1 && last >= first) {
            return CellRange{{first - 1, 0}, {last - 1, Sheet::kMaxCols - 1}};
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<CondFormatRule::Op> cellIsOperator(const QString &name)
{
    return byName(kCellIs, name);
}

QString cellIsOperatorName(CondFormatRule::Op op)
{
    return nameOf(kCellIs, op);
}

std::optional<CondFormatRule::Period> timePeriodFromName(const QString &name)
{
    return byName(kPeriods, name);
}

QString timePeriodName(CondFormatRule::Period period)
{
    return nameOf(kPeriods, period);
}

std::optional<CfThreshold::Type> cfvoType(const QString &name)
{
    return byName(kCfvo, name);
}

QString cfvoTypeName(CfThreshold::Type type, bool x14)
{
    // Excel 2007's markup has no automatic minimum or maximum.
    if (!x14 && type == Type::AutoMin) {
        return QStringLiteral("min");
    }
    if (!x14 && type == Type::AutoMax) {
        return QStringLiteral("max");
    }
    return nameOf(kCfvo, type);
}

bool isKnownIconSet(const QString &name)
{
    static const QStringList sets{
        QStringLiteral("3Arrows"),         QStringLiteral("3ArrowsGray"),     QStringLiteral("3Flags"),
        QStringLiteral("3TrafficLights1"), QStringLiteral("3TrafficLights2"), QStringLiteral("3Signs"),
        QStringLiteral("3Symbols"),        QStringLiteral("3Symbols2"),       QStringLiteral("4Arrows"),
        QStringLiteral("4ArrowsGray"),     QStringLiteral("4RedToBlack"),     QStringLiteral("4Rating"),
        QStringLiteral("4TrafficLights"),  QStringLiteral("5Arrows"),         QStringLiteral("5ArrowsGray"),
        QStringLiteral("5Rating"),         QStringLiteral("5Quarters"),       QStringLiteral("3Stars"),
        QStringLiteral("3Triangles"),      QStringLiteral("5Boxes"),          QStringLiteral("NoIcons")};
    return sets.contains(name);
}

bool isX14OnlyIconSet(const QString &name)
{
    return name == QLatin1String("3Stars") || name == QLatin1String("3Triangles") || name == QLatin1String("5Boxes")
        || name == QLatin1String("NoIcons");
}

std::optional<ValidationRule::Kind> validationKind(const QString &type)
{
    return byName(kKinds, type.isEmpty() ? QStringLiteral("none") : type);
}

QString validationTypeName(ValidationRule::Kind kind)
{
    // QindaCalc's NotEmpty is Excel's custom formula LEN(TRIM(cell))>0.
    return kind == Kind::NotEmpty ? QStringLiteral("custom") : nameOf(kKinds, kind);
}

std::optional<ValidationRule::Operator> validationOperator(const QString &name)
{
    return byName(kOperators, name.isEmpty() ? QStringLiteral("between") : name);
}

QString validationOperatorName(ValidationRule::Operator op)
{
    return nameOf(kOperators, op);
}

QList<CellRange> parseSqref(const QString &text)
{
    QList<CellRange> out;
    for (const QString &part : text.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (const std::optional<CellRange> range = rangeFromText(part)) {
            out.append(*range);
        } else if (const std::optional<CellRange> lines = wholeLines(part)) {
            out.append(*lines);
        } else {
            return {}; // one part we cannot read: the whole sqref is unread
        }
    }
    return out;
}

QString sqrefText(const QList<CellRange> &ranges)
{
    QStringList parts;
    for (const CellRange &range : ranges) {
        parts.append(range.topLeft == range.bottomRight ? addrText(range.topLeft)
                                                        : addrText(range.topLeft) + QLatin1Char(':')
                                                              + addrText(range.bottomRight));
    }
    return parts.join(QLatin1Char(' '));
}

QString xmlEscaped(const QString &text)
{
    QString out = text;
    out.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    out.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    out.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    return out;
}

QString attrEscaped(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        switch (c.unicode()) {
        case u'&': out += QStringLiteral("&amp;"); break;
        case u'<': out += QStringLiteral("&lt;"); break;
        case u'>': out += QStringLiteral("&gt;"); break;
        case u'"': out += QStringLiteral("&quot;"); break;
        case u'\n': out += QStringLiteral("&#10;"); break;
        case u'\r': out += QStringLiteral("&#13;"); break;
        case u'\t': out += QStringLiteral("&#9;"); break;
        default:
            if (c.unicode() >= 0x20 && c.unicode() != 0xFFFE && c.unicode() != 0xFFFF) {
                out += c;
            }
        }
    }
    return out;
}

QString rgbText(const Rgb &color)
{
    return QStringLiteral("FF%1%2%3")
        .arg(int(color.r), 2, 16, QLatin1Char('0'))
        .arg(int(color.g), 2, 16, QLatin1Char('0'))
        .arg(int(color.b), 2, 16, QLatin1Char('0'))
        .toUpper();
}

QString numberText(double value)
{
    return QString::number(value, 'g', 15);
}

bool boolAttr(const QString &value, bool fallback)
{
    if (value.isEmpty()) {
        return fallback;
    }
    return value == QLatin1String("1") || value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
}

QString firstStartTag(const QString &xml)
{
    char16_t quote = 0;
    for (qsizetype i = 1; i < xml.size(); ++i) {
        const char16_t c = xml.at(i).unicode();
        if (quote != 0) {
            quote = c == quote ? 0 : quote;
        } else if (c == u'"' || c == u'\'') {
            quote = c;
        } else if (c == u'>') {
            return xml.left(i + 1);
        }
    }
    return {};
}

QString withAttribute(const QString &xml, const QString &qname, const QString &value)
{
    QString tag = firstStartTag(xml);
    if (tag.isEmpty()) {
        return xml;
    }
    const qsizetype end = tag.size() - 1;
    const QRegularExpression existing(QStringLiteral("(\\s%1\\s*=\\s*)(\"[^\"]*\"|'[^']*')")
                                          .arg(QRegularExpression::escape(qname)));
    const QRegularExpressionMatch match = existing.match(tag);
    const QString quoted = QLatin1Char('"') + attrEscaped(value) + QLatin1Char('"');
    if (match.hasMatch()) {
        tag.replace(match.capturedStart(2), match.capturedLength(2), quoted);
    } else {
        const qsizetype at = tag.endsWith(QLatin1String("/>")) ? tag.size() - 2 : tag.size() - 1;
        tag.insert(at, QLatin1Char(' ') + qname + QLatin1Char('=') + quoted);
    }
    return tag + xml.mid(end + 1);
}

} // namespace QindaOffice::Calc::IO::Rules
