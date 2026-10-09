// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Federal income tax withholding by IRS Publication 15-T, Worksheet 1A
// ("Employer's Withholding Worksheet for Percentage Method Tables for
// Automated Payroll Systems") — for a Form W-4 from 2020 or later and for
// one from 2019 or earlier (M71, D-081).
//
// AGENT-CONTRACT (tst_payroll_federal): the result carries every
// worksheet line under the publication's own number ("1a" … "4b"), so the
// app and the tests can follow the computation line by line. Only the
// division lines (2h, 3b) and the percentage (2f) round, half-up to the
// cent; nothing rounds to whole dollars (Pub 15-T allows it, never requires
// it).

#include "payroll_tables.h"
#include "payroll_types.h"

namespace QindaOffice::Books::Payroll {

// What the employee's Form W-4 says. Annual amounts are annual as the
// form asks for them; Step 4(c) and the old line 6 are per pay period.
struct W4Certificate {
    enum class Form { From2020, Before2020 };
    Form form = Form::From2020;
    // 2020 or later.
    W4Status status = W4Status::SingleOrMarriedSeparately;
    bool step2Checked = false;
    Money step3Credits = usd(0);
    Money step4aOtherIncome = usd(0);
    Money step4bDeductions = usd(0);
    Money step4cExtra = usd(0);
    // 2019 or earlier.
    W4LegacyStatus legacyStatus = W4LegacyStatus::Single;
    int allowances = 0;
    Money legacyExtra = usd(0);
    // "Exempt" claimed (either form): no federal income tax is withheld.
    bool exempt = false;
};

struct FederalWithholdingResult {
    Money amount = usd(0); // to withhold this pay period
    Worksheet worksheet;
};

// `taxableWages`: this pay period's wages subject to federal income tax
// withholding (gross pay less pre-tax deductions, plus taxable fringe
// benefits such as a 2 % shareholder's health premiums).
[[nodiscard]] FederalWithholdingResult federalWithholding(const PayrollTables &tables, const Money &taxableWages,
                                                          PayFrequency frequency, const W4Certificate &w4);

// The optional flat-rate method for supplemental wages (bonuses) paid
// separately from regular wages: the flat rate, or the mandatory rate on
// the part above the threshold for the year (Pub 15 section 7).
[[nodiscard]] Money supplementalWithholding(const PayrollTables &tables, const Money &supplemental,
                                            const Money &supplementalYearToDate);

} // namespace QindaOffice::Books::Payroll
