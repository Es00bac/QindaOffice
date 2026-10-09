// SPDX-License-Identifier: GPL-3.0-or-later
// Company-defined payroll tax items (custom_items.h).
#include "custom_items.h"

namespace QindaOffice::Books::Payroll {

using WL = WorksheetLine;

QString customSideKey(CustomItem::Side side)
{
    return side == CustomItem::Side::Employee ? QStringLiteral("employee") : QStringLiteral("employer");
}

std::optional<CustomItem::Side> customSideFromKey(const QString &key)
{
    if (key == QLatin1String("employee")) {
        return CustomItem::Side::Employee;
    }
    if (key == QLatin1String("employer")) {
        return CustomItem::Side::Employer;
    }
    return std::nullopt;
}

QString customKindKey(CustomItem::Kind kind)
{
    return kind == CustomItem::Kind::Withholding ? QStringLiteral("withholding") : QStringLiteral("tax");
}

std::optional<CustomItem::Kind> customKindFromKey(const QString &key)
{
    if (key == QLatin1String("tax")) {
        return CustomItem::Kind::Tax;
    }
    if (key == QLatin1String("withholding")) {
        return CustomItem::Kind::Withholding;
    }
    return std::nullopt;
}

CustomItemResult computeCustomItem(const CustomItemInput &in, const Money &taxWages, const Money &incomeWages)
{
    CustomItemResult r;
    r.id = in.item.id;
    r.name = in.item.name;
    r.side = in.item.side;
    r.kind = in.item.kind;
    if (in.exempt) {
        r.worksheet << WL::note(QStringLiteral("1"), QStringLiteral("Not subject to %1 (set on the employee)").arg(in.item.name));
        return r;
    }
    const bool withholding = in.item.kind == CustomItem::Kind::Withholding;
    const Money wages = nonNegative(withholding ? incomeWages : taxWages);
    r.worksheet << WL::money(QStringLiteral("1"),
                             withholding ? QStringLiteral("Wages subject to income tax this check")
                                         : QStringLiteral("Wages this check (gross less Section 125)"),
                             wages);
    r.wages = wages;
    if (in.item.wageBase) {
        // Only what is left of the wage base this calendar year.
        const Money left = nonNegative(*in.item.wageBase - in.ytdWages);
        r.wages = minMoney(wages, left);
        r.worksheet << WL::money(QStringLiteral("2"), QStringLiteral("Wage base left this year (%1 less %2 so far)")
                                                           .arg(usdText(*in.item.wageBase), usdText(in.ytdWages)),
                                 left)
                    << WL::money(QStringLiteral("3"), QStringLiteral("Wages subject (the smaller)"), r.wages);
    }
    const Decimal rate = withholding ? in.employeeRate : in.item.rate;
    Money amount = percentOf(r.wages, rate);
    r.worksheet << WL::percent(QStringLiteral("4"), withholding ? QStringLiteral("Rate entered for this employee")
                                                                : QStringLiteral("Rate entered for the item"),
                               rate)
                << WL::money(QStringLiteral("5"), QStringLiteral("Wages subject × rate"), amount);
    if (withholding && in.employeeExtra.isPositive()) {
        amount += in.employeeExtra;
        r.worksheet << WL::money(QStringLiteral("6"), QStringLiteral("Extra each paycheck, as entered"), in.employeeExtra);
    }
    if (in.item.annualLimit) {
        const Money left = nonNegative(*in.item.annualLimit - in.ytdAmount);
        amount = minMoney(amount, left);
        r.worksheet << WL::money(QStringLiteral("7"), QStringLiteral("Annual limit left (%1 less %2 so far)")
                                                           .arg(usdText(*in.item.annualLimit), usdText(in.ytdAmount)),
                                 left);
    }
    r.amount = nonNegative(amount);
    r.worksheet << WL::money(QStringLiteral("8"), QStringLiteral("%1 this check").arg(in.item.name), r.amount);
    return r;
}

} // namespace QindaOffice::Books::Payroll
