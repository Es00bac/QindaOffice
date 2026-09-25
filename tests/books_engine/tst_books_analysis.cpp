// SPDX-License-Identifier: GPL-3.0-or-later
// FinancialAnalysis on the fixture company: ratios (exact fixed point),
// expense mix, monthly trends and the trailing-average forecast.
#include "analysis.h"
#include "books_fixture.h"

using namespace BooksTest;

class TstBooksAnalysis : public QObject {
    Q_OBJECT
    ReportsFixture *fx = nullptr;

    static const Ratio &find(const QList<Ratio> &ratios, const QString &id)
    {
        for (const Ratio &r : ratios) {
            if (r.id == id) {
                return r;
            }
        }
        qFatal("no ratio %s", qPrintable(id));
    }

private slots:
    void initTestCase() { fx = new ReportsFixture; }
    void cleanupTestCase() { delete fx; }

    void ratios()
    {
        const QList<Ratio> r = FinancialAnalysis(fx->f()).ratios(QDate(2026, 1, 1), QDate(2026, 3, 31), Basis::Accrual);
        QCOMPARE(r.size(), 7);
        // (1,500 − 80) / 1,500 = 94.67%
        QCOMPARE(find(r, QStringLiteral("gross_margin")).value, 947);
        QCOMPARE(find(r, QStringLiteral("gross_margin")).display(), QStringLiteral("94.7%"));
        // −200 / 1,500
        QCOMPARE(find(r, QStringLiteral("net_margin")).value, -133);
        // 10,020 / 220 = 45.545…
        QCOMPARE(find(r, QStringLiteral("current_ratio")).value, 4555);
        QCOMPARE(find(r, QStringLiteral("current_ratio")).display(), QStringLiteral("45.55×"));
        QCOMPARE(find(r, QStringLiteral("quick_ratio")).value, 4555);
        // 220 / 9,800
        QCOMPARE(find(r, QStringLiteral("debt_to_equity")).value, 2);
        // 620 × 90 / 1,500 = 37.2 days
        QCOMPARE(find(r, QStringLiteral("dso")).value, 37);
        QCOMPARE(find(r, QStringLiteral("dso")).display(), QStringLiteral("37 days"));
        // 80 × 90 / 1,700 = 4.2 days
        QCOMPARE(find(r, QStringLiteral("dpo")).value, 4);
        for (const Ratio &ratio : r) {
            QVERIFY2(!ratio.definition.isEmpty(), qPrintable(ratio.id));
            QVERIFY(ratio.available);
        }
        // No income in a period → margins are unavailable, not a crash.
        const QList<Ratio> empty = FinancialAnalysis(fx->f()).ratios(QDate(2025, 1, 1), QDate(2025, 3, 31));
        QVERIFY(!find(empty, QStringLiteral("gross_margin")).available);
        QCOMPARE(find(empty, QStringLiteral("gross_margin")).display(), QStringLiteral("—"));
    }

    void expenseBreakdown()
    {
        const QList<ExpenseShare> shares =
            FinancialAnalysis(fx->f()).expenseBreakdown(QDate(2026, 1, 1), QDate(2026, 3, 31), 2, Basis::Accrual);
        QCOMPARE(shares.size(), 3); // top 2 + everything else
        QCOMPARE(shares[0].amount.minor(), 150000);
        QCOMPARE(shares[0].share, 882);
        QCOMPARE(shares[1].amount.minor(), 12000);
        QCOMPARE(shares[2].accountId, 0);
        QCOMPARE(shares[2].amount.minor(), 8000);
        QVERIFY(!shares[0].definition.isEmpty());
    }

    void trendsAndForecast()
    {
        FinancialAnalysis analysis(fx->f());
        const QList<TrendPoint> trend = analysis.monthlyTrends(QDate(2026, 3, 15), 3, Basis::Accrual);
        QCOMPARE(trend.size(), 3);
        QCOMPARE(trend[0].month, QDate(2026, 1, 1));
        QCOMPARE(trend[0].revenue.minor(), 120000);
        QCOMPARE(trend[0].expenses.minor(), 162000);
        QCOMPARE(trend[0].netIncome.minor(), -42000);
        QCOMPARE(trend[0].cashBalance.minor(), 1000000);
        QCOMPARE(trend[1].expenses.minor(), 8000);
        QCOMPARE(trend[1].cashBalance.minor(), 910000);
        QCOMPARE(trend[2].revenue.minor(), 30000);
        QCOMPARE(trend[2].cashBalance.minor(), 940000);
        // Trailing 3-month average: revenue 1,500/3, expenses 1,700/3.
        const QList<TrendPoint> ahead = analysis.forecast(trend, 2, 3);
        QCOMPARE(ahead.size(), 2);
        QVERIFY(ahead[0].forecast);
        QCOMPARE(ahead[0].month, QDate(2026, 4, 1));
        QCOMPARE(ahead[0].revenue.minor(), 50000);
        QCOMPARE(ahead[0].expenses.minor(), 56667);
        QCOMPARE(ahead[0].netIncome.minor(), -6667);
        QCOMPARE(ahead[0].cashBalance.minor(), 933333);
        QCOMPARE(ahead[1].cashBalance.minor(), 926666);
        QVERIFY(!FinancialAnalysis::definition(QStringLiteral("forecast")).isEmpty());
        // The cash-basis trend recognises the Acme payment in February.
        const QList<TrendPoint> cash = analysis.monthlyTrends(QDate(2026, 3, 1), 3, Basis::Cash);
        QCOMPARE(cash[1].revenue.minor(), 60000);
        QCOMPARE(cash[2].cashBalance.minor(), 940000); // cash is cash on either basis
    }
};

QTEST_GUILESS_MAIN(TstBooksAnalysis)
#include "tst_books_analysis.moc"
