// SPDX-License-Identifier: GPL-3.0-or-later
#include "paycheck_calculator.h"

namespace QindaOffice::Books::Payroll {

namespace {

void noteUnconfirmed(const PayrollTables &tables, const QString &path, QStringList *notes)
{
    for (const UnconfirmedValue &u : tables.unconfirmedUnder(path)) {
        const QString line = QStringLiteral("Needs confirmation: %1 — %2").arg(u.label, u.note);
        if (!notes->contains(line)) {
            notes->append(line);
        }
    }
}

} // namespace

Decimal futaNetRate(const PayrollTables &tables, const QString &state)
{
    const FutaTables &f = tables.futa();
    return ratePlus(rateMinus(f.grossRate, f.maxCredit), f.creditReduction.value(state, Decimal()));
}

PaycheckTaxes computePaycheck(const PayrollTables &tables, const EmployerSettings &employer,
                              const EmployeeTaxSetup &employee, const PayInput &pay)
{
    PaycheckTaxes r;
    if (pay.payDate.year() != tables.year()) {
        r.error = PayrollTables::missingYearMessage(pay.payDate.year());
        return r;
    }
    for (const Money &m : {pay.regular, pay.supplemental, pay.section125, pay.retirement, pay.shareholderHealth,
                           pay.afterTax}) {
        if (m.isNegative()) {
            r.error = QStringLiteral("Pay and deductions cannot be negative; void the paycheck to undo one.");
            return r;
        }
    }
    r.gross = pay.regular + pay.supplemental;
    r.preTaxDeductions = pay.section125 + pay.retirement;
    r.afterTaxDeductions = pay.afterTax;
    if (r.preTaxDeductions > r.gross) {
        r.error = QStringLiteral("Pre-tax deductions are more than the gross pay.");
        return r;
    }
    if (pay.retirement.isPositive()) {
        const RetirementLimits &lim = tables.retirement();
        const Money limit = pay.simpleIra ? lim.deferralSimple : lim.deferral401k;
        const Money catchUp = pay.simpleIra ? lim.catchUpSimple : lim.catchUp401k;
        if (pay.ytd.retirement + pay.retirement > limit) {
            r.notes << QStringLiteral("Elective deferrals this year reach %1, past the %2 limit of %3 (%4 more is "
                                      "allowed at age 50 or older); the plan must refund any excess.")
                           .arg(usdText(pay.ytd.retirement + pay.retirement))
                           .arg(tables.year())
                           .arg(usdText(limit), usdText(catchUp));
        }
    }
    // ---- the wages each tax is on (header comment) ----
    // Pre-tax deductions come out of the regular wages first.
    const Money regularAfterPreTax = nonNegative(pay.regular - r.preTaxDeductions);
    const Money supplementalAfterPreTax = pay.supplemental - nonNegative(r.preTaxDeductions - pay.regular);
    const Money ficaBase = r.gross - pay.section125; // 401(k) stays in it
    r.federalWages = regularAfterPreTax + supplementalAfterPreTax + pay.shareholderHealth;
    r.coloradoWages = employee.coloradoWithholding ? r.federalWages : usd(0);
    const FicaTables &fica = tables.fica();
    r.socialSecurityWages = wagesUnderBase(ficaBase, pay.ytd.socialSecurityWages, fica.socialSecurityWageBase);
    r.medicareWages = ficaBase;
    r.futaWages = wagesUnderBase(ficaBase, pay.ytd.futaWages, tables.futa().wageBase);
    r.uiWages = wagesUnderBase(ficaBase, pay.ytd.uiWages, tables.colorado().uiWageBase);

    // ---- federal income tax: Worksheet 1A on the regular wages, the flat
    // rate on a bonus ----
    const FederalWithholdingResult fit =
        federalWithholding(tables, regularAfterPreTax + pay.shareholderHealth, pay.frequency, employee.w4);
    r.federalWorksheet = fit.worksheet;
    r.federalIncomeTax = fit.amount;
    if (supplementalAfterPreTax.isPositive() && !employee.w4.exempt) {
        r.federalIncomeTax += supplementalWithholding(tables, supplementalAfterPreTax, pay.ytd.supplemental);
        r.notes << QStringLiteral("Bonus withheld at the flat %1 federal rate.")
                       .arg(tables.supplemental().flatRate.toPercentString());
    }
    if (employer.roundFederalWithholding) {
        r.federalIncomeTax = roundToDollar(r.federalIncomeTax);
    }

    // ---- social security and Medicare: the employee's share per check, the
    // employer's on the quarter's running total (header AGENT-NOTE): the
    // combined tax on the total less the employee share on the total, less
    // what earlier checks recorded ----
    const EmployerToDate &e2d = pay.employerToDate;
    r.socialSecurity = percentOf(r.socialSecurityWages, fica.socialSecurityRate);
    r.medicare = percentOf(r.medicareWages, fica.medicareRate);
    const auto employerShare = [](const Money &total, const Decimal &rate, const Money &recorded) {
        return nonNegative(percentOf(total, ratePlus(rate, rate)) - percentOf(total, rate) - recorded);
    };
    r.socialSecurityEmployer = employerShare(e2d.quarterSocialSecurityWages + r.socialSecurityWages,
                                             fica.socialSecurityRate, e2d.quarterSocialSecurityEmployer);
    r.medicareEmployer = employerShare(e2d.quarterMedicareWages + r.medicareWages, fica.medicareRate,
                                       e2d.quarterMedicareEmployer);
    // Additional Medicare: the part of this check above the threshold for
    // the calendar year; employee only.
    const Money before = nonNegative(pay.ytd.medicareWages);
    const Money after = before + r.medicareWages;
    const Money above = nonNegative(after - maxMoney(before, fica.additionalMedicareThreshold));
    r.additionalMedicare = percentOf(above, fica.additionalMedicareRate);

    // ---- FUTA, on the year's running total ----
    r.futa = nonNegative(percentOf(e2d.yearFutaWages + r.futaWages, futaNetRate(tables, employer.futaState))
                         - e2d.yearFuta);
    noteUnconfirmed(tables, QStringLiteral("federal.futa.creditReduction"), &r.notes);

    // ---- Colorado ----
    if (employee.coloradoWithholding) {
        const ColoradoWithholdingResult co = coloradoWithholding(tables, r.coloradoWages, pay.frequency, employee.w4,
                                                                 employee.dr0004, employee.w4OnFile);
        r.coloradoWorksheet = co.worksheet;
        r.coloradoIncomeTax = co.amount;
        if (employee.w4.form == W4Certificate::Form::Before2020 && employee.w4OnFile
            && !(employee.dr0004.filed && employee.dr0004.annualAllowance)) {
            noteUnconfirmed(tables, QStringLiteral("colorado.withholding.before2020W4"), &r.notes);
        }
    }
    const UiRate uiRate = employer.uiRate.value_or(tables.colorado().uiIntroductoryRate);
    r.coloradoUi = nonNegative(uiPremium(e2d.quarterUiWages + r.uiWages, uiRate) - e2d.quarterUi);
    if (!employer.uiRate) {
        r.notes << QStringLiteral("Colorado UI at the %1 new-employer rate (%2); enter the rate CDLE assigned in "
                                  "Payroll Setup.")
                       .arg(tables.year())
                       .arg(uiRateTotal(uiRate).toPercentString());
        noteUnconfirmed(tables, QStringLiteral("colorado.ui"), &r.notes);
    }
    // FAMLI: the pay stub's gross wages (pre-tax deductions included).
    const FamliPremium famli = famliPremium(tables, r.gross, pay.ytd.famliWages, employer.famliEmployeeShare,
                                            employer.famliEmployerShare);
    if (pay.section125.isPositive()) {
        noteUnconfirmed(tables, QStringLiteral("colorado.famli.wages"), &r.notes);
    }
    r.famliWages = famli.wages;
    r.famliEmployee = famli.employee;
    r.famliEmployer = famli.employer;
    if (!employee.optCity.isEmpty()) {
        if (const OccupationalPrivilegeTax *city = tables.opt(employee.optCity)) {
            const OptCharge opt = occupationalPrivilegeTax(*city, pay.ytd.monthWages, r.gross, pay.ytd.optChargedThisMonth);
            if (!city->status.isEmpty()) {
                r.notes << city->status;
            }
            r.optEmployee = opt.employee;
            r.optEmployer = opt.employer;
            noteUnconfirmed(tables, QStringLiteral("colorado.opt.") + employee.optCity, &r.notes);
        } else {
            r.notes << QStringLiteral("No Occupational Privilege Tax is listed for \"%1\".").arg(employee.optCity);
        }
    }

    // ---- the check ----
    r.employeeTaxes = r.federalIncomeTax + r.socialSecurity + r.medicare + r.additionalMedicare + r.coloradoIncomeTax
        + r.famliEmployee + r.optEmployee;
    r.employerTaxes = r.socialSecurityEmployer + r.medicareEmployer + r.futa + r.coloradoUi + r.famliEmployer
        + r.optEmployer;
    r.netPay = r.gross - r.preTaxDeductions - r.employeeTaxes - r.afterTaxDeductions;
    if (r.netPay.isNegative()) {
        r.error = QStringLiteral("Taxes and deductions (%1) are more than the gross pay (%2).")
                      .arg(usdText(r.preTaxDeductions + r.employeeTaxes + r.afterTaxDeductions),
                           usdText(r.gross));
        return r;
    }
    r.ok = true;
    return r;
}

} // namespace QindaOffice::Books::Payroll
