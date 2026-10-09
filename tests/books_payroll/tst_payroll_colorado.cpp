// SPDX-License-Identifier: GPL-3.0-or-later
// Colorado payroll (colorado_payroll.h): DR 1098 (2026) withholding under
// each certificate rule, the UI premium part by part, the FAMLI shares and
// the local Occupational Privilege Taxes. DR 1098 prints no worked example;
// every expected value is worked by hand in the comment above it.
#include "colorado_payroll.h"
#include "payroll_tables.h"

#include <QTest>

using namespace QindaOffice::Books;
using namespace QindaOffice::Books::Payroll;

namespace {

Money m(const char *text)
{
    return parseUsd(QString::fromLatin1(text));
}

Decimal d(const char *text)
{
    return *Decimal::parse(QString::fromLatin1(text));
}

W4Certificate w4(W4Status status, bool exempt = false)
{
    W4Certificate w;
    w.status = status;
    w.exempt = exempt;
    return w;
}

} // namespace

class TstPayrollColorado : public QObject {
    Q_OBJECT
    PayrollTables t;

private slots:
    void initTestCase()
    {
        QString error;
        const std::optional<PayrollTables> tables = PayrollTables::forYear(2026, &error);
        QVERIFY2(tables.has_value(), qPrintable(error));
        t = *tables;
    }

    // A W-4 only, single: 1c = 2,000 × 26 = 52,000; 2a = 5,500; 2b =
    // 46,500; 2c = × 4.40 % = 2,046.00; 2d = ÷ 26 = 78.6923… → 78.69;
    // 2f 78.69, withheld in whole dollars → $79.
    void w4OnlySingleBiweekly()
    {
        const ColoradoWithholdingResult r =
            coloradoWithholding(t, m("2000"), PayFrequency::Biweekly, w4(W4Status::SingleOrMarriedSeparately), {});
        QCOMPARE(worksheetAmount(r.worksheet, QStringLiteral("2a")), m("5500"));
        QCOMPARE(worksheetAmount(r.worksheet, QStringLiteral("2c")), m("2046"));
        QCOMPARE(worksheetAmount(r.worksheet, QStringLiteral("2d")), m("78.69"));
        QCOMPARE(worksheetAmount(r.worksheet, QStringLiteral("2f")), m("78.69"));
        QCOMPARE(r.amount, m("79"));
    }

    // Married filing jointly, monthly $5,000: 1c 60,000 − 11,000 = 49,000
    // × 4.40 % = 2,156.00 ÷ 12 = 179.666… → 179.67 → $180. Head of
    // household takes the "otherwise" amount: 60,000 − 5,500 = 54,500 ×
    // 4.40 % = 2,398 ÷ 12 = 199.83 → $200.
    void w4MarriedAndHeadOfHousehold()
    {
        QCOMPARE(coloradoWithholding(t, m("5000"), PayFrequency::Monthly, w4(W4Status::MarriedJointly), {}).amount, m("180"));
        QCOMPARE(coloradoWithholding(t, m("5000"), PayFrequency::Monthly, w4(W4Status::HeadOfHousehold), {}).amount, m("200"));
    }

