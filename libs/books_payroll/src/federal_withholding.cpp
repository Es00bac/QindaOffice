// SPDX-License-Identifier: GPL-3.0-or-later
// IRS Publication 15-T (2026), Worksheet 1A, line by line
// (federal_withholding.h). The line texts are the publication's, shortened.
#include "federal_withholding.h"

namespace QindaOffice::Books::Payroll {

namespace {

using WL = WorksheetLine;

const RateSchedule &scheduleFor(const FederalWithholdingTables &t, const W4Certificate &w4)
{
    static const RateSchedule empty;
    if (w4.form == W4Certificate::Form::Before2020) {
        // Pub 15-T: "Married" uses the married table; "Single" and
        // "Married, but withhold at higher Single rate" the single one. No
        // Head of Household table for a 2019-or-earlier form.
        return w4.legacyStatus == W4LegacyStatus::Married ? t.legacyMarried : t.legacySingle;
    }
    const QMap<W4Status, RateSchedule> &tables = w4.step2Checked ? t.step2Checkbox : t.standard;
    const auto it = tables.constFind(w4.status);
    return it == tables.cend() ? empty : *it;
}

} // namespace

FederalWithholdingResult federalWithholding(const PayrollTables &tables, const Money &taxableWages,
                                            PayFrequency frequency, const W4Certificate &w4)
{
    const FederalWithholdingTables &t = tables.federalWithholding();
    FederalWithholdingResult r;
    Worksheet &ws = r.worksheet;
    if (w4.exempt) {
        ws << WL::note(QStringLiteral("W-4"), QStringLiteral("The employee claimed exemption from withholding"));
        return r;
    }
    const int periods = t.payPeriods.value(frequency, 0);
    if (periods <= 0) {
        ws << WL::note(QStringLiteral("1b"), QStringLiteral("Publication 15-T has no pay-period count for %1 payrolls")
                                                 .arg(payFrequencyLabel(frequency)));
        return r;
    }
    // ---- Step 1: adjust the employee's payment amount ----
    const Money line1a = nonNegative(taxableWages);
    const Money line1c = line1a.multipliedBy(periods);
    ws << WL::money(QStringLiteral("1a"), QStringLiteral("Total taxable wages this payroll period"), line1a)
       << WL::number(QStringLiteral("1b"), QStringLiteral("Number of pay periods per year (Table 3)"), periods)
       << WL::money(QStringLiteral("1c"), QStringLiteral("Multiply line 1a by line 1b"), line1c);
    Money adjusted;
    QString adjustedLine;
    if (w4.form == W4Certificate::Form::From2020) {
        const Money line1d = w4.step4aOtherIncome;
        const Money line1e = line1c + line1d;
        const Money line1f = w4.step4bDeductions;
        const Money line1g = w4.step2Checked ? usd(0)
                                             : (w4.status == W4Status::MarriedJointly ? t.step2UncheckedMarriedJointly
                                                                                      : t.step2UncheckedOther);
        const Money line1h = line1f + line1g;
        adjusted = nonNegative(line1e - line1h);
        adjustedLine = QStringLiteral("1i");
        ws << WL::money(QStringLiteral("1d"), QStringLiteral("Step 4(a) of Form W-4 (other income)"), line1d)
           << WL::money(QStringLiteral("1e"), QStringLiteral("Add lines 1c and 1d"), line1e)
           << WL::money(QStringLiteral("1f"), QStringLiteral("Step 4(b) of Form W-4 (deductions)"), line1f)
           << WL::money(QStringLiteral("1g"), w4.step2Checked ? QStringLiteral("Step 2 box checked")
                                                              : QStringLiteral("Step 2 box not checked"),
                        line1g)
           << WL::money(QStringLiteral("1h"), QStringLiteral("Add lines 1f and 1g"), line1h)
           << WL::money(QStringLiteral("1i"), QStringLiteral("Adjusted Annual Wage Amount"), adjusted);
    } else {
        const Money line1k = t.allowanceValue.multipliedBy(qMax(0, w4.allowances));
        adjusted = nonNegative(line1c - line1k);
        adjustedLine = QStringLiteral("1l");
        ws << WL::number(QStringLiteral("1j"), QStringLiteral("Allowances on the Form W-4 (2019 or earlier)"),
                         qMax(0, w4.allowances))
           << WL::money(QStringLiteral("1k"), QStringLiteral("Multiply line 1j by %1").arg(usdText(t.allowanceValue)),
                        line1k)
           << WL::money(QStringLiteral("1l"), QStringLiteral("Adjusted Annual Wage Amount"), adjusted);
    }
    // ---- Step 2: the Tentative Withholding Amount ----
    const std::optional<Bracket> row = bracketFor(scheduleFor(t, w4), adjusted);
    if (!row) {
        ws << WL::note(QStringLiteral("2b"), QStringLiteral("No row of the annual table holds line %1").arg(adjustedLine));
        return r;
    }
    const Money line2e = adjusted - row->over;
    const Money line2f = percentOf(line2e, row->rate);
    const Money line2g = row->base + line2f;
    const Money line2h = dividedBy(line2g, periods);
    ws << WL::money(QStringLiteral("2a"), QStringLiteral("Adjusted Annual Wage Amount (line %1)").arg(adjustedLine), adjusted)
       << WL::money(QStringLiteral("2b"), QStringLiteral("Column A of the table row"), row->over)
       << WL::money(QStringLiteral("2c"), QStringLiteral("Column C of that row"), row->base)
       << WL::percent(QStringLiteral("2d"), QStringLiteral("Column D of that row"), row->rate)
       << WL::money(QStringLiteral("2e"), QStringLiteral("Subtract line 2b from line 2a"), line2e)
       << WL::money(QStringLiteral("2f"), QStringLiteral("Multiply line 2e by line 2d"), line2f)
       << WL::money(QStringLiteral("2g"), QStringLiteral("Add lines 2c and 2f"), line2g)
       << WL::money(QStringLiteral("2h"), QStringLiteral("Divide line 2g by line 1b: Tentative Withholding Amount"), line2h);
    // ---- Step 3: tax credits (2020 or later forms only) ----
    const Money line3a = w4.form == W4Certificate::Form::From2020 ? w4.step3Credits : usd(0);
    const Money line3b = dividedBy(line3a, periods);
    const Money line3c = nonNegative(line2h - line3b);
    ws << WL::money(QStringLiteral("3a"), QStringLiteral("Step 3 of Form W-4 (credits)"), line3a)
       << WL::money(QStringLiteral("3b"), QStringLiteral("Divide line 3a by line 1b"), line3b)
       << WL::money(QStringLiteral("3c"), QStringLiteral("Subtract line 3b from line 2h"), line3c);
    // ---- Step 4: the final amount ----
    const Money line4a = w4.form == W4Certificate::Form::From2020 ? w4.step4cExtra : w4.legacyExtra;
    r.amount = line3c + line4a;
    ws << WL::money(QStringLiteral("4a"), QStringLiteral("Additional withholding (Step 4(c), or line 6 of an earlier form)"), line4a)
       << WL::money(QStringLiteral("4b"), QStringLiteral("Amount to withhold this pay period"), r.amount);
    return r;
}

Money supplementalWithholding(const PayrollTables &tables, const Money &supplemental, const Money &supplementalYearToDate)
{
    const SupplementalTables &s = tables.supplemental();
    if (!supplemental.isPositive()) {
        return usd(0);
    }
    // The part of this payment above the year's threshold takes the
    // mandatory rate; the rest the flat rate.
    const Money roomBelow = nonNegative(s.mandatoryThreshold - nonNegative(supplementalYearToDate));
    const Money atFlat = minMoney(supplemental, roomBelow);
    const Money atMandatory = supplemental - atFlat;
    return percentOf(atFlat, s.flatRate) + percentOf(atMandatory, s.mandatoryRate);
}

} // namespace QindaOffice::Books::Payroll
