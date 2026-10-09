// SPDX-License-Identifier: GPL-3.0-or-later
// Whole paychecks (paycheck_calculator.h): which wages each tax is on, the
// wage-base crossings in the middle of a check, the Additional Medicare
// threshold, the FUTA $7,000, pre-tax deductions, a 2 % shareholder's
// health premiums, a bonus, and the refusals. Every expected value is
// worked by hand in the comment above it from the 2026 tables.
#include "paycheck_calculator.h"
#include "payroll_tables.h"

#include <QTest>

using namespace QindaOffice::Books;
using namespace QindaOffice::Books::Payroll;

namespace {

Money m(const char *text)
{
    return parseUsd(QString::fromLatin1(text));
}

PayInput pay(const char *regular, PayFrequency frequency = PayFrequency::Biweekly)
{
    PayInput p;
    p.payDate = QDate(2026, 10, 9);
    p.frequency = frequency;
    p.regular = m(regular);
    return p;
}

} // namespace

class TstPayrollPaycheck : public QObject {
    Q_OBJECT
    PayrollTables t;
    EmployerSettings employer;   // Colorado: introductory UI rate, FAMLI 50/50
    EmployeeTaxSetup single;     // 2026 W-4 single, no DR 0004, Colorado

private slots:
    void initTestCase()
    {
        QString error;
        const std::optional<PayrollTables> tables = PayrollTables::forYear(2026, &error);
        QVERIFY2(tables.has_value(), qPrintable(error));
        t = *tables;
        employer.colorado = true;
        employer.futaState = QStringLiteral("CO");
    }

