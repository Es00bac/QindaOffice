// SPDX-License-Identifier: GPL-3.0-or-later
// DR 1098 (2026), Colorado UI, FAMLI and the local OPTs (colorado_payroll.h).
#include "colorado_payroll.h"

namespace QindaOffice::Books::Payroll {

namespace {

using WL = WorksheetLine;

// Both decimals at the larger scale, mantissas aligned.
std::pair<qint64, qint64> aligned(const Decimal &a, const Decimal &b, int *scale)
{
    *scale = qMax(a.scale(), b.scale());
    return {a.mantissa() * pow10i(*scale - a.scale()), b.mantissa() * pow10i(*scale - b.scale())};
}

bool marriedForColorado(const W4Certificate &w4)
{
    if (w4.form == W4Certificate::Form::Before2020) {
        return w4.legacyStatus == W4LegacyStatus::Married;
    }
    return w4.status == W4Status::MarriedJointly;
}

} // namespace

Decimal rateMinus(const Decimal &a, const Decimal &b)
{
    int scale = 0;
    const auto [x, y] = aligned(a, b, &scale);
    return Decimal(x - y, scale).normalized();
}

Decimal ratePlus(const Decimal &a, const Decimal &b)
{
    int scale = 0;
    const auto [x, y] = aligned(a, b, &scale);
    return Decimal(x + y, scale).normalized();
}

Decimal rateTimes(const Decimal &a, const Decimal &b)
{
    // Rates have a handful of digits each (0.0088 × 0.5): the product and
    // its scale stay far inside qint64 and Decimal::kMaxScale.
    return Decimal(a.mantissa() * b.mantissa(), a.scale() + b.scale()).normalized();
}

ColoradoWithholdingResult coloradoWithholding(const PayrollTables &tables, const Money &taxableWages,
                                              PayFrequency frequency, const W4Certificate &w4,
                                              const ColoradoCertificate &dr0004, bool w4OnFile)
{
    const ColoradoWithholdingTables &t = tables.colorado().withholding;
    ColoradoWithholdingResult r;
    Worksheet &ws = r.worksheet;
    if (!dr0004.filed && w4OnFile && w4.exempt) {
        ws << WL::note(QStringLiteral("Rule 3"), QStringLiteral("W-4 claims exemption and no DR 0004: no Colorado withholding"));
        return r;
    }
    const int periods = t.payPeriods.value(frequency, 0);
    if (periods <= 0) {
        ws << WL::note(QStringLiteral("1b"), QStringLiteral("DR 1098 has no pay-period count for %1 payrolls")
                                                 .arg(payFrequencyLabel(frequency)));
        return r;
    }
    const Money line1a = nonNegative(taxableWages);
    const Money line1c = line1a.multipliedBy(periods);
    Money line2a;
    QString source;
    if (dr0004.filed && dr0004.annualAllowance) {
        line2a = nonNegative(*dr0004.annualAllowance);
        source = QStringLiteral("DR 0004 Line 2");
    } else if (w4OnFile && w4.form == W4Certificate::Form::Before2020) {
        line2a = marriedForColorado(w4) ? t.defaultAllowanceMarriedJointly : t.defaultAllowanceOther;
        source = QStringLiteral("2019-or-earlier W-4, mapped by QindaBooks — needs confirmation");
    } else if (w4OnFile && marriedForColorado(w4)) {
        line2a = t.defaultAllowanceMarriedJointly;
        source = QStringLiteral("W-4 filing status: married filing jointly");
    } else {
        line2a = t.defaultAllowanceOther;
        source = w4OnFile ? QStringLiteral("W-4 filing status: other than married filing jointly")
                          : QStringLiteral("No certificate: treated as single");
    }
    const Money line2b = nonNegative(line1c - line2a);
    const Money line2c = percentOf(line2b, t.rate);
    const Money line2d = dividedBy(line2c, periods);
    const Money line2e = dr0004.filed ? nonNegative(dr0004.additionalPerPeriod) : usd(0);
    const Money line2f = line2d + line2e;
    r.amount = t.wholeDollars ? roundToDollar(line2f) : line2f;
    ws << WL::money(QStringLiteral("1a"), QStringLiteral("Total taxable wages this payroll period"), line1a)
       << WL::number(QStringLiteral("1b"), QStringLiteral("Number of pay periods per year"), periods)
       << WL::money(QStringLiteral("1c"), QStringLiteral("Multiply line 1a by line 1b"), line1c)
       << WL::money(QStringLiteral("2a"), QStringLiteral("Annual withholding allowance (%1)").arg(source), line2a)
       << WL::money(QStringLiteral("2b"), QStringLiteral("Subtract line 2a from line 1c"), line2b)
       << WL::money(QStringLiteral("2c"), QStringLiteral("Multiply line 2b by %1").arg(t.rate.toPercentString()), line2c)
       << WL::money(QStringLiteral("2d"), QStringLiteral("Divide line 2c by line 1b"), line2d)
       << WL::money(QStringLiteral("2e"), QStringLiteral("Additional withholding (DR 0004 Line 3)"), line2e)
       << WL::money(QStringLiteral("2f"), QStringLiteral("Colorado withholding amount per pay period"), line2f);
    if (t.wholeDollars) {
        ws << WL::money(QStringLiteral("2f$"), QStringLiteral("Withheld in whole dollars"), r.amount);
    }
    return r;
}

Money wagesUnderBase(const Money &wagesThisCheck, const Money &wagesYearToDate, const Money &base)
{
    const Money room = nonNegative(base - nonNegative(wagesYearToDate));
    return minMoney(nonNegative(wagesThisCheck), room);
}

FamliPremium famliPremium(const PayrollTables &tables, const Money &wagesThisCheck, const Money &wagesYearToDate,
                          const Decimal &employeeShare, const Decimal &employerShare)
{
    const ColoradoTables &c = tables.colorado();
    FamliPremium p;
    p.wages = wagesUnderBase(wagesThisCheck, wagesYearToDate, c.famliWageBase);
    p.premium = percentOf(p.wages, c.famliRate);
    // AGENT-GUARD: the employee never pays more than the maximum share.
    const Decimal eeShare =
        rateMinus(c.famliMaxEmployeeShare, employeeShare).isNegative() ? c.famliMaxEmployeeShare : employeeShare;
    p.employee = percentOf(p.wages, rateTimes(c.famliRate, eeShare));
    if (ratePlus(eeShare, employerShare) == Decimal(1, 0)) {
        p.employer = p.premium - p.employee;
    } else {
        p.employer = percentOf(p.wages, rateTimes(c.famliRate, employerShare));
        p.premium = p.employee + p.employer;
    }
    return p;
}

OptCharge occupationalPrivilegeTax(const OccupationalPrivilegeTax &city, const Money &monthWagesBefore,
                                   const Money &wagesThisCheck, bool alreadyChargedThisMonth)
{
    OptCharge c;
    if (alreadyChargedThisMonth || !city.status.isEmpty() || !wagesThisCheck.isPositive()) {
        return c;
    }
    const Money month = monthWagesBefore + wagesThisCheck;
    const bool taxable = city.thresholdInclusive ? month >= city.monthlyThreshold : month > city.monthlyThreshold;
    if (taxable) {
        c.employee = city.employeeMonthly;
        c.employer = city.employerMonthly;
    }
    return c;
}

Money uiPremium(const Money &chargeableWages, const UiRate &rate)
{
    Money total = usd(0);
    for (const UiRatePart &part : rate) {
        total += percentOf(chargeableWages, part.rate);
    }
    return total;
}

Decimal uiRateTotal(const UiRate &rate)
{
    Decimal total;
    for (const UiRatePart &part : rate) {
        total = ratePlus(total, part.rate);
    }
    return total;
}

} // namespace QindaOffice::Books::Payroll
