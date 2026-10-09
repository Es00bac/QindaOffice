// SPDX-License-Identifier: GPL-3.0-or-later
// The Form 1040-ES (2026) worksheets and the DR 0104EP rules
// (estimated_tax.h). Line numbers are the forms' own.
#include "estimated_tax.h"

#include <algorithm>

namespace QindaOffice::Books::Payroll {

namespace {

using WL = WorksheetLine;

struct FederalTax {
    Worksheet se;
    Worksheet main;
    Money seTax = usd(0);
    Money seDeduction = usd(0);
    Money additionalMedicare = usd(0);
    Money qbi = usd(0);
    Money taxable = usd(0);
    Money incomeTax = usd(0);
    Money total = usd(0); // 11c
};

// Form 8959: Additional Medicare Tax on wages above the filing status's
// threshold and on self-employment income above the threshold reduced by
// those wages.
Money additionalMedicare(const EstimatedTaxTables &t, FilingStatus status, const Money &medicareWages,
                         const Money &seIncome)
{
    const Money threshold = t.additionalMedicareThreshold.value(status, usd(0));
    const Money onWages = nonNegative(medicareWages - threshold);
    const Money reduced = nonNegative(threshold - medicareWages);
    const Money onSe = nonNegative(nonNegative(seIncome) - reduced);
    return percentOf(onWages + onSe, t.additionalMedicareRate);
}

FederalTax federalTax(const PayrollTables &tables, const EstimatedTaxInput &in, const Money &profit, QStringList *notes)
{
    const EstimatedTaxTables &t = tables.estimatedTax();
    FederalTax r;
    // ---- Self-Employment Tax and Deduction Worksheet ----
    const Money line2 = nonNegative(profit);
    const Money line3 = percentOf(line2, t.seNetEarningsFactor);
    r.se << WL::money(QStringLiteral("1a"), QStringLiteral("Expected income and profits subject to self-employment tax"), line2)
         << WL::money(QStringLiteral("3"), QStringLiteral("Multiply line 2 by %1").arg(t.seNetEarningsFactor.toPercentString()), line3);
    Money seIncome = line3;
    if (line3 < t.seMinimumNetEarnings) {
        r.se << WL::note(QStringLiteral("3"), QStringLiteral("Less than %1: no self-employment tax")
                                                  .arg(usdText(t.seMinimumNetEarnings)));
        seIncome = usd(0);
    } else {
        const Money line4 = percentOf(line3, t.seMedicareRate);
        const Money line5 = t.seSocialSecurityWageBase;
        const Money line6 = in.wagesForSocialSecurity;
        const Money line7 = nonNegative(line5 - line6);
        const Money line8 = minMoney(line3, line7);
        const Money line9 = percentOf(line8, t.seSocialSecurityRate);
        r.seTax = line4 + line9;
        r.seDeduction = percentOf(r.seTax, t.seDeductionShare);
        r.se << WL::money(QStringLiteral("4"), QStringLiteral("Multiply line 3 by %1").arg(t.seMedicareRate.toPercentString()), line4)
             << WL::money(QStringLiteral("5"), QStringLiteral("Social security tax maximum income"), line5)
             << WL::money(QStringLiteral("6"), QStringLiteral("Expected wages subject to social security tax"), line6)
             << WL::money(QStringLiteral("7"), QStringLiteral("Subtract line 6 from line 5"), line7)
             << WL::money(QStringLiteral("8"), QStringLiteral("Smaller of line 3 or line 7"), line8)
             << WL::money(QStringLiteral("9"), QStringLiteral("Multiply line 8 by %1").arg(t.seSocialSecurityRate.toPercentString()), line9)
             << WL::money(QStringLiteral("10"), QStringLiteral("Self-employment tax (add lines 4 and 9)"), r.seTax)
             << WL::money(QStringLiteral("11"), QStringLiteral("Deduction for one-half of self-employment tax"), r.seDeduction);
    }
    r.additionalMedicare = additionalMedicare(t, in.status, in.wagesForMedicare, seIncome);

    // ---- 2026 Estimated Tax Worksheet ----
    const Money agi = profit + in.otherIncome - r.seDeduction - in.businessAdjustments - in.otherAdjustments;
    const Money line2a = in.itemizedDeductions.isPositive() ? in.itemizedDeductions
                                                            : t.standardDeduction.value(in.status, usd(0));
    const Money line2c = in.additionalDeductions;
    const Money beforeQbi = nonNegative(agi - line2a - line2c);
    const Money qbiIncome = profit - r.seDeduction - in.businessAdjustments;
    if (qbiIncome.isPositive()) {
        const Money threshold = t.qbiThreshold.value(in.status, usd(0));
        if (beforeQbi > threshold) {
            notes->append(QStringLiteral("The qualified business income deduction is left out: taxable income before it "
                                         "is above %1, where Form 8995-A's wage, property and service-business limits "
                                         "apply. The estimate is higher than the tax may turn out.")
                              .arg(usdText(threshold)));
        } else {
            r.qbi = minMoney(percentOf(qbiIncome, t.qbiRate), percentOf(beforeQbi, t.qbiRate));
            if (t.qbiMinimumDeduction.isPositive() && qbiIncome >= t.qbiMinimumQualifiedIncome) {
                r.qbi = maxMoney(r.qbi, t.qbiMinimumDeduction);
            }
        }
    }
    const Money line2d = line2a + r.qbi + line2c;
    r.taxable = nonNegative(agi - line2d);
    r.incomeTax = taxBySchedule(t.rateSchedules.value(in.status), r.taxable);
    const Money line8 = nonNegative(r.incomeTax - in.credits);
    const Money line11a = line8 + r.seTax + r.additionalMedicare;
    r.total = line11a;
    r.main << WL::money(QStringLiteral("1"), QStringLiteral("Adjusted gross income you expect"), agi)
           << WL::money(QStringLiteral("2a"), in.itemizedDeductions.isPositive() ? QStringLiteral("Itemized deductions")
                                                                                : QStringLiteral("Standard deduction"),
                        line2a)
           << WL::money(QStringLiteral("2b"), QStringLiteral("Qualified business income deduction"), r.qbi)
           << WL::money(QStringLiteral("2c"), QStringLiteral("Additional deductions (Schedule 1-A)"), line2c)
           << WL::money(QStringLiteral("2d"), QStringLiteral("Add lines 2a, 2b and 2c"), line2d)
           << WL::money(QStringLiteral("3"), QStringLiteral("Subtract line 2d from line 1"), r.taxable)
           << WL::money(QStringLiteral("4"), QStringLiteral("Tax by the %1 Tax Rate Schedules").arg(tables.year()), r.incomeTax)
           << WL::money(QStringLiteral("7"), QStringLiteral("Credits"), in.credits)
           << WL::money(QStringLiteral("8"), QStringLiteral("Subtract line 7 from line 6"), line8)
           << WL::money(QStringLiteral("9"), QStringLiteral("Self-employment tax"), r.seTax)
           << WL::money(QStringLiteral("10"), QStringLiteral("Other taxes (Additional Medicare Tax)"), r.additionalMedicare)
           << WL::money(QStringLiteral("11c"), QStringLiteral("Total %1 estimated tax").arg(tables.year()), r.total);
    return r;
}

// DR 0104EP worksheet lines 1–2, in whole dollars: the rate on the
// estimated Colorado taxable income (federal taxable income here — the
// planner models no Colorado additions or subtractions).
Money coloradoTaxOn(const PayrollTables &tables, const Money &federalTaxable)
{
    return roundToDollar(percentOf(roundToDollar(federalTaxable), tables.colorado().incomeTaxRate));
}

void schedule(EstimatePlan &plan, const QList<QDate> &dueDates, const QList<EstimatedPayment> &payments,
              EstimatedPayment::Jurisdiction jurisdiction, const QDate &asOf)
{
    plan.installments = scheduleInstallments(plan.estimatedPaymentsNeeded, plan.paymentsRequired, dueDates, payments,
                                             jurisdiction, asOf, &plan.paid,
                                             jurisdiction == EstimatedPayment::Jurisdiction::Colorado);
    plan.remaining = nonNegative(plan.estimatedPaymentsNeeded - plan.paid);
}

Money priorYearPart(const PayrollTables &tables, FilingStatus status, const Money &priorTax,
                    const std::optional<Money> &priorAgi, Decimal *rateOut = nullptr)
{
    const EstimatedTaxTables &t = tables.estimatedTax();
    const Money highAgi = status == FilingStatus::MarriedSeparately ? t.highIncomeAgiSeparate : t.highIncomeAgi;
    const Decimal rate = priorAgi && *priorAgi > highAgi ? t.safeHarborPriorYearHighIncome : t.safeHarborPriorYear;
    if (rateOut != nullptr) {
        *rateOut = rate;
    }
    return percentOf(priorTax, rate);
}

EstimatePlan federalPlan(const PayrollTables &tables, const EstimatedTaxInput &in, const FederalTax &tax)
{
    const EstimatedTaxTables &t = tables.estimatedTax();
    EstimatePlan plan;
    plan.totalTax = tax.total;
    plan.withholding = in.federalWithholding;
    plan.worksheet = tax.main;
    const Money line12a = percentOf(tax.total, t.safeHarborCurrentYear);
    plan.worksheet << WL::money(QStringLiteral("12a"), QStringLiteral("Multiply line 11c by %1").arg(t.safeHarborCurrentYear.toPercentString()), line12a);
    plan.requiredAnnualPayment = federalRequiredAnnualPayment(tables, in.status, tax.total, in.priorYearTax,
                                                              in.priorYearAgi, in.priorYearCoveredFullYear,
                                                              &plan.safeHarborBasis);
    if (in.priorYearTax && in.priorYearCoveredFullYear) {
        plan.worksheet << WL::money(QStringLiteral("12b"), QStringLiteral("Required annual payment based on %1 tax").arg(tables.year() - 1),
                                    priorYearPart(tables, in.status, *in.priorYearTax, in.priorYearAgi));
    }
    const Money line14a = plan.requiredAnnualPayment - plan.withholding;
    const Money line14b = tax.total - plan.withholding;
    plan.worksheet << WL::money(QStringLiteral("12c"), QStringLiteral("Required annual payment to avoid a penalty"), plan.requiredAnnualPayment)
                   << WL::money(QStringLiteral("13"), QStringLiteral("Income tax withheld and estimated to be withheld"), plan.withholding)
                   << WL::money(QStringLiteral("14a"), QStringLiteral("Subtract line 13 from line 12c"), line14a)
                   << WL::money(QStringLiteral("14b"), QStringLiteral("Subtract line 13 from line 11c"), line14b);
    if (!line14a.isPositive()) {
        plan.reason = QStringLiteral("Withholding already covers the required annual payment.");
    } else if (line14b < t.minimumBalanceDue) {
        plan.reason = QStringLiteral("You expect to owe less than %1 after withholding.").arg(usdText(t.minimumBalanceDue));
    } else {
        plan.paymentsRequired = true;
        plan.estimatedPaymentsNeeded = line14a;
    }
    return plan;
}

// DR 0104EP (2026): estimated payments when the net Colorado tax after
// withholding will be more than the minimum; the required annual amount is
// the smaller of the current-year share and last year's net tax (100 %,
// or 110 % when last year's federal AGI was above the threshold); the
// worksheet and the payments are whole dollars.
EstimatePlan coloradoPlan(const PayrollTables &tables, const EstimatedTaxInput &in, const Money &coTax)
{
    const ColoradoTables &c = tables.colorado();
    EstimatePlan plan;
    plan.totalTax = coTax;
    plan.withholding = in.coloradoWithholding;
    plan.requiredAnnualPayment = roundToDollar(percentOf(coTax, c.estimatedSafeHarborCurrentYear));
    plan.safeHarborBasis = QStringLiteral("%1 of %2 Colorado tax").arg(c.estimatedSafeHarborCurrentYear.toPercentString()).arg(tables.year());
    if (in.priorYearColoradoTax && in.priorYearCoveredFullYear) {
        const Money highAgi = in.status == FilingStatus::MarriedSeparately ? c.estimatedHighIncomeAgiSeparate
                                                                           : c.estimatedHighIncomeAgi;
        const bool high = in.priorYearAgi && *in.priorYearAgi > highAgi;
        const Decimal rate = high ? c.estimatedSafeHarborPriorYearHighIncome : c.estimatedSafeHarborPriorYear;
        const Money prior = roundToDollar(percentOf(*in.priorYearColoradoTax, rate));
        if (prior < plan.requiredAnnualPayment) {
            plan.requiredAnnualPayment = prior;
            plan.safeHarborBasis = QStringLiteral("%1 of %2 Colorado tax").arg(rate.toPercentString()).arg(tables.year() - 1);
        }
    }
    const Money owed = coTax - plan.withholding;
    plan.worksheet << WL::money(QStringLiteral("2"), QStringLiteral("Estimated %1 Colorado income tax (%2 of taxable income)")
                                                         .arg(tables.year()).arg(c.incomeTaxRate.toPercentString()), coTax)
                   << WL::money(QStringLiteral("8"), QStringLiteral("Estimated Colorado wage withholding"), plan.withholding)
                   << WL::money(QStringLiteral("9"), QStringLiteral("Net estimated tax"), owed)
                   << WL::money(QStringLiteral("R"), QStringLiteral("Required annual amount (%1)").arg(plan.safeHarborBasis),
                                plan.requiredAnnualPayment);
    const Money needed = plan.requiredAnnualPayment - plan.withholding;
    if (owed <= c.estimatedMinimumBalanceDue) {
        plan.reason = QStringLiteral("Colorado requires estimated payments only when you expect to owe more than %1 "
                                     "after withholding.")
                          .arg(usdText(c.estimatedMinimumBalanceDue));
    } else if (!needed.isPositive()) {
        plan.reason = QStringLiteral("Withholding already covers the required annual amount.");
    } else {
        plan.paymentsRequired = true;
        plan.estimatedPaymentsNeeded = roundToDollar(needed);
    }
    return plan;
}

} // namespace

Money annualizeProfit(const Money &yearToDate, const QDate &asOf)
{
    if (!asOf.isValid()) {
        return yearToDate;
    }
    const int days = QDate(asOf.year(), 12, 31).dayOfYear();
    return yearToDate.scaled(days, asOf.dayOfYear(), RoundingMode::HalfUp);
}

Money setAsideFor(const Money &draw, int percent)
{
    return draw.scaled(percent, 100, RoundingMode::HalfUp);
}

Money federalRequiredAnnualPayment(const PayrollTables &tables, FilingStatus status, const Money &currentYearTax,
                                   const std::optional<Money> &priorYearTax, const std::optional<Money> &priorYearAgi,
                                   bool priorYearFull, QString *basis)
{
    const EstimatedTaxTables &t = tables.estimatedTax();
    Money required = percentOf(currentYearTax, t.safeHarborCurrentYear);
    QString why = QStringLiteral("%1 of %2 tax").arg(t.safeHarborCurrentYear.toPercentString()).arg(tables.year());
    if (priorYearTax && priorYearFull) {
        Decimal rate;
        const Money prior = priorYearPart(tables, status, *priorYearTax, priorYearAgi, &rate);
        if (prior < required) {
            required = prior;
            why = QStringLiteral("%1 of %2 tax").arg(rate.toPercentString()).arg(tables.year() - 1);
        }
    }
    if (basis != nullptr) {
        *basis = why;
    }
    return required;
}

QList<Installment> scheduleInstallments(const Money &needed, bool required, const QList<QDate> &dueDates,
                                        const QList<EstimatedPayment> &payments,
                                        EstimatedPayment::Jurisdiction jurisdiction, const QDate &asOf, Money *paid,
                                        bool wholeDollars)
{
    QList<EstimatedPayment> mine;
    Money total = usd(0);
    for (const EstimatedPayment &p : payments) {
        if (p.jurisdiction == jurisdiction && p.amount.isPositive()) {
            mine << p;
            total += p.amount;
        }
    }
    if (paid != nullptr) {
        *paid = total;
    }
    std::stable_sort(mine.begin(), mine.end(), [](const auto &a, const auto &b) { return a.date < b.date; });
    QList<Installment> out;
    QList<Money> parts = (wholeDollars ? usd(roundToDollar(needed).minor() / 100) : needed).split(int(dueDates.size()));
    if (wholeDollars) {
        for (Money &part : parts) {
            part = part.multipliedBy(100);
        }
    }
    for (qsizetype i = 0; i < dueDates.size(); ++i) {
        Installment in;
        in.number = int(i + 1);
        in.due = dueDates.at(i);
        in.amount = required ? parts.value(i, usd(0)) : usd(0);
        out << in;
    }
    QList<QDate> completedOn(out.size());
    for (EstimatedPayment p : mine) {
        for (qsizetype i = 0; i < out.size() && p.amount.isPositive(); ++i) {
            Installment &in = out[i];
            const Money open = in.amount - in.paid;
            if (!open.isPositive()) {
                continue;
            }
            const Money applied = minMoney(open, p.amount);
            in.paid += applied;
            p.amount -= applied;
            if (in.paid == in.amount) {
                completedOn[i] = p.date;
            }
        }
    }
    for (qsizetype i = 0; i < out.size(); ++i) {
        Installment &in = out[i];
        in.owed = in.amount - in.paid;
        if (!in.amount.isPositive()) {
            in.state = Installment::State::NotRequired;
        } else if (!in.owed.isPositive()) {
            in.state = completedOn.at(i) > in.due ? Installment::State::PaidLate : Installment::State::Paid;
        } else {
            in.state = asOf.isValid() && asOf > in.due ? Installment::State::Overdue : Installment::State::Upcoming;
        }
    }
    return out;
}

Money amountDueBy(const QList<Installment> &installments, const QDate &date)
{
    Money due = usd(0);
    for (const Installment &in : installments) {
        if (in.due <= date) {
            due += in.owed;
        }
    }
    return due;
}

EstimatedTaxResult planEstimatedTax(const PayrollTables &tables, const EstimatedTaxInput &input)
{
    EstimatedTaxResult r;
    if (input.asOf.isValid() && input.asOf.year() != tables.year()) {
        r.error = PayrollTables::missingYearMessage(input.asOf.year());
        return r;
    }
    r.annualized = !input.projectedProfit.has_value();
    r.annualProfit = input.projectedProfit ? *input.projectedProfit : annualizeProfit(input.yearToDateProfit, input.asOf);
    const FederalTax fed = federalTax(tables, input, r.annualProfit, &r.notes);
    r.seWorksheet = fed.se;
    r.seTax = fed.seTax;
    r.seDeduction = fed.seDeduction;
    r.additionalMedicare = fed.additionalMedicare;
    r.qbiDeduction = fed.qbi;
    r.taxableIncome = fed.taxable;
    r.incomeTax = fed.incomeTax;
    r.federal = federalPlan(tables, input, fed);
    schedule(r.federal, tables.estimatedTax().dueDates, input.payments, EstimatedPayment::Jurisdiction::Federal, input.asOf);
    // Colorado starts from federal taxable income (DR 0104 line 1).
    r.coloradoTaxableIncome = fed.taxable;
    r.coloradoTax = coloradoTaxOn(tables, fed.taxable);
    r.colorado = coloradoPlan(tables, input, r.coloradoTax);
    schedule(r.colorado, tables.colorado().estimatedDueDates, input.payments, EstimatedPayment::Jurisdiction::Colorado,
             input.asOf);
    // The share of the profit to set aside: the tax with the business
    // less the tax without it.
    QStringList ignored;
    const FederalTax without = federalTax(tables, input, usd(0), &ignored);
    r.businessTax = nonNegative((fed.total + r.coloradoTax) - (without.total + coloradoTaxOn(tables, without.taxable)));
    if (r.annualProfit.isPositive()) {
        r.setAsidePercent = int(*scaleExact(r.businessTax.minor(), 100, r.annualProfit.minor(), RoundingMode::AwayFromZero));
    }
    for (const UnconfirmedValue &u : tables.unconfirmedUnder(QStringLiteral("estimatedTax"))
             + tables.unconfirmedUnder(QStringLiteral("colorado.incomeTaxRate"))
             + tables.unconfirmedUnder(QStringLiteral("colorado.estimated"))) {
        r.notes << QStringLiteral("Needs confirmation: %1 — %2").arg(u.label, u.note);
    }
    r.ok = true;
    return r;
}

} // namespace QindaOffice::Books::Payroll
