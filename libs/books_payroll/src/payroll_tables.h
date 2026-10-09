// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PayrollTables: one tax year's official numbers (M71, D-081), read from
// the versioned data file `:/books_payroll/payroll_<year>.json`
// (resources/tables/, documented value by value with its source in
// docs/payroll-tax-tables.md).
//
// AGENT-CONTRACT (data file ↔ code): the file carries `schema` (the
// layout version this loader reads, kSchemaVersion) and `taxYear`; every
// rate and amount the calculators use lives in it — code never writes a
// rate. A value the official sources could not confirm is listed under
// `unconfirmed` with a note; isConfirmed() lets a calculator say so in its
// result, and the app shows those values as needing confirmation.
//
// AGENT-GUARD: a year without a file is refused (forYear() returns nullopt
// and says which tables are missing). Never fall back to another year's
// numbers: withholding from last year's tables is a wrong paycheck.
//
// Employer-specific values (the UI rate CDLE assigns, the FAMLI shares,
// the deposit schedule, the local OPT city) are company settings, not
// tables; the tables carry only the published defaults and limits.

#include "payroll_types.h"

#include <QByteArray>
#include <QDate>
#include <QList>
#include <QMap>
#include <QString>

#include <optional>

namespace QindaOffice::Books::Payroll {

inline constexpr int kSchemaVersion = 1;

struct TableSource {
    QString id;      // "p15t"
    QString title;   // "IRS Publication 15-T (2026), Federal Income Tax Withholding Methods"
    QString url;
    QString revised; // the document's own revision date, as printed
    QString checked; // when the value was read (yyyy-MM-dd)
};

struct UnconfirmedValue {
    QString path;  // "colorado.opt.sheridan" — a data-file path (or a prefix of several)
    QString label; // what it is, for the app
    QString note;  // why it is unconfirmed and what was used
};

// IRS Publication 15-T, Worksheet 1A and the annual percentage-method
// tables for automated payroll systems.
struct FederalWithholdingTables {
    QMap<PayFrequency, int> payPeriods;  // Table 3
    Money step2UncheckedMarriedJointly;  // line 1g when Step 2 is not checked: MFJ
    Money step2UncheckedOther;           // … every other status
    Money allowanceValue;                // Form W-4 2019 or earlier: per allowance
    QMap<W4Status, RateSchedule> standard;      // Form W-4 2020+, Step 2 not checked
    QMap<W4Status, RateSchedule> step2Checkbox; // Form W-4 2020+, Step 2 checked
    RateSchedule legacyMarried;          // Form W-4 2019 or earlier: Married
    RateSchedule legacySingle;           // … Single (and "married, withhold at single rate")
};

struct FicaTables {
    Decimal socialSecurityRate;  // each of employee and employer
    Money socialSecurityWageBase;
    Decimal medicareRate;        // each of employee and employer
    Decimal additionalMedicareRate;
    Money additionalMedicareThreshold; // withholding starts on wages above it in the calendar year
};

struct FutaTables {
    Decimal grossRate;   // 6.0%
    Decimal maxCredit;   // 5.4%
    Money wageBase;      // per employee per calendar year
    // Credit reduction for the tax year, by state code; a state absent
    // from the map has none. `creditReductionNote` says how final it is.
    QMap<QString, Decimal> creditReduction;
    QString creditReductionNote;
    Money depositThreshold; // deposit when undeposited FUTA is more than this
};

struct SupplementalTables {
    Decimal flatRate;        // optional flat rate on supplemental wages
    Decimal mandatoryRate;   // on supplemental wages over the threshold in the year
    Money mandatoryThreshold;
};

struct DepositTables {
    Money lookbackThreshold;   // more than this in the lookback period = semiweekly
    Money nextDayThreshold;    // accumulating this much = deposit next business day
    Money quarterlyDeMinimis;  // less than this for the quarter may be paid with Form 941
    Money form944Limit;        // Form 944 for an annual liability of this or less (IRS notifies)
};

// Elective-deferral limits for the year (IRS Notice 2025-67): QindaBooks
// warns when a paycheck takes deferrals past them; the plan enforces them.
struct RetirementLimits {
    Money deferral401k;   // § 402(g)(1)
    Money catchUp401k;    // age 50 or older
    Money deferralSimple; // SIMPLE IRA / SIMPLE 401(k)
    Money catchUpSimple;
};

// Form 1040-ES, Schedule SE, Form 8959 and Form 8995 values.
struct EstimatedTaxTables {
    QMap<FilingStatus, RateSchedule> rateSchedules;
    QMap<FilingStatus, Money> standardDeduction;
    Decimal seNetEarningsFactor;   // 92.35%
    Decimal seSocialSecurityRate;  // 12.4%
    Decimal seMedicareRate;        // 2.9%
    Money seMinimumNetEarnings;    // no SE tax under this
    Money seSocialSecurityWageBase;
    Decimal seDeductionShare;      // 50% of SE tax is deductible
    Decimal additionalMedicareRate;
    QMap<FilingStatus, Money> additionalMedicareThreshold;
    Decimal qbiRate;               // 20%
    QMap<FilingStatus, Money> qbiThreshold; // the simplified Form 8995 applies at or below
    Money qbiMinimumDeduction;     // zero when the year has none
    Money qbiMinimumQualifiedIncome;
    Decimal safeHarborCurrentYear; // 90%
    Decimal safeHarborPriorYear;   // 100%
    Decimal safeHarborPriorYearHighIncome; // 110%
    Money highIncomeAgi;           // prior-year AGI above this → 110%
    Money highIncomeAgiSeparate;   // … married filing separately
    Money minimumBalanceDue;       // estimated payments needed when owing this much or more
    QList<QDate> dueDates;         // the four installments
};

// A Colorado city's Occupational Privilege Tax (a head tax per month).
struct OccupationalPrivilegeTax {
    QString key;             // "denver"
    QString city;            // "Denver"
    Money employeeMonthly;   // withheld from the employee for each month taxable
    Money employerMonthly;   // paid by the employer per taxable employee per month
    Money monthlyThreshold;  // the month's compensation from which the tax applies
    bool thresholdInclusive = true; // "at least" (true) or "more than" (false) the threshold
    bool employerOnOwners = false;  // the employer amount is also due on each owner or partner
    int dueDay = 0;          // returns due this day of the month after the period; 0 = its last day
    QString filing;          // how the city collects it, as the city says
    QString status;          // "" = in force; otherwise why nothing is withheld (a repeal)
};

// One part of a Colorado UI rate (CDLE: base, support, solvency). CDLE
// computes and rounds each part separately.
struct UiRatePart {
    QString name;  // "Base", "Support", "Solvency surcharge"
    Decimal rate;
};
using UiRate = QList<UiRatePart>;

struct ColoradoWithholdingTables {
    Decimal rate;                          // DR 1098 line 2c
    QMap<PayFrequency, int> payPeriods;    // DR 1098 Pay Period Table
    // DR 1098 line 2a when the employee gave no DR 0004 (or left its
    // Line 2 blank): by the expected filing status on Form W-4 Step 1(c).
    Money defaultAllowanceMarriedJointly;  // married filing jointly or qualifying surviving spouse
    Money defaultAllowanceOther;           // every other status
    // The Department: "Withholding tax must be deducted and paid in whole
    // dollar amounts" (DR 1098 itself has no rounding step).
    bool wholeDollars = false;
};

struct ColoradoTables {
    Decimal incomeTaxRate;
    ColoradoWithholdingTables withholding;
    Money uiWageBase;            // chargeable wages per employee per year
    UiRate uiIntroductoryRate;   // a new non-construction employer's rate, part by part
    UiRate uiHeavyConstructionRate;
    Decimal famliRate;           // premium as a share of wages
    Decimal famliMaxEmployeeShare; // of the premium the employee may be charged
    int famliSmallEmployerSize = 0; // fewer employees than this: no employer share required
    Money famliWageBase;
    QList<OccupationalPrivilegeTax> opt;
    // Individual estimated tax (DR 0104EP).
    Decimal estimatedSafeHarborCurrentYear;
    Decimal estimatedSafeHarborPriorYear;
    Decimal estimatedSafeHarborPriorYearHighIncome;
    Money estimatedHighIncomeAgi;          // last year's federal AGI above this → 110 %
    Money estimatedHighIncomeAgiSeparate;  // … married filing separately
    Money estimatedMinimumBalanceDue;      // payments required when owing MORE than this
    QList<QDate> estimatedDueDates;
    // DR 1094 filing frequency by the annual withholding liability
    // (Rule 39-22-604-1(7)): quarterly below `withholdingMonthlyFrom`,
    // weekly above `withholdingWeeklyAbove`, monthly between.
    Money withholdingMonthlyFrom;
    Money withholdingWeeklyAbove;
};

class PayrollTables {
public:
    // The bundled tables for `year`, or nullopt with `error` saying plainly
    // that the year has no tables (and therefore no payroll or estimates).
    [[nodiscard]] static std::optional<PayrollTables> forYear(int year, QString *error = nullptr);
    // Parses a data file (tests feed broken ones); validates the schema
    // version, every required value and every schedule's shape.
    [[nodiscard]] static std::optional<PayrollTables> fromJson(const QByteArray &json, QString *error = nullptr);
    // The years the build carries, ascending.
    [[nodiscard]] static QList<int> availableYears();
    // The plain sentence the app shows for a year without tables.
    [[nodiscard]] static QString missingYearMessage(int year);

