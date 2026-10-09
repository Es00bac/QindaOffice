// SPDX-License-Identifier: GPL-3.0-or-later
// The estimated-tax planner (estimated_tax.h) against Form 1040-ES (2026),
// Publication 505 (2026) — its safe-harbor and amended-installment
// examples first — and DR 0104EP (2026). Every other expected value is
// worked by hand in the comment above it.
#include "estimated_tax.h"
#include "payroll_tables.h"

#include <QTest>

using namespace QindaOffice::Books;
using namespace QindaOffice::Books::Payroll;

namespace {

Money m(const char *text)
{
    return parseUsd(QString::fromLatin1(text));
}

EstimatedPayment pay(EstimatedPayment::Jurisdiction j, const QDate &date, const char *amount)
{
    return EstimatedPayment{j, date, m(amount)};
}

} // namespace

class TstPayrollEstimates : public QObject {
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

    // Pub 505 (2026) ch. 2: 2025 tax $42,581, expected 2026 tax $71,253,
    // 2025 AGI $180,000 → 90 % × 71,253 = $64,128; 110 % × 42,581 =
    // $46,839 (the AGI is over $150,000); the required annual payment is
    // $46,839, the smaller. (Cents: 64,127.70 and 46,839.10.)
    void publicationSafeHarborExample()
    {
        QString basis;
        const Money required = federalRequiredAnnualPayment(t, FilingStatus::Single, m("71253"), m("42581"),
                                                            m("180000"), true, &basis);
        QCOMPARE(required, m("46839.10"));
        QCOMPARE(roundToDollar(required), m("46839"));
        QCOMPARE(basis, QStringLiteral("110% of 2025 tax"));
        // AGI $150,000 or less: 100 % of last year.
        QCOMPARE(federalRequiredAnnualPayment(t, FilingStatus::Single, m("71253"), m("42581"), m("150000"), true),
                 m("42581"));
        // Married filing separately: the line is $75,000.
        QCOMPARE(federalRequiredAnnualPayment(t, FilingStatus::MarriedSeparately, m("71253"), m("42581"), m("80000"), true),
                 m("46839.10"));
        // 90 % of this year when that is smaller: 90 % × 40,000 = 36,000.
        QCOMPARE(federalRequiredAnnualPayment(t, FilingStatus::Single, m("40000"), m("42581"), m("90000"), true, &basis),
                 m("36000"));
        QCOMPARE(basis, QStringLiteral("90% of 2026 tax"));
        // No full prior year: only the 90 % rule.
        QCOMPARE(federalRequiredAnnualPayment(t, FilingStatus::Single, m("71253"), m("42581"), m("90000"), false),
                 m("64127.70"));
    }

    // Pub 505 (2026) ch. 2: estimated tax $1,800, $450 paid April 15 and
    // June 15; refigured to $4,100 in July → "The required estimated tax
    // payment for the third payment period is $2,175 … the fourth … $1,025."
    void publicationAmendedInstallmentExample()
    {
        const QList<EstimatedPayment> paid{pay(EstimatedPayment::Jurisdiction::Federal, QDate(2026, 4, 15), "450"),
                                           pay(EstimatedPayment::Jurisdiction::Federal, QDate(2026, 6, 15), "450")};
        Money total;
        const QList<Installment> in = scheduleInstallments(m("4100"), true, t.estimatedTax().dueDates, paid,
                                                           EstimatedPayment::Jurisdiction::Federal, QDate(2026, 7, 10), &total);
        QCOMPARE(total, m("900"));
        QCOMPARE(amountDueBy(in, QDate(2026, 9, 15)), m("2175"));
        QCOMPARE(in.at(3).owed, m("1025"));
        QCOMPARE(in.at(0).owed, m("125")); // paid short: it carries
        QCOMPARE(in.at(0).state, Installment::State::Overdue);
        QCOMPARE(in.at(2).state, Installment::State::Upcoming);
    }

