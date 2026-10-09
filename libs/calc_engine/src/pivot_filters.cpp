// SPDX-License-Identifier: GPL-3.0-or-later
// A PivotTable field's label and value filters (pivot_compute.h; M74,
// D-083): Excel's comparisons — equals, begins with, contains, between …
// on the item's caption with the * ? ~ wildcards, numbers compared as
// numbers when both sides are; value filters on a data field's total.

#include "pivot_compute.h"

#include <QRegularExpression>

namespace QindaOffice::Calc {

namespace {

// Excel's label filter wildcards: * any run, ? one character, ~ escapes.
bool wildcardMatch(const QString &text, const QString &pattern)
{
    QString re;
    for (qsizetype i = 0; i < pattern.size(); ++i) {
        const QChar c = pattern.at(i);
        if (c == QLatin1Char('~') && i + 1 < pattern.size()) {
            re += QRegularExpression::escape(QString(pattern.at(++i)));
        } else if (c == QLatin1Char('*')) {
            re += QStringLiteral(".*");
        } else if (c == QLatin1Char('?')) {
            re += QLatin1Char('.');
        } else {
            re += QRegularExpression::escape(QString(c));
        }
    }
    const QRegularExpression expression(QRegularExpression::anchoredPattern(re),
                                        QRegularExpression::CaseInsensitiveOption
                                            | QRegularExpression::DotMatchesEverythingOption);
    return expression.match(text).hasMatch();
}

// <0, 0, >0 comparing an item with a typed operand: numbers as numbers
// when both are numbers, text without case otherwise.
int compareCaption(const Value &item, const QString &caption, const QString &operand)
{
    bool ok = false;
    const double number = operand.trimmed().toDouble(&ok);
    if (ok && isNumber(item)) {
        const double v = std::get<double>(item);
        return v < number ? -1 : (v > number ? 1 : 0);
    }
    return caption.compare(operand, Qt::CaseInsensitive);
}

} // namespace

bool pivotLabelAccepts(const PivotFilter &filter, const Value &item, const QString &caption)
{
    using Op = PivotFilter::Op;
    switch (filter.op) {
    case Op::Equal: return wildcardMatch(caption, filter.text1);
    case Op::NotEqual: return !wildcardMatch(caption, filter.text1);
    case Op::BeginsWith: return wildcardMatch(caption, filter.text1 + QLatin1Char('*'));
    case Op::NotBeginsWith: return !wildcardMatch(caption, filter.text1 + QLatin1Char('*'));
    case Op::EndsWith: return wildcardMatch(caption, QLatin1Char('*') + filter.text1);
    case Op::NotEndsWith: return !wildcardMatch(caption, QLatin1Char('*') + filter.text1);
    case Op::Contains: return wildcardMatch(caption, QLatin1Char('*') + filter.text1 + QLatin1Char('*'));
    case Op::NotContains: return !wildcardMatch(caption, QLatin1Char('*') + filter.text1 + QLatin1Char('*'));
    case Op::Greater: return compareCaption(item, caption, filter.text1) > 0;
    case Op::GreaterOrEqual: return compareCaption(item, caption, filter.text1) >= 0;
    case Op::Less: return compareCaption(item, caption, filter.text1) < 0;
    case Op::LessOrEqual: return compareCaption(item, caption, filter.text1) <= 0;
    case Op::Between:
        return compareCaption(item, caption, filter.text1) >= 0 && compareCaption(item, caption, filter.text2) <= 0;
    case Op::NotBetween:
        return compareCaption(item, caption, filter.text1) < 0 || compareCaption(item, caption, filter.text2) > 0;
    }
    return true;
}

bool pivotValueAccepts(const PivotFilter &filter, const Value &value)
{
    using Op = PivotFilter::Op;
    const auto *n = std::get_if<double>(&value);
    if (n == nullptr) {
        return false;
    }
    const double v = *n;
    const double a = filter.value1;
    const double b = filter.value2;
    switch (filter.op) {
    case Op::Equal: return v == a;
    case Op::NotEqual: return v != a;
    case Op::Greater: return v > a;
    case Op::GreaterOrEqual: return v >= a;
    case Op::Less: return v < a;
    case Op::LessOrEqual: return v <= a;
    case Op::Between: return v >= qMin(a, b) && v <= qMax(a, b);
    case Op::NotBetween: return v < qMin(a, b) || v > qMax(a, b);
    default: return true;
    }
}

} // namespace QindaOffice::Calc