    // DR 0004 Line 2 $14,000 and Line 3 $10, semimonthly $1,500: 1c 36,000
    // − 14,000 = 22,000 × 4.40 % = 968.00 ÷ 24 = 40.333… → 40.33; 2e 10;
    // 2f 50.33 → $50. Line 2 of zero counts: monthly $1,000 → 12,000 ×
    // 4.40 % = 528 ÷ 12 = 44.00. A blank Line 2 falls back to the W-4.
    void dr0004Rules()
    {
        ColoradoCertificate dr;
        dr.filed = true;
        dr.annualAllowance = m("14000");
        dr.additionalPerPeriod = m("10");
        const ColoradoWithholdingResult r =
            coloradoWithholding(t, m("1500"), PayFrequency::Semimonthly, w4(W4Status::SingleOrMarriedSeparately), dr);
        QCOMPARE(worksheetAmount(r.worksheet, QStringLiteral("2d")), m("40.33"));
        QCOMPARE(worksheetAmount(r.worksheet, QStringLiteral("2e")), m("10"));
        QCOMPARE(r.amount, m("50"));
        dr.annualAllowance = m("0");
        dr.additionalPerPeriod = m("0");
        QCOMPARE(coloradoWithholding(t, m("1000"), PayFrequency::Monthly, w4(W4Status::MarriedJointly), dr).amount, m("44"));
        dr.annualAllowance.reset();
        const ColoradoWithholdingResult blank =
            coloradoWithholding(t, m("5000"), PayFrequency::Monthly, w4(W4Status::MarriedJointly), dr);
        QCOMPARE(worksheetAmount(blank.worksheet, QStringLiteral("2a")), m("11000"));
        QCOMPARE(blank.amount, m("180"));
    }

    // Rule 3: a W-4 claiming exemption and no DR 0004 → nothing. With a
    // DR 0004 the DR 0004 decides (Rule 1). Rule 4: no certificate at all
    // → as if single.
    void exemptAndNoCertificate()
    {
        const W4Certificate exempt = w4(W4Status::SingleOrMarriedSeparately, true);
        QCOMPARE(coloradoWithholding(t, m("5000"), PayFrequency::Monthly, exempt, {}).amount, m("0"));
        ColoradoCertificate dr;
        dr.filed = true;
        dr.annualAllowance = m("14000");
        // 60,000 − 14,000 = 46,000 × 4.40 % = 2,024 ÷ 12 = 168.67 → $169.
        QCOMPARE(coloradoWithholding(t, m("5000"), PayFrequency::Monthly, exempt, dr).amount, m("169"));
        // No W-4 on file (a married employee who gave nothing) → single.
        const ColoradoWithholdingResult none =
            coloradoWithholding(t, m("5000"), PayFrequency::Monthly, w4(W4Status::MarriedJointly), {}, false);
        QCOMPARE(worksheetAmount(none.worksheet, QStringLiteral("2a")), m("5500"));
    }

    // A 2019 W-4 "Married" takes the married allowance — QindaBooks'
    // mapping, listed as needing confirmation in the tables.
    void legacyW4Mapping()
    {
        W4Certificate old;
        old.form = W4Certificate::Form::Before2020;
        old.legacyStatus = W4LegacyStatus::Married;
        QCOMPARE(worksheetAmount(coloradoWithholding(t, m("5000"), PayFrequency::Monthly, old, {}).worksheet,
                                 QStringLiteral("2a")),
                 m("11000"));
        old.legacyStatus = W4LegacyStatus::MarriedWithholdAtSingleRate;
        QCOMPARE(worksheetAmount(coloradoWithholding(t, m("5000"), PayFrequency::Monthly, old, {}).worksheet,
                                 QStringLiteral("2a")),
                 m("5500"));
        QVERIFY(!t.isConfirmed(QStringLiteral("colorado.withholding.before2020W4")));
    }

    // The 2026 introductory UI rate, part by part, each rounded (CDLE):
    // 3,000.00 → 45.90 + 5.10 + 40.50 = 91.50 (3.05 %). 1,234.56 → 18.89
    // (18.888768) + 2.10 (2.098752) + 16.67 (16.66656) = 37.66, a cent more
    // than 3.05 % on the whole (37.65408 → 37.65).
    void uiPartByPart()
    {
        const UiRate rate = t.colorado().uiIntroductoryRate;
        QCOMPARE(uiRateTotal(rate), d("3.05%"));
        QCOMPARE(uiPremium(m("3000"), rate), m("91.50"));
        QCOMPARE(uiPremium(m("1234.56"), rate), m("37.66"));
        // The $30,600 chargeable wage base.
        QCOMPARE(wagesUnderBase(m("2000"), m("30000"), t.colorado().uiWageBase), m("600"));
        QCOMPARE(wagesUnderBase(m("2000"), m("31000"), t.colorado().uiWageBase), m("0"));
    }

