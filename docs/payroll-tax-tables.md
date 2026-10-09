<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Payroll and estimated-tax tables (tax year 2026)

QindaBooks computes paychecks, payroll liabilities and estimated taxes from
the official tables below (M71, [D-081](decisions.md#d-081-qindabooks-owner-pay-estimated-taxes-and-payroll-federal--colorado)):
the federal ones for every business, and Colorado's — the first state with
built-in tables — for a business that chooses Colorado in its payroll
setup. Any other state's or city's payroll taxes are payroll tax items a
business enters itself ([books.md](books.md#payroll-tax-items-any-other-state-or-locality)):
QindaBooks computes them as entered and sources nothing for them.
They ship as one data file per tax year,
`libs/books_payroll/resources/tables/payroll_2026.json` (schema 1), read by
`PayrollTables` (`payroll_tables.h`). **No rate or amount is written in
code**; a year without a file is refused in plain words ("QindaBooks has no
2027 payroll tax tables …") and nothing is computed for it.

Every value was read from the source named beside it on **2026-10-08**.
Values an official source could not confirm are listed under
[Needs confirmation](#needs-confirmation); the data file lists them under
`unconfirmed`; the app lists them in *Set Up Owner Pay & Payroll* (Colorado's
only for a business that chose Colorado) and adds a "Needs confirmation: …"
note to every paycheck or estimate that used one. Employer-specific values
— the UI rate CDLE assigns, the FAMLI shares, the deposit schedule, the
city — are company settings, not tables.

QindaBooks computes from these official tables. A business should confirm
how the IRS taxes it, and have a tax professional review its first payroll
and its first estimate.

## Sources

| Id | Document | Revision | URL |
| --- | --- | --- | --- |
| p15t | IRS Publication 15-T (2026), Federal Income Tax Withholding Methods | Dec 3, 2025 (Cat. No. 32112B) | https://www.irs.gov/pub/irs-pdf/p15t.pdf |
| p15 | IRS Publication 15 (2026), (Circular E), Employer's Tax Guide | Dec 15, 2025 (Cat. No. 10000W) | https://www.irs.gov/pub/irs-pdf/p15.pdf |
| ssa | SSA, Contribution and Benefit Base | Oct 24, 2025 | https://www.ssa.gov/oact/cola/cbb.html |
| fw4 | IRS Form W-4 (2026) | Created 12/8/25 | https://www.irs.gov/pub/irs-pdf/fw4.pdf |
| n2025-67 | IRS Notice 2025-67 (IR-2025-111), 2026 plan limits | Nov 13, 2025 | https://www.irs.gov/pub/irs-drop/n-25-67.pdf |
| f1040es | IRS Form 1040-ES (2026) | Feb 12, 2026 (Cat. No. 11340T) | https://www.irs.gov/pub/irs-pdf/f1040es.pdf |
| rp2025-32 | Rev. Proc. 2025-32, 2025-45 I.R.B. 695 | Nov 3, 2025 | https://www.irs.gov/pub/irs-drop/rp-25-32.pdf |
| p505 | IRS Publication 505 (2026) | Mar 31, 2026 (Cat. No. 15008E) | https://www.irs.gov/pub/irs-pdf/p505.pdf |
| i8959 | Instructions for Form 8959 (2025; 2026 draft of Aug 26, 2026) | Aug 7, 2025 | https://www.irs.gov/pub/irs-pdf/i8959.pdf |
| f1040sse | Schedule SE (Form 1040) 2026, early-release draft | Created 4/27/26 | https://www.irs.gov/pub/irs-dft/f1040sse--dft.pdf |
| f8995 | IRS Qualified Business Income Deduction page; Form 8995 (2026) draft | Sep 22, 2026; draft 5/1/26 | https://www.irs.gov/newsroom/qualified-business-income-deduction |
| p509-2027 | IRS Publication 509 (2027), early-release draft | Aug 24, 2026 | https://www.irs.gov/pub/irs-dft/p509--dft.pdf |
| dol-futa | U.S. DOL, FUTA Credit Reductions | Sep 11, 2026 | https://oui.doleta.gov/unemploy/futa_credit.asp |
| dr1098 | Colorado DR 1098 (2026), Withholding Worksheet for Employers | DR 1098 (10/21/25) | https://tax.colorado.gov/sites/default/files/documents/DR_1098_Colorado_Withholding_Worksheet_for_Employees.pdf |
| dr0004 | Colorado DR 0004 (2026), Employee Withholding Certificate | DR 0004 (10/29/25) | https://tax.colorado.gov/sites/default/files/documents/DR_0004_2026.pdf |
| co-wh-guide | Colorado Wage Withholding Tax Guide | Revised January 2026 | https://tax.colorado.gov/sites/default/files/documents/Wage_Withholding_Tax_Guide_Jan_2026.pdf |
| co-wh-frequency | Colorado DOR, Withholding Filing Frequency & Due Dates | web page | https://tax.colorado.gov/withholding-filing-frequency-due-dates |
| co-rule-604 | Colorado Rule 39-22-604-1 | effective Mar 2, 2026 | https://www.coloradosos.gov/CCR/eDocketDetails.do?trackingNum=2025-00396 |
| dr0104ep | Colorado DR 0104EP (2026), Individual Estimated Income Tax | DR 0104EP (06/10/25) | https://tax.colorado.gov/sites/default/files/documents/DR_0104EP_2026.pdf |
| crs-39-22-104 | C.R.S. 39-22-104(1.7)(c) | 2024 C.R.S. | https://content.leg.colorado.gov/sites/default/files/images/olls/crs2024-title-39.pdf |
| co-lcs-2026-09 | Colorado Legislative Council Staff, September 2026 Forecast | Sep 17, 2026 | https://content.leg.colorado.gov/sites/default/files/2026-09/sept2026-forecast-with-cover-for-remediation-and-posting-accessible.pdf |
| cdle-ui | CDLE, UI Premium Rates and Introductory Rates | web pages | https://cdle.colorado.gov/employers/unemployment-insurance-premiums/introductory-rates |
| famli | SB25-144 (C.R.S. 8-13.3-507); 7 CCR 1107-1; famli.colorado.gov | May 30, 2025; Jan 1, 2026 | https://famli.colorado.gov/individuals-and-families/how-famli-works/premium-and-benefits-calculator |
| denver-opt | Denver Tax Guide Topic 61; Treasury Tax Rule 004 | 04/2026; Sep 18, 2026 | https://denvergov.org/files/assets/public/v/2/finance/documents/treasury/tax-guides/taxguidetopic61_occupationalprivilegetaxes_1.pdf |
| aurora-opt | City of Aurora, OPT repeal (Ordinance 2022-77) | effective Jan 1, 2025 | https://www.auroragov.org/business_services/taxes/occupational_privilege_tax |
| gv-opt | Greenwood Village OPT; Code ch. 4 arts. 6–7 | Code through Mar 2, 2026 | https://www.greenwoodvillage.com/1220/Occupational-Privilege-Tax-OPT |
| sheridan-opt | Sheridan OPT; Code §§ 62-231–62-240 | Code through Apr 27, 2026 | https://www.ci.sheridan.co.us/288/Occupational-Privilege-Tax |
| glendale-opt | Glendale OPT; Municipal Code ch. 3.21 | Code through Dec 2, 2025 | https://glendale.co.us/355/Occupational-Privilege-Tax |
| co-holidays | Colorado Judicial, 2026 and FY 2027 holiday schedules (C.R.S. 24-11-101) | Sep 2, 2025; Jul 1, 2026 | https://www.coloradojudicial.gov/sites/default/files/2026-07/Holiday%20Schedule%202026%20Calendar%20Year%20v.2.pdf |

## Federal withholding (Publication 15-T, Worksheet 1A)

`federal_withholding.cpp` follows Worksheet 1A line by line (1a–1i for a
2020-or-later Form W-4, 1a–1c and 1j–1l for an earlier one, then 2a–2h,
3a–3c, 4a–4b) and reports every line under its own number.

| Value | 2026 | Source |
| --- | --- | --- |
| Pay periods (Table 3) | semiannual 2, quarterly 4, monthly 12, semimonthly 24, biweekly 26, weekly 52, daily 260 — Table 3 has **no annual column**, so an annual federal payroll is not offered | p15t p. 10 |
| Line 1g, Step 2 not checked | $12,900 married filing jointly; $8,600 otherwise | p15t p. 10 |
| Line 1k, per allowance (2019-or-earlier W-4) | $4,300 | p15t p. 10 |
| Annual percentage tables | STANDARD and Step 2 Checkbox schedules for MFJ, Single/MFS, HoH (every row in the data file) | p15t p. 12 |
| 2019-or-earlier W-4 | the STANDARD rows ("Married" → MFJ, "Single" and "Married, but withhold at higher Single rate" → Single; no HoH) | p15t pp. 5–6, 10, 65 |
| Rounding | none in the worksheet; "You may also round the tax for the pay period to the nearest dollar … consistently" — a company setting, off by default | p15t p. 9 |
| Supplemental wages | 22 % flat; 37 % on supplemental wages over $1,000,000 in the year | p15 §7 |
| Nonresident aliens | not supported (Pub 15-T adds Table 1/2 amounts before line 1a) | p15t p. 7 |

**Checks.** `tst_payroll_tables` recomputes every base from the row above
(exact for five schedules; the Step 2 Single schedule prints each base as
exactly half the STANDARD Single base — $20,512.00, not the recomputed
$20,512.12 — so the test pins that relation), and proves the STANDARD
thresholds equal the 1040-ES brackets shifted by the standard deduction less
line 1g ($19,300 / $7,500 / $15,550) — two documents published separately
agree. Pub 15-T (2026) prints **no percentage-method worked example**; its
nonresident-alien example (p. 7, wage-bracket method, $31) is reproduced
through Worksheet 1A ($31.23, $31 to the dollar).

## Social security, Medicare, FUTA, deposits

| Value | 2026 | Source |
| --- | --- | --- |
| Social security rate | 6.2 % employee + 6.2 % employer | p15 §9 |
| Social security wage base | $184,500 | ssa; p15 |
| Medicare rate | 1.45 % each, no wage base | p15 §9 |
| Additional Medicare withholding | 0.9 % of wages over $200,000 in the calendar year, employee only | p15 §9 |
| FUTA | 6.0 % of the first $7,000 per employee; credit up to 5.4 % → 0.6 % | p15 §14 |
| FUTA credit reduction, Colorado | none for 2025 (final list) and none possible for 2026: only states with a Title XII advance balance on January 1, 2026 can be reduced, and DOL's potential list names only California (5.3 %) and the Virgin Islands (4.8 %). DOL publishes the final 2026 list after November 10, 2026 | dol-futa |
| FUTA deposits | deposit when undeposited FUTA is more than $500, by the last day of the month after the quarter; else carry forward | p15 §14 |
| Lookback | more than $50,000 in the lookback period → semiweekly; new employers are monthly depositors | p15 §11 |
| Next-day rule | $100,000 accumulated on any day → deposit by the next business day; semiweekly from then on | p15 §11 |
| De minimis | under $2,500 for the quarter (no $100,000 day) → may pay with Form 941 | p15 §11 |
| Form 944 | only on IRS notice, for annual liability of $1,000 or less | p15 |
| 401(k) deferral limit | $24,500 (+$8,000 at 50+); SIMPLE $17,000 (+$4,000) — a warning, the plan enforces it | n2025-67 |
| Legal holidays (DC) | 2026: Jan 1, Jan 19, Feb 16, Apr 16, May 25, Jun 19, Jul 3, Sep 7, Oct 12, Nov 11, Nov 26, Dec 25; 2027 (draft Pub 509): Jan 1, Jan 18, Feb 15, Apr 16 | p15 §11; p509-2027 |

**Due dates** (`payroll_calendar.cpp`): monthly deposits by the 15th of the
next month; semiweekly by the third business day after the Wednesday–Friday
or Saturday–Tuesday period (Pub 15's "one more day per legal holiday" is
exactly that), a period split at a quarter end; returns (941, 940, W-2/W-3)
by the last day of the month after the period, every date moved past
weekends and DC legal holidays. Q3 2026's Form 941 is due Monday November 2,
2026; Q4's and Form 940/W-2 Monday February 1, 2027.

## Wages for each tax

| Paid as | Fed. income tax | Social security / Medicare | FUTA | CO income tax | CO UI | FAMLI |
| --- | --- | --- | --- | --- | --- | --- |
| Salary, hourly, bonus | yes | yes | yes | yes | yes | yes |
| Section 125 health premium (pre-tax) | no | no | no | no | no | yes — gross wages (needs confirmation, below) |
| 401(k) / SIMPLE elective deferral | no | yes | yes | no | yes | yes |
| > 2 % S-corp shareholder's health premiums | yes (W-2 box 1) | no | no | yes | no | no (non-monetary) |

Sources: Pub 15 §§ 5, 9; Pub 15-B; the Colorado guide ("wages subject to
Colorado wage withholding requirements generally include any wages subject
to federal wage withholding"); C.R.S. 8-70-142 (Colorado UI wages leave out
cafeteria-plan reductions, as FUTA does); 7 CCR 1107-1 §1.2(9) (FAMLI
"wages" are "gross wages", monetary compensation).

**FAMLI and pre-tax deductions.** The FAMLI rules define the premium's
wages as "gross wages" — the pay stub's gross, "pretax earnings" — and say
nothing more about cafeteria-plan (Section 125) reductions. QindaBooks
charges the premium on gross pay before every deduction and flags each
paycheck where that choice changed the premium (one with a Section 125
deduction) as needing confirmation.

## Colorado

The first state with built-in tables, applied only for a business that
chose Colorado (`PayrollSetup.colorado`). Its withholding, UI, FAMLI, city
taxes, DR 0104EP estimates and forms do not appear for any other business.

| Value | 2026 | Source |
| --- | --- | --- |
| Income tax rate | 4.40 % (no TABOR reduction for 2026: revenue fell $175.9 million short of the cap; the next cut is 2029) | crs-39-22-104; dr1098; dr0104ep; co-lcs-2026-09 |
| DR 1098 line 2a without DR 0004 Line 2 | $11,000 married filing jointly or qualifying surviving spouse; $5,500 otherwise | dr1098 |
| DR 1098 pay periods | annual 1, semiannual 2, quarterly 4, monthly 12, semimonthly 24, biweekly 26, weekly 52, daily 260 | dr1098 |
| Rounding | the worksheet rounds nothing; "Withholding tax must be deducted and paid in whole dollar amounts" → the per-period amount is rounded half-up to the dollar | co-wh-frequency |
| No certificate | as if a W-4 with "single" | co-rule-604 (5)(c) |
| W-4 exempt, no DR 0004 | zero (send the W-4 to the Department when over $200 a week) | dr1098 rule 3 |
| DR 1094 frequency | quarterly under $7,000 a year; monthly $7,000–$50,000; weekly more than $50,000 (the guide says "$50,000 or more"; the newer rule governs) | co-rule-604 (7) |
| DR 1094 due | quarterly: last day of the month after the quarter; monthly: the 15th; weekly: third business day after the Friday; the year's last weekly period ends December 31 | co-wh-guide |
| DR 1093 / W-2s | by January 31; electronically for 10 or more W-2s ("more than 10" on another page) | co-wh-guide |
| UI chargeable wage base | $30,600 per employee | cdle-ui |
| UI new-employer rate | non-construction, general construction and trades: 1.53 % beginning + 0.17 % support + 1.35 % solvency surcharge = 3.05 %; heavy construction 4.28 % + 0.48 % + 1.525 % = 6.285 %; each part computed and rounded separately | cdle-ui |
| FAMLI premium | 0.88 % of wages up to the social security base ($184,500); up to half from the employee; employers with fewer than 10 employees need not pay the employer half; half a cent rounds up | famli |
| Estimated tax | required when owing more than $1,000 after withholding; the smaller of 70 % of this year, 100 % of last year (federal AGI $150,000 or less, $75,000 MFS) or 110 %; due Apr 15, Jun 15, Sep 15, 2026 and Jan 15, 2027; whole dollars | dr0104ep |
| Legal holidays | 2026: Jan 1, Jan 19, Feb 16, May 25, Jun 19, Jul 3, Sep 7, Oct 5 (Cabrini Day), Nov 11, Nov 26, Dec 25; 2027: Jan 1, Jan 18, Feb 15 | co-holidays (the state HR schedule misprints MLK Day as "Monday, Jan. 16" while naming the 3rd Monday) |

### Local Occupational Privilege Taxes (per calendar month)

| City | Employee | Employer | Taxable when the month's pay is | Returns | Source |
| --- | --- | --- | --- | --- | --- |
| Denver | $5.75 | $4.00 per taxable employee (also each owner/partner of a partnership or disregarded LLC, whatever they earn; an S corporation has only employees) | at least $500 | quarterly under 60 employees, monthly 60+; last day of the month after; owners alone may pay the year by Jan 31 | denver-opt |
| Aurora | — | — | repealed effective Jan 1, 2025 | none | aurora-opt |
| Greenwood Village | $2.00 | $2.00 (owners under $250: employer part only) | at least $250 | monthly, last day of the next month | gv-opt |
| Sheridan | $3.00 | $3.00 | more than $500 | monthly by the last day of the next month; fewer than 10 employees may file quarterly | sheridan-opt |
| Glendale | $5.00 | $5.00 (also each owner/partner) | at least $750 (code) | monthly over $60 a month, quarterly under $100 a quarter; the 20th of the next month | glendale-opt |

## Estimated tax (Form 1040-ES, Schedule SE, Form 8959, Form 8995)

| Value | 2026 | Source |
| --- | --- | --- |
| Tax Rate Schedules X, Y-1, Y-2, Z | every row in the data file | f1040es p. 9; rp2025-32 §4.01 |
| Standard deduction | $16,100 single/MFS; $32,200 MFJ/QSS; $24,150 HoH | f1040es; rp2025-32 §4.14 |
| Self-employment tax | 92.35 % of profit; 12.4 % up to $184,500 less wages subject to social security; 2.9 % on all; none under $400 of net earnings; half deductible | f1040es p. 8; f1040sse (draft) |
| Additional Medicare | 0.9 % over $250,000 MFJ, $125,000 MFS, $200,000 otherwise; SE threshold reduced by Medicare wages | i8959 |
| QBI (simplified, Form 8995) | 20 % of QBI, limited to 20 % of taxable income before it; at or below $201,750 ($201,775 MFS, $403,500 MFJ); minimum $400 deduction with at least $1,000 of active QBI | rp2025-32 §§2.12, 4.26; f8995 |
| Safe harbor | smaller of 90 % of 2026 tax and 100 % of 2025 tax (110 % if 2025 AGI over $150,000, $75,000 MFS) | f1040es; p505 |
| Payments required | when owing $1,000 or more after withholding | f1040es |
| Due dates | Apr 15, Jun 15, Sep 15, 2026; Jan 15, 2027 (not needed if the return is filed and paid by Feb 1, 2027) | f1040es p. 4 |

**What the planner models and what it does not.** Modelled: the SE
worksheet, Additional Medicare Tax, the simplified QBI deduction, the rate
schedules, credits as entered, withholding, the safe harbors, the $1,000
test, Colorado tax as 4.40 % of federal taxable income. **Not modelled**:
the QBI deduction above the threshold (Form 8995-A — left out, so the
estimate is higher, and the result says so), capital gains rates, AMT, NIIT,
the annualized installment method (Form 2210 Schedule AI), Colorado
additions and subtractions, farming and fishing rules. Pub 505's safe-harbor
example ($46,839 required) and its amended-installment example ($2,175 due
for the third period) are reproduced in `tst_payroll_estimates`.

## Needs confirmation

| Value | Why | What QindaBooks uses |
| --- | --- | --- |
| Colorado withholding for a 2019-or-earlier Form W-4 | DR 1098, DR 0004, the January 2026 guide and Rule 39-22-604-1 do not address it | "Married" → the married allowance; otherwise the other allowance (Pub 15-T's mapping) — ask the employee for a DR 0004 or a 2026 W-4 |
| Glendale OPT at exactly $750 | the code says "no less than $750", the city's page "more than $750" | at least $750 (the code) |
| Sheridan's quarterly due date | the code allows quarterly returns but gives no date | the last day of the month after the quarter |
| FAMLI premium on Section 125 deductions | "gross wages" (7 CCR 1107-1 §1.2(9)); nothing says outright whether cafeteria-plan reductions stay in | gross pay before every deduction |

Not values, but worth knowing: the 2026 Schedule SE, Form 8959 and Form
8995 are still early-release drafts (their amounts are confirmed by final
documents: Form 1040-ES, Rev. Proc. 2025-32, Pub 505); DOL's final 2026
FUTA credit-reduction list comes after November 10, 2026 (Colorado cannot
be on it); and no official 2026 DR 1098 worked example exists.

## The data file

`payroll_<year>.json`: `schema`, `taxYear`, `sources[]` (`id`, `title`,
`url`, `revised`, `checked`), `unconfirmed[]` (`path`, `label`, `note`),
`federal` (`withholding`, `fica`, `futa`, `supplemental`, `deposits`,
`retirement`, `holidays`), `estimatedTax`, `colorado` (`incomeTaxRate`,
`withholding`, `filing`, `ui`, `famli`, `opt`, `estimated`, `holidays`).
Amounts are decimal text in dollars, rates percent text, schedules rows of
`[at least, less than ("" = no limit), base, rate]`. The reader refuses a
missing or malformed value with its path, a schedule with a gap, and a file
whose schema it does not know. Adding 2027 = a new file and its `.qrc` row,
every value sourced here.
