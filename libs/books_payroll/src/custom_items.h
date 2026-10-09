// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Payroll tax items a company defines itself (D-081 amendment, docs/books.md
// "Payroll tax items"): the payroll taxes of any state or locality that
// QindaBooks has no built-in tables for — a state unemployment or
// disability insurance tax, a city tax — each a flat percentage of wages
// with an optional wage base and annual limit, on the employee's side
// (withheld) or the employer's (an expense); and state income tax
// withholding where no built-in table exists, for which each employee
// carries a flat percentage and/or a fixed extra amount per paycheck. Both
// are user-entered: QindaBooks computes them as typed and sources nothing.
//
// Wages:
//  * a percentage item ("tax") is on gross pay less Section 125 deductions
//    — 401(k) deferrals stay in, as for social security, FUTA and most
//    state unemployment and disability taxes — up to its wage base per
//    employee per calendar year;
//  * a withholding item is on the wages federal income tax is on (after
//    every pre-tax deduction).
// Cents round half-up; an annual limit caps what the item takes from (or
// costs for) one employee in a calendar year. Pure: no tables, files or
// clock.

#include "payroll_types.h"

#include <QList>
#include <QString>

#include <optional>

namespace QindaOffice::Books::Payroll {

struct CustomItem {
    qint64 id = 0;
    QString name;
    enum class Side { Employee, Employer };
    Side side = Side::Employer;
    enum class Kind { Tax, Withholding };
    Kind kind = Kind::Tax;
    Decimal rate;                     // Tax: the flat percentage (0.027 = 2.7 %)
    std::optional<Money> wageBase;    // wages subject per employee per calendar year
    std::optional<Money> annualLimit; // the most it takes per employee per calendar year
};

[[nodiscard]] QString customSideKey(CustomItem::Side side); // "employee" | "employer"
[[nodiscard]] std::optional<CustomItem::Side> customSideFromKey(const QString &key);
[[nodiscard]] QString customKindKey(CustomItem::Kind kind); // "tax" | "withholding"
[[nodiscard]] std::optional<CustomItem::Kind> customKindFromKey(const QString &key);

// One item on one paycheck.
struct CustomItemInput {
    CustomItem item;
    bool exempt = false;           // this employee is not subject to it
    Decimal employeeRate;          // Withholding: the employee's flat percentage
    Money employeeExtra = usd(0);  // Withholding: a fixed extra amount per paycheck
    Money ytdWages = usd(0);       // wages subject to it earlier this calendar year
    Money ytdAmount = usd(0);      // what it took earlier this calendar year
};

struct CustomItemResult {
    qint64 id = 0;
    QString name;
    CustomItem::Side side = CustomItem::Side::Employer;
    CustomItem::Kind kind = CustomItem::Kind::Tax;
    Money wages = usd(0);          // subject to it on this check (after the wage base)
    Money amount = usd(0);
    Worksheet worksheet;
};

// `taxWages`: the check's gross pay less Section 125; `incomeWages`: its
// federal income tax wages.
[[nodiscard]] CustomItemResult computeCustomItem(const CustomItemInput &input, const Money &taxWages,
                                                 const Money &incomeWages);

} // namespace QindaOffice::Books::Payroll