    // Biweekly $3,000, working in Denver:
    //  FIT: 1c 78,000; 1i 69,400; 5,800 + 22 % × 11,500 = 8,330 ÷ 26 = 320.38.
    //  SS 6.2 % = 186.00; Medicare 1.45 % = 43.50 (each side).
    //  Colorado: 78,000 − 5,500 = 72,500 × 4.40 % = 3,190 ÷ 26 = 122.69 → $123.
    //  FAMLI 0.44 % each = 13.20; Denver OPT 5.75 / 4.00.
    //  FUTA 0.6 % = 18.00; UI 45.90 + 5.10 + 40.50 = 91.50.
    //  Employee 320.38 + 186 + 43.50 + 123 + 13.20 + 5.75 = 691.83 → net 2,308.17.
    //  Employer 186 + 43.50 + 18 + 91.50 + 13.20 + 4 = 356.20.
    void ordinaryBiweeklyCheck()
    {
        EmployeeTaxSetup denver = single;
        denver.optCity = QStringLiteral("denver");
        const PaycheckTaxes r = computePaycheck(t, employer, denver, pay("3000"));
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.federalIncomeTax, m("320.38"));
        QCOMPARE(r.socialSecurity, m("186"));
        QCOMPARE(r.medicare, m("43.50"));
        QCOMPARE(r.coloradoIncomeTax, m("123"));
        QCOMPARE(r.famliEmployee, m("13.20"));
        QCOMPARE(r.optEmployee, m("5.75"));
        QCOMPARE(r.futa, m("18"));
        QCOMPARE(r.coloradoUi, m("91.50"));
        QCOMPARE(r.employeeTaxes, m("691.83"));
        QCOMPARE(r.netPay, m("2308.17"));
        QCOMPARE(r.employerTaxes, m("356.20"));
        QVERIFY(r.notes.join(QLatin1Char(' ')).contains(QStringLiteral("new-employer rate")));
    }

    // Monthly $4,000 with $200 cafeteria-plan health and $300 to a 401(k):
    //  income tax wages 3,500: 1c 42,000 − 8,600 = 33,400 → 1,240 + 12 % ×
    //  13,500 = 2,860 ÷ 12 = 238.33; Colorado 42,000 − 5,500 = 36,500 ×
    //  4.40 % = 1,606 ÷ 12 = 133.83 → $134.
    //  SS/Medicare/FUTA/UI wages 3,800 (the 401(k) stays in them): 235.60,
    //  55.10, 22.80, 58.14 + 6.46 + 51.30 = 115.90. FAMLI on the gross
    //  4,000 (its "gross wages"): 17.60.
    //  Net 4,000 − 500 − (238.33 + 235.60 + 55.10 + 134 + 17.60) = 2,819.37.
    void preTaxDeductions()
    {
        PayInput p = pay("4000", PayFrequency::Monthly);
        p.section125 = m("200");
        p.retirement = m("300");
        const PaycheckTaxes r = computePaycheck(t, employer, single, p);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.federalWages, m("3500"));
        QCOMPARE(r.socialSecurityWages, m("3800"));
        QCOMPARE(r.federalIncomeTax, m("238.33"));
        QCOMPARE(r.coloradoIncomeTax, m("134"));
        QCOMPARE(r.socialSecurity, m("235.60"));
        QCOMPARE(r.medicare, m("55.10"));
        QCOMPARE(r.futa, m("22.80"));
        QCOMPARE(r.coloradoUi, m("115.90"));
        QCOMPARE(r.famliWages, m("4000"));
        QCOMPARE(r.famliEmployee, m("17.60"));
        QCOMPARE(r.netPay, m("2819.37"));
        QVERIFY(r.notes.join(QLatin1Char(' ')).contains(QStringLiteral("FAMLI premium on pre-tax deductions")));
    }

    // The social security wage base crossed in the middle of a check:
    // $180,000 earlier, $10,000 now → only 4,500 is under $184,500 → 279.00;
    // Medicare on all 10,000 = 145.00; still under $200,000 → no Additional
    // Medicare; FAMLI wages stop at the same base → 4,500 × 0.44 % = 19.80.
    void socialSecurityBaseCrossedMidCheck()
    {
        PayInput p = pay("10000", PayFrequency::Monthly);
        p.ytd.socialSecurityWages = m("180000");
        p.ytd.medicareWages = m("180000");
        p.ytd.famliWages = m("180000");
        p.ytd.futaWages = m("7000");
        p.ytd.uiWages = m("30600");
        const PaycheckTaxes r = computePaycheck(t, employer, single, p);
        QCOMPARE(r.socialSecurityWages, m("4500"));
        QCOMPARE(r.socialSecurity, m("279"));
        QCOMPARE(r.socialSecurityEmployer, m("279"));
        QCOMPARE(r.medicare, m("145"));
        QCOMPARE(r.additionalMedicare, m("0"));
        QCOMPARE(r.famliEmployee, m("19.80"));
        QCOMPARE(r.futa, m("0"));
        QCOMPARE(r.coloradoUi, m("0"));
    }

    // Additional Medicare from the check that crosses $200,000: $195,000
    // earlier, $10,000 now → 0.9 % × 5,000 = 45.00, employee only. Past the
    // social security base: no social security at all.
    void additionalMedicareCrossed()
    {
        PayInput p = pay("10000", PayFrequency::Monthly);
        p.ytd.socialSecurityWages = m("195000");
        p.ytd.medicareWages = m("195000");
        const PaycheckTaxes r = computePaycheck(t, employer, single, p);
        QCOMPARE(r.additionalMedicare, m("45"));
        QCOMPARE(r.socialSecurity, m("0"));
        QCOMPARE(r.medicareEmployer, m("145")); // no employer share of the 0.9 %
        // Every later check: all of it. 210,000 earlier, 5,000 → 45.00.
        p.regular = m("5000");
        p.ytd.medicareWages = m("210000");
        QCOMPARE(computePaycheck(t, employer, single, p).additionalMedicare, m("45"));
    }

    // FUTA stops at $7,000: $6,000 earlier, $3,000 now → 1,000 × 0.6 % = 6.00.
    // Colorado UI at $30,600: $29,000 earlier → 1,600 chargeable →
    // 24.48 + 2.72 + 21.60 = 48.80.
    void futaAndUiLimits()
    {
        PayInput p = pay("3000");
        p.ytd.futaWages = m("6000");
        p.ytd.uiWages = m("29000");
        const PaycheckTaxes r = computePaycheck(t, employer, single, p);
        QCOMPARE(r.futaWages, m("1000"));
        QCOMPARE(r.futa, m("6"));
        QCOMPARE(r.uiWages, m("1600"));
        QCOMPARE(r.coloradoUi, m("48.80"));
    }

    // A 2 % shareholder's $500 health premiums with monthly $5,000:
    // income tax wages 5,500: 1c 66,000 − 8,600 = 57,400 → 1,240 + 12 % ×
    // 37,500 = 5,740 ÷ 12 = 478.33; Colorado 66,000 − 5,500 = 60,500 ×
    // 4.40 % = 2,662 ÷ 12 = 221.83 → $222. Social security on 5,000 only.
    // The premiums are not cash: net = 5,000 − (478.33 + 310 + 72.50 + 222
    // + 22.00) = 3,895.17.
    void shareholderHealth()
    {
        PayInput p = pay("5000", PayFrequency::Monthly);
        p.shareholderHealth = m("500");
        const PaycheckTaxes r = computePaycheck(t, employer, single, p);
        QCOMPARE(r.federalWages, m("5500"));
        QCOMPARE(r.coloradoWages, m("5500"));
        QCOMPARE(r.socialSecurityWages, m("5000"));
        QCOMPARE(r.federalIncomeTax, m("478.33"));
        QCOMPARE(r.coloradoIncomeTax, m("222"));
        QCOMPARE(r.netPay, m("3895.17"));
    }

    // A $1,000 bonus with biweekly $2,000: Worksheet 1A on the $2,000
    // (156.15) plus the flat 22 % on the bonus (220) = 376.15. Colorado has
    // no flat rate: 3,000 through DR 1098 → $123.
    void bonusAtTheFlatRate()
    {
        PayInput p = pay("2000");
        p.supplemental = m("1000");
        const PaycheckTaxes r = computePaycheck(t, employer, single, p);
        QCOMPARE(r.gross, m("3000"));
        QCOMPARE(r.federalIncomeTax, m("376.15"));
        QCOMPARE(r.coloradoIncomeTax, m("123"));
        QVERIFY(r.notes.join(QLatin1Char(' ')).contains(QStringLiteral("22%")));
    }

    // The employer's share on the running total (paycheck_calculator.h):
    // two checks of 1,234.57. The first: employee 6.2 % = 76.54 (76.54334);
    // employer = 12.4 % of 1,234.57 (153.09) − 6.2 % of it (76.54) = 76.55.
    // The second, the quarter's total 2,469.14: 12.4 % = 306.17, 6.2 % =
    // 153.09 → the employer's 153.08 so far − 76.55 = 76.53. The employer's
    // 153.08 + the employees' 153.08 = 306.16 = Form 941 line 5a (306.17)
    // + line 7 (−0.01). FUTA likewise: 0.6 % of 2,469.14 = 14.81 − 7.41.
    void employerSharesOnRunningTotals()
    {
        PayInput first = pay("1234.57");
        const PaycheckTaxes a = computePaycheck(t, employer, single, first);
        QCOMPARE(a.socialSecurity, m("76.54"));
        QCOMPARE(a.socialSecurityEmployer, m("76.55"));
        QCOMPARE(a.futa, m("7.41"));
        PayInput second = pay("1234.57");
        second.employerToDate.quarterSocialSecurityWages = m("1234.57");
        second.employerToDate.quarterSocialSecurityEmployer = a.socialSecurityEmployer;
        second.employerToDate.quarterMedicareWages = m("1234.57");
        second.employerToDate.quarterMedicareEmployer = a.medicareEmployer;
        second.employerToDate.yearFutaWages = m("1234.57");
        second.employerToDate.yearFuta = a.futa;
        second.employerToDate.quarterUiWages = m("1234.57");
        second.employerToDate.quarterUi = a.coloradoUi;
        const PaycheckTaxes b = computePaycheck(t, employer, single, second);
        QCOMPARE(b.socialSecurity, m("76.54"));
        QCOMPARE(b.socialSecurityEmployer, m("76.53"));
        QCOMPARE(a.socialSecurityEmployer + b.socialSecurityEmployer, m("153.08"));
        QCOMPARE(b.futa, m("7.40"));
        // Medicare: 2.9 % of 2,469.14 = 71.61 (71.60506); 1.45 % = 35.80
        // (35.80253); the employer's 35.81 overall. UI: 3.05 % by part on
        // 2,469.14 = 37.78 + 4.20 + 33.33 = 75.31 overall.
        QCOMPARE(a.medicareEmployer + b.medicareEmployer, m("35.81"));
        QCOMPARE(a.coloradoUi + b.coloradoUi, m("75.31"));
    }

    // An employer outside Colorado: the same $3,000 check has no Colorado
    // tax, UI, FAMLI or city tax — federal only: 3,000 − 320.38 − 186.00 −
    // 43.50 = 2,450.12 net; employer 186.00 + 43.50 + 18.00 = 247.50.
    void outsideColorado()
    {
        EmployerSettings elsewhere = employer;
        elsewhere.colorado = false;
        elsewhere.futaState = QStringLiteral("TX");
        EmployeeTaxSetup denver = single;
        denver.optCity = QStringLiteral("denver");
        const PaycheckTaxes r = computePaycheck(t, elsewhere, denver, pay("3000"));
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.coloradoWages, m("0"));
        QCOMPARE(r.uiWages, m("0"));
        QCOMPARE(r.coloradoIncomeTax, m("0"));
        QCOMPARE(r.coloradoUi, m("0"));
        QCOMPARE(r.famliEmployee + r.famliEmployer, m("0"));
        QCOMPARE(r.optEmployee + r.optEmployer, m("0"));
        QVERIFY(r.coloradoWorksheet.isEmpty());
        QCOMPARE(r.netPay, m("2450.12"));
        QCOMPARE(r.employerTaxes, m("247.50"));
        QVERIFY(!r.notes.join(QLatin1Char(' ')).contains(QStringLiteral("Colorado")));
    }

    // The company's own items join the check (outside Colorado): an
    // employer unemployment tax 2.7 % up to $10,000 with $9,000 taxed → 27.00;
    // an employee disability tax 1 % up to $5,000 with $4,000 taxed → 10.00;
    // user-entered state withholding 4 % of the $3,000 + $5.00 → 125.00.
    // Net 2,450.12 − 10.00 − 125.00 = 2,315.12; employer 247.50 + 27.00 =
    // 274.50.
    void customItemsJoinTheCheck()
    {
        EmployerSettings elsewhere = employer;
        elsewhere.colorado = false;
        PayInput p = pay("3000");
        CustomItemInput ui;
        ui.item = CustomItem{1, QStringLiteral("Example state unemployment"), CustomItem::Side::Employer,
                             CustomItem::Kind::Tax, *Decimal::parse(QStringLiteral("0.027")), m("10000"), std::nullopt};
        ui.ytdWages = m("9000");
        CustomItemInput sdi;
        sdi.item = CustomItem{2, QStringLiteral("Example state disability"), CustomItem::Side::Employee,
                              CustomItem::Kind::Tax, *Decimal::parse(QStringLiteral("0.01")), m("5000"), std::nullopt};
        sdi.ytdWages = m("4000");
        CustomItemInput sit;
        sit.item = CustomItem{3, QStringLiteral("Example state income tax"), CustomItem::Side::Employee,
                              CustomItem::Kind::Withholding, Decimal(), std::nullopt, std::nullopt};
        sit.employeeRate = *Decimal::parse(QStringLiteral("0.04"));
        sit.employeeExtra = m("5");
        p.custom = {ui, sdi, sit};
        const PaycheckTaxes r = computePaycheck(t, elsewhere, single, p);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.custom.size(), 3);
        QCOMPARE(r.custom.at(0).amount, m("27"));
        QCOMPARE(r.custom.at(1).amount, m("10"));
        QCOMPARE(r.custom.at(2).amount, m("125"));
        QCOMPARE(r.customEmployee, m("135"));
        QCOMPARE(r.customEmployer, m("27"));
        QCOMPARE(r.netPay, m("2315.12"));
        QCOMPARE(r.employerTaxes, m("274.50"));
    }

    // Pub 15-T lets an employer round withholding to the dollar when done
    // consistently: 156.15 → $156.
    void roundingOption()
    {
        EmployerSettings rounding = employer;
        rounding.roundFederalWithholding = true;
        QCOMPARE(computePaycheck(t, rounding, single, pay("2000")).federalIncomeTax, m("156"));
    }

    void refusals()
    {
        PayInput p = pay("100");
        p.afterTax = m("200");
        const PaycheckTaxes over = computePaycheck(t, employer, single, p);
        QVERIFY(!over.ok);
        QVERIFY(over.error.contains(QStringLiteral("more than the gross")));
        PayInput nextYear = pay("2000");
        nextYear.payDate = QDate(2027, 1, 8);
        const PaycheckTaxes refused = computePaycheck(t, employer, single, nextYear);
        QVERIFY(!refused.ok);
        QVERIFY(refused.error.contains(QStringLiteral("2027")));
        PayInput deferrals = pay("2000");
        deferrals.retirement = m("1000");
        deferrals.ytd.retirement = m("24000");
        QVERIFY(computePaycheck(t, employer, single, deferrals).notes.join(QLatin1Char(' ')).contains(QStringLiteral("$24,500.00")));
    }
};

QTEST_GUILESS_MAIN(TstPayrollPaycheck)
#include "tst_payroll_paycheck.moc"