    // FAMLI 0.88 %: 3,000 → premium 26.40, employee 0.44 % = 13.20,
    // employer 13.20. 1,001.25 → premium 8.81 (8.811), employee 4.41
    // (4.4055), employer 8.81 − 4.41 = 4.40: the shares always add up. A
    // small employer paying no share remits only the employee's 0.44 %.
    // The employee's share never passes 50 %. Wages stop at $184,500.
    void famliShares()
    {
        const FamliPremium even = famliPremium(t, m("3000"), m("0"), d("50%"), d("50%"));
        QCOMPARE(even.premium, m("26.40"));
        QCOMPARE(even.employee, m("13.20"));
        QCOMPARE(even.employer, m("13.20"));
        const FamliPremium odd = famliPremium(t, m("1001.25"), m("0"), d("50%"), d("50%"));
        QCOMPARE(odd.premium, m("8.81"));
        QCOMPARE(odd.employee, m("4.41"));
        QCOMPARE(odd.employer, m("4.40"));
        const FamliPremium small = famliPremium(t, m("3000"), m("0"), d("50%"), d("0%"));
        QCOMPARE(small.employee, m("13.20"));
        QCOMPARE(small.employer, m("0"));
        QCOMPARE(small.premium, m("13.20"));
        QCOMPARE(famliPremium(t, m("3000"), m("0"), d("60%"), d("40%")).employee, m("13.20"));
        QCOMPARE(famliPremium(t, m("1000"), m("184000"), d("50%"), d("50%")).wages, m("500"));
    }

    // Occupational Privilege Taxes, once per month at each city's
    // threshold as the city words it.
    void occupationalPrivilegeTaxes()
    {
        const OccupationalPrivilegeTax *denver = t.opt(QStringLiteral("denver"));
        QVERIFY(denver != nullptr);
        QCOMPARE(occupationalPrivilegeTax(*denver, m("0"), m("499.99"), false).employee, m("0"));
        const OptCharge reached = occupationalPrivilegeTax(*denver, m("300"), m("200"), false); // at least $500
        QCOMPARE(reached.employee, m("5.75"));
        QCOMPARE(reached.employer, m("4.00"));
        QCOMPARE(occupationalPrivilegeTax(*denver, m("600"), m("600"), true).employee, m("0")); // once a month
        const OccupationalPrivilegeTax *sheridan = t.opt(QStringLiteral("sheridan"));
        QCOMPARE(occupationalPrivilegeTax(*sheridan, m("0"), m("500"), false).employee, m("0")); // more than $500
        QCOMPARE(occupationalPrivilegeTax(*sheridan, m("0"), m("500.01"), false).employee, m("3.00"));
        const OccupationalPrivilegeTax *glendale = t.opt(QStringLiteral("glendale"));
        QCOMPARE(occupationalPrivilegeTax(*glendale, m("0"), m("750"), false).employee, m("5.00"));
        const OccupationalPrivilegeTax *gv = t.opt(QStringLiteral("greenwoodVillage"));
        QCOMPARE(occupationalPrivilegeTax(*gv, m("0"), m("250"), false).employer, m("2.00"));
        const OccupationalPrivilegeTax *aurora = t.opt(QStringLiteral("aurora"));
        QVERIFY(aurora != nullptr && !aurora->status.isEmpty()); // repealed in 2025
        QCOMPARE(occupationalPrivilegeTax(*aurora, m("0"), m("5000"), false).employee, m("0"));
    }

    void exactRateArithmetic()
    {
        QCOMPARE(rateMinus(d("6.0%"), d("5.4%")), d("0.6%"));
        QCOMPARE(ratePlus(d("1.53%"), d("0.17%")), d("1.7%"));
        QCOMPARE(rateTimes(d("0.88%"), d("50%")), d("0.44%"));
    }
};

QTEST_GUILESS_MAIN(TstPayrollColorado)
#include "tst_payroll_colorado.moc"
