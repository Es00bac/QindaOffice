// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Colorado payroll (M71, D-081): income tax withholding by the DR 1098
// "Colorado Withholding Worksheet for Employers" (2026), the employer's
// unemployment insurance premium (CDLE), the FAMLI premium and its
// employee/employer shares, and the local Occupational Privilege Taxes.
//
// AGENT-NOTE (DR 1098 rules, docs/payroll-tax-tables.md): a DR 0004 on file
// decides — its Line 2 (zero included) is the annual allowance, its Line 3
// the extra per pay period; a blank Line 2 or no DR 0004 uses the W-4's
// filing status (the married-filing-jointly allowance for married filing
// jointly or a qualifying surviving spouse, the other allowance otherwise);
// a W-4 claiming exemption with no DR 0004 withholds nothing; no
// certificate at all is treated as single (Rule 39-22-604-1(5)(c)). A
// 2019-or-earlier W-4 maps "Married" to the married allowance and its other
// two statuses to the other one — Pub 15-T's mapping; Colorado publishes
// none, so the tables list it as needing confirmation.
// The worksheet itself rounds nothing (its percentage and division lines
// round half-up to the cent here); the total per pay period is then
// rounded half-up to the whole dollar, because the Department says
// "Withholding tax must be deducted and paid in whole dollar amounts"
// (tax.colorado.gov, Withholding Filing Frequency & Due Dates) — a
// data-file flag, so a year that drops the rule drops the rounding.

#include "federal_withholding.h"
#include "payroll_tables.h"
#include "payroll_types.h"

namespace QindaOffice::Books::Payroll {

// Form DR 0004, Colorado Employee Withholding Certificate.
struct ColoradoCertificate {
    bool filed = false;
    std::optional<Money> annualAllowance; // Line 2; nullopt = left blank
    Money additionalPerPeriod = usd(0);   // Line 3
};

struct ColoradoWithholdingResult {
    Money amount = usd(0);
    Worksheet worksheet; // DR 1098 lines 1a … 2f
};

// `w4OnFile` false = the employee gave neither a W-4 nor a DR 0004.
[[nodiscard]] ColoradoWithholdingResult coloradoWithholding(const PayrollTables &tables, const Money &taxableWages,
                                                            PayFrequency frequency, const W4Certificate &w4,
                                                            const ColoradoCertificate &dr0004, bool w4OnFile = true);

// The part of this check's wages under a per-employee annual wage base
// (UI chargeable wages, FAMLI wages, social security, FUTA): what is left
// of the base after the year to date, never below zero.
[[nodiscard]] Money wagesUnderBase(const Money &wagesThisCheck, const Money &wagesYearToDate, const Money &base);

// The FAMLI premium on one check: `employeeShare` and `employerShare` are
// shares OF THE PREMIUM (50 % each by default; the employee's may not
// exceed the maximum the tables give; a small employer may pay 0 %). When
// the shares make up the whole premium, the employer's is the premium less
// the employee's, so the two always add up to the premium on the wages.
struct FamliPremium {
    Money wages = usd(0);
    Money premium = usd(0);
    Money employee = usd(0);
    Money employer = usd(0);
};
[[nodiscard]] FamliPremium famliPremium(const PayrollTables &tables, const Money &wagesThisCheck,
                                        const Money &wagesYearToDate, const Decimal &employeeShare,
                                        const Decimal &employerShare);

// A local Occupational Privilege Tax for one check: the monthly amounts
// are due once per calendar month, on the check that brings the month's
// compensation to the city's threshold ("at least" or "more than", as the
// city words it). A repealed tax charges nothing.
// AGENT-NOTE (owners): Denver's business OPT is also due on each owner or
// partner of a partnership or a disregarded LLC, whatever they earn — but
// an S corporation "has only employees, not working owners" (Denver Tax
// Guide Topic 61), so an owner on the payroll is an ordinary employee here.
// The owner's own OPT (employerOnOwners) is a reminder on the owner-pay
// side, never part of a paycheck.
struct OptCharge {
    Money employee = usd(0);
    Money employer = usd(0);
};
[[nodiscard]] OptCharge occupationalPrivilegeTax(const OccupationalPrivilegeTax &city, const Money &monthWagesBefore,
                                                 const Money &wagesThisCheck, bool alreadyChargedThisMonth);

// The Colorado UI premium on chargeable wages: each part of the rate
// computed and rounded to the cent on its own, then added (CDLE).
[[nodiscard]] Money uiPremium(const Money &chargeableWages, const UiRate &rate);
// The parts added together, for display ("3.05%").
[[nodiscard]] Decimal uiRateTotal(const UiRate &rate);

// Exact decimal arithmetic for rates (net FUTA rate, a share of a premium).
[[nodiscard]] Decimal rateMinus(const Decimal &a, const Decimal &b);
[[nodiscard]] Decimal ratePlus(const Decimal &a, const Decimal &b);
[[nodiscard]] Decimal rateTimes(const Decimal &a, const Decimal &b);

} // namespace QindaOffice::Books::Payroll