    // Single, $80,000 profit for 2026, nothing else.
    //  SE: 3 = 80,000 × 92.35 % = 73,880.00; 4 = × 2.9 % = 2,142.52;
    //      9 = 73,880 × 12.4 % = 9,161.12; 10 = 11,303.64; 11 = 5,651.82.
    //  1 AGI = 80,000 − 5,651.82 = 74,348.18; 2a 16,100.
    //  QBI: 20 % × (80,000 − 5,651.82) = 14,869.64, limited to 20 % ×
    //      (74,348.18 − 16,100) = 11,649.64 → 2b 11,649.64.
    //  3 = 74,348.18 − 27,749.64 = 46,598.54; 4 = 1,240 + 12 % ×
    //      34,198.54 = 5,343.82; 11c = 5,343.82 + 11,303.64 = 16,647.46.
    //  12a = 14,982.71 (no prior year given) → four of 3,745.68/.68/.68/.67.
    //  Colorado: 46,599 × 4.4 % = 2,050.36 → $2,050; 70 % = $1,435 →
    //      $359, $359, $359, $358.
    //  Set aside: (16,647.46 + 2,050) ÷ 80,000 = 23.37 % → 24 %.
    void scheduleCOwner()
    {
        EstimatedTaxInput in;
        in.asOf = QDate(2026, 10, 8);
        in.projectedProfit = m("80000");
        const EstimatedTaxResult r = planEstimatedTax(t, in);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(worksheetAmount(r.seWorksheet, QStringLiteral("3")), m("73880"));
        QCOMPARE(worksheetAmount(r.seWorksheet, QStringLiteral("4")), m("2142.52"));
        QCOMPARE(worksheetAmount(r.seWorksheet, QStringLiteral("9")), m("9161.12"));
        QCOMPARE(r.seTax, m("11303.64"));
        QCOMPARE(r.seDeduction, m("5651.82"));
        QCOMPARE(r.qbiDeduction, m("11649.64"));
        QCOMPARE(r.taxableIncome, m("46598.54"));
        QCOMPARE(r.incomeTax, m("5343.82"));
        QCOMPARE(r.federal.totalTax, m("16647.46"));
        QCOMPARE(r.federal.requiredAnnualPayment, m("14982.71"));
        QVERIFY(r.federal.paymentsRequired);
        QCOMPARE(r.federal.installments.size(), 4);
        QCOMPARE(r.federal.installments.at(0).amount, m("3745.68"));
        QCOMPARE(r.federal.installments.at(3).amount, m("3745.67"));
        QCOMPARE(r.federal.installments.at(3).due, QDate(2027, 1, 15));
        // Asked on Oct 8 with nothing paid: Q1–Q3 overdue, Q4 upcoming.
        QCOMPARE(r.federal.installments.at(2).state, Installment::State::Overdue);
        QCOMPARE(r.federal.installments.at(3).state, Installment::State::Upcoming);
        QCOMPARE(r.coloradoTax, m("2050"));
        QCOMPARE(r.colorado.requiredAnnualPayment, m("1435"));
        QCOMPARE(r.colorado.installments.at(0).amount, m("359"));
        QCOMPARE(r.colorado.installments.at(3).amount, m("358"));
        QCOMPARE(r.setAsidePercent, 24);
        QCOMPARE(setAsideFor(m("2500"), r.setAsidePercent), m("600"));
    }

