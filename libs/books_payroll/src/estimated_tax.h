// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The estimated-tax planner for an owner taxed on the business's profit —
// a single-member LLC on Schedule C, or a partner (M71, D-081): federal
// self-employment and income tax by the Form 1040-ES (2026) worksheets,
// Colorado income tax by DR 0104EP, the required annual payment (the
// safe harbors), each quarter's amount and due date, what remains, and a
// suggested share of each owner draw to set aside.
//
// What is modelled (docs/payroll-tax-tables.md "Estimated tax"):
//  * 1040-ES Self-Employment Tax and Deduction Worksheet: 92.35 % of the
//    profit, 2.9 % Medicare on all of it, 12.4 % social security up to the
//    wage base less wages already subject to it, half deductible; none
//    under the $400 net-earnings floor (Schedule SE line 4c).
//  * Additional Medicare Tax (Form 8959) on wages and on self-employment
//    income over the filing status's threshold reduced by Medicare wages.
//  * The QBI deduction by the simplified Form 8995 when taxable income
//    before it is at or below the threshold: 20 % of QBI (profit less the
//    deductible half of SE tax and the business adjustments), limited to
//    20 % of taxable income before it, never below the minimum deduction
//    when QBI reaches the minimum. ABOVE the threshold (Form 8995-A: W-2
//    wages, property, specified service businesses) it is left out and the
//    result says so — the estimate is then higher, never lower.
//  * Tax Rate Schedules (worksheet line 4), credits as entered, the
//    required annual payment (90 % of this year, or 100 % / 110 % of last
//    year's tax), withholding, and the $1,000 test.
// Not modelled: AMT, capital gains rates, the annualized installment
// method, farming/fishing rules, NIIT, Colorado additions and
// subtractions. The planner never reads the clock.

#include "payroll_tables.h"
#include "payroll_types.h"

#include <QDate>
#include <QList>
#include <QStringList>

#include <optional>

namespace QindaOffice::Books::Payroll {

struct EstimatedPayment {
    enum class Jurisdiction { Federal, Colorado };
    Jurisdiction jurisdiction = Jurisdiction::Federal;
    QDate date;
    Money amount = usd(0);
};

struct EstimatedTaxInput {
    FilingStatus status = FilingStatus::Single;
    QDate asOf;                          // "today": annualizing and quarter status
    Money yearToDateProfit = usd(0);     // the business's net profit, Jan 1 through asOf
    std::optional<Money> projectedProfit; // the year's profit as the owner expects it (overrides annualizing)
    Money otherIncome = usd(0);          // other income in AGI: wages, interest, a spouse's income on a joint return
    Money wagesForSocialSecurity = usd(0); // W-2 box 3 wages among it (SE worksheet line 6)
    Money wagesForMedicare = usd(0);     // W-2 box 5 wages (Form 8959)
    Money businessAdjustments = usd(0);  // SE health insurance, retirement contributions: reduce AGI and QBI
    Money otherAdjustments = usd(0);     // other adjustments to income
    Money itemizedDeductions = usd(0);   // zero = the standard deduction
    Money additionalDeductions = usd(0); // Schedule 1-A and other deductions entered on line 2c
    Money credits = usd(0);              // nonrefundable credits (worksheet line 7)
    Money federalWithholding = usd(0);   // expected for the whole year (line 13)
    Money coloradoWithholding = usd(0);
    std::optional<Money> priorYearTax;   // last year's total federal tax (12b)
    std::optional<Money> priorYearAgi;
    bool priorYearCoveredFullYear = true;
    std::optional<Money> priorYearColoradoTax;
    QList<EstimatedPayment> payments;
};

struct Installment {
    int number = 0;            // 1–4
    QDate due;
    Money amount = usd(0);     // what this installment requires
    Money paid = usd(0);       // applied to it (payments go to the earliest installment)
    Money owed = usd(0);       // amount − paid
    enum class State { Upcoming, Paid, PaidLate, Overdue, NotRequired };
    State state = State::Upcoming;
};

struct EstimatePlan {
    Money totalTax = usd(0);            // the year's tax (federal 11c; Colorado net tax)
    Money withholding = usd(0);
    Money requiredAnnualPayment = usd(0); // the safe harbor (12c)
    QString safeHarborBasis;            // "90% of 2026 tax", "110% of 2025 tax", …
    bool paymentsRequired = false;
    QString reason;                     // why not, when not
    Money estimatedPaymentsNeeded = usd(0); // 14a: required annual payment less withholding
    QList<Installment> installments;
    Money paid = usd(0);
    Money remaining = usd(0);           // estimatedPaymentsNeeded − paid, not below zero
    Worksheet worksheet;
};

struct EstimatedTaxResult {
    bool ok = false;
    QString error;
    Money annualProfit = usd(0);
    bool annualized = false;
    Worksheet seWorksheet;      // 1040-ES Self-Employment Tax and Deduction Worksheet
    Money seTax = usd(0);
    Money seDeduction = usd(0);
    Money additionalMedicare = usd(0);
    Money qbiDeduction = usd(0);
    Money taxableIncome = usd(0);
    Money incomeTax = usd(0);
    EstimatePlan federal;
    Money coloradoTaxableIncome = usd(0);
    Money coloradoTax = usd(0);
    EstimatePlan colorado;
    // The extra federal and Colorado tax the business's profit causes, and
    // that as a whole percentage of the profit (rounded up): the share of
    // each draw to set aside.
    Money businessTax = usd(0);
    int setAsidePercent = 0;
    QStringList notes;          // what was left out, unconfirmed values used
};

[[nodiscard]] EstimatedTaxResult planEstimatedTax(const PayrollTables &tables, const EstimatedTaxInput &input);

// The federal required annual payment (worksheet lines 12a–12c): the
// smaller of 90 % of this year's tax and 100 % (110 % when last year's AGI
// was above the threshold) of last year's tax, when last year was a full
// year with a return. `basis` says which applied.
[[nodiscard]] Money federalRequiredAnnualPayment(const PayrollTables &tables, FilingStatus status,
                                                 const Money &currentYearTax, const std::optional<Money> &priorYearTax,
                                                 const std::optional<Money> &priorYearAgi, bool priorYearFull,
                                                 QString *basis = nullptr);

// Spreads `needed` over the due dates in equal parts (the cents — or, with
// `wholeDollars` (Colorado: "round your payment to the nearest dollar"),
// the dollars — left over go to the earliest) and applies the payments,
// oldest first, to the earliest installment still owing — the way the IRS
// applies them — so an installment paid short carries into the next due
// date. `paid` gets the payments' total.
[[nodiscard]] QList<Installment> scheduleInstallments(const Money &needed, bool required, const QList<QDate> &dueDates,
                                                     const QList<EstimatedPayment> &payments,
                                                     EstimatedPayment::Jurisdiction jurisdiction, const QDate &asOf,
                                                     Money *paid = nullptr, bool wholeDollars = false);
// What must be paid by `date`: everything owed on installments due then
// or earlier (Pub 505: an amended estimate's next payment).
[[nodiscard]] Money amountDueBy(const QList<Installment> &installments, const QDate &date);

// The profit for the whole year from a year-to-date figure: × days in the
// year ÷ days elapsed through `asOf`, half-up. A loss annualizes too.
[[nodiscard]] Money annualizeProfit(const Money &yearToDate, const QDate &asOf);

// The suggested set-aside for one draw: `percent` of it, half-up.
[[nodiscard]] Money setAsideFor(const Money &draw, int percent);

} // namespace QindaOffice::Books::Payroll
