// SPDX-License-Identifier: GPL-3.0-or-later
// Federal income tax withholding by IRS Publication 15-T (2026),
// Worksheet 1A (federal_withholding.h). Every expected value is worked by
// hand from the 2026 annual tables (Pub 15-T p. 12) in the comment above
// it; the one example the publication prints is reproduced first.
#include "federal_withholding.h"
#include "payroll_tables.h"

#include <QTest>

using namespace QindaOffice::Books;
using namespace QindaOffice::Books::Payroll;

namespace {

Money m(const char *text)
{
    return parseUsd(QString::fromLatin1(text));
}

} // namespace

class TstPayrollFederal : public QObject {
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

    // Pub 15-T (2026) p. 7: a weekly $300 paid to a nonresident alien with
    // a 2019 Form W-4, Single, one allowance; the employer adds $226.90
    // (Table 1) and withholds $31 (wage-bracket method). Through Worksheet
    // 1A with the same $526.90: 1c = 526.90 × 52 = 27,398.80; 1k = 4,300;
    // 1l = 23,098.80; Single row $19,900–$57,900: 2c 1,240.00, 12 %;
    // 2e = 3,198.80; 2f = 383.856 → 383.86; 2g = 1,623.86;
    // 2h = 1,623.86 ÷ 52 = 31.2280… → 31.23 — the IRS's $31 to the dollar.
    void publicationExampleNonresidentAlien()
    {
        W4Certificate w4;
        w4.form = W4Certificate::Form::Before2020;
        w4.legacyStatus = W4LegacyStatus::Single;
        w4.allowances = 1;
        const FederalWithholdingResult r = federalWithholding(t, m("526.90"), PayFrequency::Weekly, w4);
        QCOMPARE(worksheetAmount(r.worksheet, QStringLiteral("1c")), m("27398.80"));
        QCOMPARE(worksheetAmount(r.worksheet, QStringLiteral("1l")), m("23098.80"));
        QCOMPARE(worksheetAmount(r.worksheet, QStringLiteral("2f")), m("383.86"));
        QCOMPARE(r.amount, m("31.23"));
        QCOMPARE(roundToDollar(r.amount), m("31"));
    }

    // 2020+ W-4, Single, biweekly $2,000, nothing else: 1c = 52,000;
    // 1g = 8,600; 1i = 43,400; row $19,900–$57,900: 1,240 + 12 % ×
    // 23,500 = 4,060; 2h = 4,060 ÷ 26 = 156.1538… → 156.15.
    void singleBiweekly()
    {
        const FederalWithholdingResult r = federalWithholding(t, m("2000"), PayFrequency::Biweekly, W4Certificate{});
        QCOMPARE(worksheetAmount(r.worksheet, QStringLiteral("1g")), m("8600"));
        QCOMPARE(worksheetAmount(r.worksheet, QStringLiteral("1i")), m("43400"));
        QCOMPARE(worksheetAmount(r.worksheet, QStringLiteral("2g")), m("4060"));
        QCOMPARE(r.amount, m("156.15"));
    }

    // MFJ, monthly $5,000, Step 3 $2,200 (one child under 17): 1c =
    // 60,000; 1g = 12,900; 1i = 47,100; row $44,100–$120,100: 2,480 +
    // 12 % × 3,000 = 2,840; 2h = 236.666… → 236.67; 3b = 2,200 ÷ 12 =
    // 183.33; 3c = 53.34.
    void marriedMonthlyWithCredit()
    {
        W4Certificate w4;
        w4.status = W4Status::MarriedJointly;
        w4.step3Credits = m("2200");
        const FederalWithholdingResult r = federalWithholding(t, m("5000"), PayFrequency::Monthly, w4);
        QCOMPARE(worksheetAmount(r.worksheet, QStringLiteral("2h")), m("236.67"));
        QCOMPARE(worksheetAmount(r.worksheet, QStringLiteral("3b")), m("183.33"));
        QCOMPARE(r.amount, m("53.34"));
    }

    // Step 2 checked, Single, semimonthly $3,000: 1c = 72,000; 1g = 0;
    // checkbox row $60,900–$108,938: 8,983.00 + 24 % × 11,100 = 11,647;
    // 2h = 11,647 ÷ 24 = 485.2916… → 485.29.
    void step2Checked()
    {
        W4Certificate w4;
        w4.step2Checked = true;
        const FederalWithholdingResult r = federalWithholding(t, m("3000"), PayFrequency::Semimonthly, w4);
        QCOMPARE(worksheetAmount(r.worksheet, QStringLiteral("1g")), m("0"));
        QCOMPARE(worksheetAmount(r.worksheet, QStringLiteral("2b")), m("60900"));
        QCOMPARE(r.amount, m("485.29"));
        // The same pay without the box: 1i = 63,400; standard row
        // $57,900–$113,200: 5,800 + 22 % × 5,500 = 7,010; ÷ 24 = 292.0833 → 292.08.
        QCOMPARE(federalWithholding(t, m("3000"), PayFrequency::Semimonthly, W4Certificate{}).amount, m("292.08"));
    }

