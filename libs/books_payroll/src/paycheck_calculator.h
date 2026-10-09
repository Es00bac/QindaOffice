// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// One paycheck's taxes (M71, D-081): from gross pay, the pay frequency,
// pre-tax deductions, the year to date and the employee's certificates,
// every amount withheld from the employee and every tax the employer owes.
//
// Which wages each tax is on (docs/payroll-tax-tables.md "Wages for each
// tax"; IRS Pub 15 section 5 and Pub 15-B, Colorado guides):
//   * Section 125 (cafeteria plan) health premiums: out of federal and
//     Colorado income tax, social security, Medicare, FUTA and Colorado UI
//     (C.R.S. 8-70-142(1)(c)(VIII)) — but IN FAMLI wages, which are the
//     pay stub's "gross wages" (7 CCR 1107-1 §1.2(9); listed as needing
//     confirmation).
//   * 401(k) / SIMPLE IRA elective deferrals: out of federal and Colorado
//     income tax only; social security, Medicare, FUTA, UI and FAMLI tax
//     them.
//   * A more-than-2 % S corporation shareholder's health premiums the
//     company paid: wages for income tax (W-2 box 1, withholding computed
//     on them — IRS "S corporation compensation and medical insurance
//     issues", Notice 2008-1), not social security, Medicare or FUTA wages,
//     nor UI (which follows FUTA) or FAMLI (non-monetary); not cash — the
//     check does not pay them.
//   * Supplemental wages (a bonus) may take the flat federal rate; Colorado
//     withholds on them with the regular wages (DR 1098 has no flat rate).
//
// AGENT-NOTE (employer shares on totals): the IRS computes Form 941's
// social security and Medicare (lines 5a/5c) and Form 940's FUTA on the
// period's total wages, and CDLE each UI rate part on the quarter's total.
// So the employer's share on each check is the share on the employer's
// running total (all employees, the quarter or year so far, this check
// included) less what earlier live checks recorded — `EmployerToDate`. The
// books then equal the forms to the cent; only the employee side keeps
// per-check rounding, which Form 941 line 7 reports.
//
// AGENT-CONTRACT (books_engine payroll ledger): every amount here is what
// the paycheck records and posts; netPay = gross − pre-tax deductions −
// employee taxes − after-tax deductions, never negative (refused instead).

#include "colorado_payroll.h"
#include "federal_withholding.h"
#include "payroll_tables.h"

#include <QDate>
#include <QStringList>

namespace QindaOffice::Books::Payroll {

// The employer's own values (company settings, not tables).
struct EmployerSettings {
    std::optional<UiRate> uiRate;        // the rate CDLE assigned, part by part; none = the introductory rate
    Decimal famliEmployeeShare = Decimal(5, 1); // of the premium withheld from wages
    Decimal famliEmployerShare = Decimal(5, 1); // of the premium the company pays
    bool roundFederalWithholding = false; // Pub 15-T allows whole dollars when used consistently
    QString futaState = QStringLiteral("CO");
};

struct EmployeeTaxSetup {
    W4Certificate w4;
    bool w4OnFile = true;
    ColoradoCertificate dr0004;
    bool coloradoWithholding = true; // Colorado resident or works in Colorado
    QString optCity;                 // key of the city's OPT where they work ("" = none)
};

// The employee's wages and taxes earlier in the same calendar year.
struct YearToDate {
    Money socialSecurityWages = usd(0);
    Money medicareWages = usd(0);
    Money futaWages = usd(0);
    Money uiWages = usd(0);
    Money famliWages = usd(0);
    Money supplemental = usd(0);    // supplemental wages paid (the 37 % threshold)
    Money retirement = usd(0);      // elective deferrals withheld (the limit warning)
    Money monthWages = usd(0);      // gross compensation earlier in this calendar month (OPT)
    bool optChargedThisMonth = false;
};

// The employer's running totals of LIVE paychecks before this one (every
// employee): the calendar quarter for social security, Medicare and UI, the
// year for FUTA.
struct EmployerToDate {
    Money quarterSocialSecurityWages = usd(0);
    Money quarterSocialSecurityEmployer = usd(0);
    Money quarterMedicareWages = usd(0);
    Money quarterMedicareEmployer = usd(0);
    Money yearFutaWages = usd(0);
    Money yearFuta = usd(0);
    Money quarterUiWages = usd(0);
    Money quarterUi = usd(0);
};

struct PayInput {
    QDate payDate;
    PayFrequency frequency = PayFrequency::Biweekly;
    Money regular = usd(0);           // salary or hours × rate
    Money supplemental = usd(0);      // a bonus at the flat federal rate
    Money section125 = usd(0);        // pre-tax health premiums (cafeteria plan)
    Money retirement = usd(0);        // 401(k) / SIMPLE IRA elective deferral
    bool simpleIra = false;           // the deferral goes to a SIMPLE plan (its own limit)
    Money shareholderHealth = usd(0); // 2 % shareholder's health premiums (non-cash)
    Money afterTax = usd(0);          // deductions after taxes
    YearToDate ytd;
    EmployerToDate employerToDate;
};

struct PaycheckTaxes {
    bool ok = false;
    QString error;
    Money gross = usd(0);
    // Wages each tax is on (this check, after the wage bases).
    Money federalWages = usd(0);
    Money socialSecurityWages = usd(0);
    Money medicareWages = usd(0);
    Money futaWages = usd(0);
    Money coloradoWages = usd(0);
    Money uiWages = usd(0);
    Money famliWages = usd(0);
    // Withheld from the employee.
    Money federalIncomeTax = usd(0);
    Money socialSecurity = usd(0);
    Money medicare = usd(0);
    Money additionalMedicare = usd(0);
    Money coloradoIncomeTax = usd(0);
    Money famliEmployee = usd(0);
    Money optEmployee = usd(0);
    // Owed by the employer.
    Money socialSecurityEmployer = usd(0);
    Money medicareEmployer = usd(0);
    Money futa = usd(0);
    Money coloradoUi = usd(0);
    Money famliEmployer = usd(0);
    Money optEmployer = usd(0);
    // Deductions and the check.
    Money preTaxDeductions = usd(0);
    Money afterTaxDeductions = usd(0);
    Money employeeTaxes = usd(0);
    Money employerTaxes = usd(0);
    Money netPay = usd(0);
    Worksheet federalWorksheet;
    Worksheet coloradoWorksheet;
    QStringList notes; // unconfirmed values used, rounding, flat-rate bonus
};

[[nodiscard]] PaycheckTaxes computePaycheck(const PayrollTables &tables, const EmployerSettings &employer,
                                            const EmployeeTaxSetup &employee, const PayInput &pay);

// The net FUTA rate for a state: the gross rate less the maximum credit
// plus the state's credit reduction for the year (none listed = none).
[[nodiscard]] Decimal futaNetRate(const PayrollTables &tables, const QString &state);

} // namespace QindaOffice::Books::Payroll