    // The same $80,000 owner of a business outside Colorado: no Colorado
    // plan; the set-aside is the federal tax alone, 16,647.46 ÷ 80,000 =
    // 20.81 % → 21 %.
    void outsideColoradoFederalOnly()
    {
        EstimatedTaxInput in;
        in.colorado = false;
        in.asOf = QDate(2026, 10, 8);
        in.projectedProfit = m("80000");
        const EstimatedTaxResult r = planEstimatedTax(t, in);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.federal.totalTax, m("16647.46"));
        QCOMPARE(r.coloradoTax, m("0"));
        QVERIFY(!r.colorado.paymentsRequired);
        QVERIFY(r.colorado.installments.isEmpty());
        QCOMPARE(r.businessTax, m("16647.46"));
        QCOMPARE(r.setAsidePercent, 21);
        QVERIFY(!r.notes.join(QLatin1Char(' ')).contains(QStringLiteral("Colorado")));
    }

    // The same owner with last year's returns: federal 2025 tax $10,000
    // (AGI $60,000) → 12b = 10,000 < 12a → four of $2,500; two paid.
    // Colorado 2025 net tax $1,200 → 100 % = $1,200 < $1,435 → $300 each.
    void priorYearSafeHarborAndPayments()
    {
        EstimatedTaxInput in;
        in.asOf = QDate(2026, 10, 8);
        in.projectedProfit = m("80000");
        in.priorYearTax = m("10000");
        in.priorYearAgi = m("60000");
        in.priorYearColoradoTax = m("1200");
        in.payments = {pay(EstimatedPayment::Jurisdiction::Federal, QDate(2026, 4, 14), "2500"),
                       pay(EstimatedPayment::Jurisdiction::Federal, QDate(2026, 6, 20), "2500"),
                       pay(EstimatedPayment::Jurisdiction::Colorado, QDate(2026, 4, 15), "300")};
        const EstimatedTaxResult r = planEstimatedTax(t, in);
        QCOMPARE(r.federal.requiredAnnualPayment, m("10000"));
        QCOMPARE(r.federal.safeHarborBasis, QStringLiteral("100% of 2025 tax"));
        QCOMPARE(r.federal.installments.at(0).state, Installment::State::Paid);
        QCOMPARE(r.federal.installments.at(1).state, Installment::State::PaidLate); // June 20 > June 15
        QCOMPARE(r.federal.installments.at(2).state, Installment::State::Overdue);
        QCOMPARE(r.federal.remaining, m("5000"));
        QCOMPARE(r.colorado.requiredAnnualPayment, m("1200"));
        QCOMPARE(r.colorado.installments.at(0).state, Installment::State::Paid);
        QCOMPARE(r.colorado.remaining, m("900"));
    }

    // No profit: no SE tax, nothing to set aside; other wages with enough
    // withholding need no estimates.
    void zeroAndNegativeProfit()
    {
        EstimatedTaxInput in;
        in.asOf = QDate(2026, 6, 30);
        in.yearToDateProfit = m("0");
        const EstimatedTaxResult zero = planEstimatedTax(t, in);
        QCOMPARE(zero.seTax, m("0"));
        QCOMPARE(zero.federal.totalTax, m("0"));
        QVERIFY(!zero.federal.paymentsRequired);
        QCOMPARE(zero.setAsidePercent, 0);
        // A $10,000 loss against $50,000 of wages with $4,000 withheld:
        // AGI 40,000; 3 = 40,000 − 16,100 = 23,900; 4 = 1,240 + 12 % ×
        // 11,500 = 2,620; no SE tax, no QBI; 12a = 2,358 < withholding.
        in.projectedProfit = m("-10000");
        in.otherIncome = m("50000");
        in.wagesForSocialSecurity = m("50000");
        in.wagesForMedicare = m("50000");
        in.federalWithholding = m("4000");
        const EstimatedTaxResult loss = planEstimatedTax(t, in);
        QCOMPARE(loss.seTax, m("0"));
        QCOMPARE(loss.qbiDeduction, m("0"));
        QCOMPARE(loss.taxableIncome, m("23900"));
        QCOMPARE(loss.federal.totalTax, m("2620"));
        QVERIFY(!loss.federal.paymentsRequired);
        QCOMPARE(loss.setAsidePercent, 0);
    }

    // Annualizing: $30,000 through June 30, 2026 (day 181 of 365) →
    // 30,000 × 365 ÷ 181 = 60,497.237… → 60,497.24.
    void annualizes()
    {
        QCOMPARE(annualizeProfit(m("30000"), QDate(2026, 6, 30)), m("60497.24"));
        QCOMPARE(annualizeProfit(m("30000"), QDate(2026, 12, 31)), m("30000"));
    }

    // Wages already over part of the social security base, and the
    // Additional Medicare Tax on SE income above the reduced threshold:
    // wages 150,000, profit 100,000 → 3 = 92,350; 7 = 184,500 − 150,000 =
    // 34,500; 9 = 34,500 × 12.4 % = 4,278.00; 4 = 92,350 × 2.9 % =
    // 2,678.15; SE tax 6,956.15. Additional Medicare: threshold 200,000 −
    // wages 150,000 = 50,000; 92,350 − 50,000 = 42,350 × 0.9 % = 381.15.
    void wagesShareTheSocialSecurityBase()
    {
        EstimatedTaxInput in;
        in.asOf = QDate(2026, 10, 8);
        in.projectedProfit = m("100000");
        in.otherIncome = m("150000");
        in.wagesForSocialSecurity = m("150000");
        in.wagesForMedicare = m("150000");
        const EstimatedTaxResult r = planEstimatedTax(t, in);
        QCOMPARE(worksheetAmount(r.seWorksheet, QStringLiteral("8")), m("34500"));
        QCOMPARE(r.seTax, m("6956.15"));
        QCOMPARE(r.additionalMedicare, m("381.15"));
    }

    // The $400 floor: profit $400 → 3 = 369.40 < 400 → no SE tax.
    // The QBI minimum: profit $1,100 with $30,000 of wages → SE 3 =
    // 1,015.85; tax 29.46 + 125.97 = 155.43; deduction 77.72; QBI 1,022.28
    // ≥ 1,000 → 20 % is 204.46, the minimum deduction $400 applies.
    // Above the threshold the QBI deduction is left out, and said so.
    void floorsAndThresholds()
    {
        EstimatedTaxInput in;
        in.asOf = QDate(2026, 10, 8);
        in.projectedProfit = m("400");
        QCOMPARE(planEstimatedTax(t, in).seTax, m("0"));
        in.projectedProfit = m("1100");
        in.otherIncome = m("30000");
        in.wagesForSocialSecurity = m("30000");
        in.wagesForMedicare = m("30000");
        const EstimatedTaxResult small = planEstimatedTax(t, in);
        QCOMPARE(small.seTax, m("155.43"));
        QCOMPARE(small.seDeduction, m("77.72"));
        QCOMPARE(small.qbiDeduction, m("400"));
        in.projectedProfit = m("300000");
        in.otherIncome = m("0");
        const EstimatedTaxResult high = planEstimatedTax(t, in);
        QCOMPARE(high.qbiDeduction, m("0"));
        QVERIFY(high.notes.join(QLatin1Char(' ')).contains(QStringLiteral("8995-A")));
    }

    void anotherYearIsRefused()
    {
        EstimatedTaxInput in;
        in.asOf = QDate(2027, 2, 1);
        const EstimatedTaxResult r = planEstimatedTax(t, in);
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(QStringLiteral("2027")));
    }
};

QTEST_GUILESS_MAIN(TstPayrollEstimates)
#include "tst_payroll_estimates.moc"
