// SPDX-License-Identifier: GPL-3.0-or-later
// Excel's PivotTable spellings (xlsx_pivot_names.h; M74, D-083).

#include "xlsx_pivot_names.h"
#include "pivot_dates.h"

#include "functions/functions.h"

#include <QDateTime>

#include <array>
#include <cmath>

namespace QindaOffice::Calc::IO::PivotNames {

namespace {

struct FunctionNames {
    PivotFunction function;
    const char *subtotal;  // dataField subtotal
    const char *item;      // <item t>
    const char *attribute; // pivotField …Subtotal
};

// Excel's own order of custom subtotals (and of the pivotField attributes).
constexpr std::array<FunctionNames, 11> kFunctions{{
    {PivotFunction::Sum, "sum", "sum", "sumSubtotal"},
    {PivotFunction::Count, "count", "countA", "countASubtotal"},
    {PivotFunction::Average, "average", "avg", "avgSubtotal"},
    {PivotFunction::Max, "max", "max", "maxSubtotal"},
    {PivotFunction::Min, "min", "min", "minSubtotal"},
    {PivotFunction::Product, "product", "product", "productSubtotal"},
    {PivotFunction::CountNums, "countNums", "count", "countSubtotal"},
    {PivotFunction::StdDev, "stdDev", "stdDev", "stdDevSubtotal"},
    {PivotFunction::StdDevP, "stdDevp", "stdDevP", "stdDevPSubtotal"},
    {PivotFunction::Var, "var", "var", "varSubtotal"},
    {PivotFunction::VarP, "varp", "varP", "varPSubtotal"},
}};

const FunctionNames &namesOf(PivotFunction function)
{
    for (const FunctionNames &names : kFunctions) {
        if (names.function == function) {
            return names;
        }
    }
    return kFunctions.front();
}

struct ShowAsNames {
    PivotShowAs as;
    const char *ecma; // "" when only x14 spells it
    const char *x14;
};

constexpr std::array<ShowAsNames, 15> kShowAs{{
    {PivotShowAs::Normal, "normal", ""},
    {PivotShowAs::Difference, "difference", ""},
    {PivotShowAs::PercentOf, "percent", ""},
    {PivotShowAs::PercentDifference, "percentDiff", ""},
    {PivotShowAs::RunningTotal, "runTotal", ""},
    {PivotShowAs::PercentOfRow, "percentOfRow", ""},
    {PivotShowAs::PercentOfColumn, "percentOfCol", ""},
    {PivotShowAs::PercentOfTotal, "percentOfTotal", ""},
    {PivotShowAs::Index, "index", ""},
    {PivotShowAs::PercentOfParentRow, "", "percentOfParentRow"},
    {PivotShowAs::PercentOfParentColumn, "", "percentOfParentCol"},
    {PivotShowAs::PercentOfParent, "", "percentOfParent"},
    {PivotShowAs::PercentRunningTotal, "", "percentOfRunningTotal"},
    {PivotShowAs::RankAscending, "", "rankAscending"},
    {PivotShowAs::RankDescending, "", "rankDescending"},
}};

struct FilterNames {
    PivotFilter::Kind kind;
    PivotFilter::Op op;
    PivotFilter::TopBy topBy;
    const char *type;
};

using K = PivotFilter::Kind;
using O = PivotFilter::Op;
using T = PivotFilter::TopBy;

constexpr std::array<FilterNames, 25> kFilters{{
    {K::Label, O::Equal, T::Items, "captionEqual"},
    {K::Label, O::NotEqual, T::Items, "captionNotEqual"},
    {K::Label, O::BeginsWith, T::Items, "captionBeginsWith"},
    {K::Label, O::NotBeginsWith, T::Items, "captionNotBeginsWith"},
    {K::Label, O::EndsWith, T::Items, "captionEndsWith"},
    {K::Label, O::NotEndsWith, T::Items, "captionNotEndsWith"},
    {K::Label, O::Contains, T::Items, "captionContains"},
    {K::Label, O::NotContains, T::Items, "captionNotContains"},
    {K::Label, O::Greater, T::Items, "captionGreaterThan"},
    {K::Label, O::GreaterOrEqual, T::Items, "captionGreaterThanOrEqual"},
    {K::Label, O::Less, T::Items, "captionLessThan"},
    {K::Label, O::LessOrEqual, T::Items, "captionLessThanOrEqual"},
    {K::Label, O::Between, T::Items, "captionBetween"},
    {K::Label, O::NotBetween, T::Items, "captionNotBetween"},
    {K::Value, O::Equal, T::Items, "valueEqual"},
    {K::Value, O::NotEqual, T::Items, "valueNotEqual"},
    {K::Value, O::Greater, T::Items, "valueGreaterThan"},
    {K::Value, O::GreaterOrEqual, T::Items, "valueGreaterThanOrEqual"},
    {K::Value, O::Less, T::Items, "valueLessThan"},
    {K::Value, O::LessOrEqual, T::Items, "valueLessThanOrEqual"},
    {K::Value, O::Between, T::Items, "valueBetween"},
    {K::Value, O::NotBetween, T::Items, "valueNotBetween"},
    {K::Top, O::Equal, T::Items, "count"},
    {K::Top, O::Equal, T::Percent, "percent"},
    {K::Top, O::Equal, T::Sum, "sum"},
}};

} // namespace

QString dataSubtotal(PivotFunction function)
{
    return QString::fromLatin1(namesOf(function).subtotal);
}

std::optional<PivotFunction> dataSubtotalFrom(const QString &name)
{
    for (const FunctionNames &names : kFunctions) {
        if (name == QLatin1String(names.subtotal)) {
            return names.function;
        }
    }
    return std::nullopt;
}

QString itemType(PivotFunction function)
{
    return QString::fromLatin1(namesOf(function).item);
}

QString subtotalAttribute(PivotFunction function)
{
    return QString::fromLatin1(namesOf(function).attribute);
}

QList<PivotFunction> excelSubtotalOrder()
{
    QList<PivotFunction> order;
    for (const FunctionNames &names : kFunctions) {
        order.append(names.function);
    }
    return order;
}

QString showDataAs(PivotShowAs as)
{
    for (const ShowAsNames &names : kShowAs) {
        if (names.as == as) {
            return QString::fromLatin1(names.ecma);
        }
    }
    return {};
}

QString pivotShowAs(PivotShowAs as)
{
    for (const ShowAsNames &names : kShowAs) {
        if (names.as == as) {
            return QString::fromLatin1(names.x14);
        }
    }
    return {};
}

std::optional<PivotShowAs> showAsFrom(const QString &name)
{
    for (const ShowAsNames &names : kShowAs) {
        if ((names.ecma[0] != '\0' && name == QLatin1String(names.ecma))
            || (names.x14[0] != '\0' && name == QLatin1String(names.x14))) {
            return names.as;
        }
    }
    return std::nullopt;
}

QString groupBy(PivotGroupBy by)
{
    switch (by) {
    case PivotGroupBy::Seconds: return QStringLiteral("seconds");
    case PivotGroupBy::Minutes: return QStringLiteral("minutes");
    case PivotGroupBy::Hours: return QStringLiteral("hours");
    case PivotGroupBy::Days: return QStringLiteral("days");
    case PivotGroupBy::Months: return QStringLiteral("months");
    case PivotGroupBy::Quarters: return QStringLiteral("quarters");
    case PivotGroupBy::Years: return QStringLiteral("years");
    default: return QStringLiteral("range");
    }
}

PivotGroupBy groupByFrom(const QString &name)
{
    for (const PivotGroupBy by : {PivotGroupBy::Seconds, PivotGroupBy::Minutes, PivotGroupBy::Hours, PivotGroupBy::Days,
                                  PivotGroupBy::Months, PivotGroupBy::Quarters, PivotGroupBy::Years}) {
        if (name == groupBy(by)) {
            return by;
        }
    }
    return PivotGroupBy::Range;
}

QString filterType(const PivotFilter &filter)
{
    if (filter.kind == PivotFilter::Kind::Date) return filter.dateType;
    for (const FilterNames &names : kFilters) {
        if (names.kind == filter.kind
            && (filter.kind == PivotFilter::Kind::Top ? names.topBy == filter.topBy : names.op == filter.op)) {
            return QString::fromLatin1(names.type);
        }
    }
    return {};
}

std::optional<PivotFilter> filterFrom(const QString &type)
{
    if (pivotDateTypes().contains(type)) {
        PivotFilter filter;
        filter.kind = PivotFilter::Kind::Date;
        filter.dateType = type;
        return filter;
    }
    for (const FilterNames &names : kFilters) {
        if (type == QLatin1String(names.type)) {
            PivotFilter filter;
            filter.kind = names.kind;
            filter.op = names.op;
            filter.topBy = names.topBy;
            return filter;
        }
    }
    return std::nullopt;
}

QString isoDate(double serial)
{
    const double day = std::floor(serial);
    const qint64 seconds = qint64(std::llround((serial - day) * 86400.0));
    const QDateTime when(dateFromSerial(day), QTime(0, 0).addSecs(int(qBound(qint64(0), seconds, qint64(86399)))));
    return when.toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
}

std::optional<double> serialFromIso(const QString &text)
{
    QDateTime when = QDateTime::fromString(text, Qt::ISODate);
    if (!when.isValid()) {
        when = QDateTime(QDate::fromString(text.left(10), Qt::ISODate), QTime(0, 0));
    }
    if (!when.isValid()) {
        return std::nullopt;
    }
    return serialFromDate(when.date()) + when.time().msecsSinceStartOfDay() / 86400000.0;
}

} // namespace QindaOffice::Calc::IO::PivotNames