    // Head of household, weekly $1,000, 4(a) 5,200, 4(b) 10,400, 4(c)
    // $25: 1c 52,000; 1e 57,200; 1h = 10,400 + 8,600 = 19,000; 1i =
    // 38,200; HoH row $33,250–$83,000: 1,770 + 12 % × 4,950 = 2,364;
    // 2h = 2,364 ÷ 52 = 45.4615… → 45.46; 4b = 45.46 + 25 = 70.46.
    void headOfHouseholdWithStep4()
    {
        W4Certificate w4;
        w4.status = W4Status::HeadOfHousehold;
        w4.step4aOtherIncome = m("5200");
        w4.step4bDeductions = m("10400");
        w4.step4cExtra = m("25");
        const FederalWithholdingResult r = federalWithholding(t, m("1000"), PayFrequency::Weekly, w4);
        QCOMPARE(worksheetAmount(r.worksheet, QStringLiteral("1h")), m("19000"));
        QCOMPARE(worksheetAmount(r.worksheet, QStringLiteral("1i")), m("38200"));
        QCOMPARE(r.amount, m("70.46"));
    }

    // A 2019 Form W-4, Married, 3 allowances, monthly $6,000, line 6 $10:
    // 1c 72,000; 1k = 3 × 4,300 = 12,900; 1l 59,100; Married row
    // $44,100–$120,100: 2,480 + 12 % × 15,000 = 4,280; 2h 356.666… →
    // 356.67; no Step 3; 4b = 366.67.
    void preTwentyTwentyMarried()
    {
        W4Certificate w4;
        w4.form = W4Certificate::Form::Before2020;
        w4.legacyStatus = W4LegacyStatus::Married;
        w4.allowances = 3;
        w4.legacyExtra = m("10");
        w4.step3Credits = m("2000"); // ignored: the old form has no Step 3
        const FederalWithholdingResult r = federalWithholding(t, m("6000"), PayFrequency::Monthly, w4);
        QCOMPARE(worksheetAmount(r.worksheet, QStringLiteral("1k")), m("12900"));
        QCOMPARE(worksheetAmount(r.worksheet, QStringLiteral("3a")), m("0"));
        QCOMPARE(r.amount, m("366.67"));
        // "Married, but withhold at higher Single rate" → the Single row
        // $57,900–$113,200: 5,800 + 22 % × 1,200 = 6,064; ÷ 12 = 505.33; + 10.
        w4.legacyStatus = W4LegacyStatus::MarriedWithholdAtSingleRate;
        QCOMPARE(federalWithholding(t, m("6000"), PayFrequency::Monthly, w4).amount, m("515.33"));
    }

    void exemptZeroAndLowWages()
    {
        W4Certificate exempt;
        exempt.exempt = true;
        QCOMPARE(federalWithholding(t, m("4000"), PayFrequency::Monthly, exempt).amount, m("0"));
        QCOMPARE(federalWithholding(t, m("0"), PayFrequency::Weekly, W4Certificate{}).amount, m("0"));
        // Single weekly $310: 1c = 16,120 − 8,600 = 7,520; row $7,500–
        // $19,900: 10 % × 20 = 2.00; ÷ 52 = 0.0384… → 0.04.
        QCOMPARE(federalWithholding(t, m("310"), PayFrequency::Weekly, W4Certificate{}).amount, m("0.04"));
        // Credits larger than the tax: monthly $4,000 → 1i 39,400 → 1,240 +
        // 12 % × 19,500 = 3,580 → 2h 298.33; 3b = 10,000 ÷ 12 = 833.33;
        // 3c is zero, never negative.
        W4Certificate credits;
        credits.step3Credits = m("10000");
        const FederalWithholdingResult r = federalWithholding(t, m("4000"), PayFrequency::Monthly, credits);
        QCOMPARE(worksheetAmount(r.worksheet, QStringLiteral("2h")), m("298.33"));
        QCOMPARE(r.amount, m("0"));
    }

    // The flat 22 % on supplemental wages, 37 % above $1,000,000 in the
    // year: a $50,000 bonus after $980,000 → 22 % × 20,000 + 37 % × 30,000
    // = 4,400 + 11,100 = 15,500.
    void supplementalFlatRate()
    {
        QCOMPARE(supplementalWithholding(t, m("1000"), m("0")), m("220"));
        QCOMPARE(supplementalWithholding(t, m("50000"), m("980000")), m("15500"));
    }
};

QTEST_GUILESS_MAIN(TstPayrollFederal)
#include "tst_payroll_federal.moc"