    [[nodiscard]] int year() const { return m_year; }
    [[nodiscard]] const QList<TableSource> &sources() const { return m_sources; }
    [[nodiscard]] const QList<UnconfirmedValue> &unconfirmed() const { return m_unconfirmed; }
    // False when `path` (or a prefix of it, or a value under it) is listed
    // as unconfirmed.
    [[nodiscard]] bool isConfirmed(const QString &path) const;
    // The unconfirmed entries that touch `path` (for result notes).
    [[nodiscard]] QList<UnconfirmedValue> unconfirmedUnder(const QString &path) const;

    [[nodiscard]] const FederalWithholdingTables &federalWithholding() const { return m_withholding; }
    [[nodiscard]] const FicaTables &fica() const { return m_fica; }
    [[nodiscard]] const FutaTables &futa() const { return m_futa; }
    [[nodiscard]] const SupplementalTables &supplemental() const { return m_supplemental; }
    [[nodiscard]] const DepositTables &deposits() const { return m_deposits; }
    [[nodiscard]] const RetirementLimits &retirement() const { return m_retirement; }
    [[nodiscard]] const EstimatedTaxTables &estimatedTax() const { return m_estimated; }
    [[nodiscard]] const ColoradoTables &colorado() const { return m_colorado; }
    // Legal holidays (District of Columbia) that move federal due dates,
    // for the tax year and the January after it.
    [[nodiscard]] const QList<QDate> &federalHolidays() const { return m_federalHolidays; }
    // Colorado state holidays that move Colorado due dates.
    [[nodiscard]] const QList<QDate> &coloradoHolidays() const { return m_coloradoHolidays; }
    [[nodiscard]] const OccupationalPrivilegeTax *opt(const QString &key) const;

private:
    friend class TablesReader;
    int m_year = 0;
    QList<TableSource> m_sources;
    QList<UnconfirmedValue> m_unconfirmed;
    FederalWithholdingTables m_withholding;
    FicaTables m_fica;
    FutaTables m_futa;
    SupplementalTables m_supplemental;
    DepositTables m_deposits;
    RetirementLimits m_retirement;
    EstimatedTaxTables m_estimated;
    ColoradoTables m_colorado;
    QList<QDate> m_federalHolidays;
    QList<QDate> m_coloradoHolidays;
};

} // namespace QindaOffice::Books::Payroll
