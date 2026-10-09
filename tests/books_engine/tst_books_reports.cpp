// SPDX-License-Identifier: GPL-3.0-or-later
// Every report on the fixture company (books_fixture.h: Q1 2026) against
// hand-computed numbers. The worked example is written out in
// docs/books.md so a reader can check the arithmetic.
#include "books_fixture.h"
#include "ledger_query.h"
#include "reports.h"

using namespace BooksTest;

namespace {
const QDate kJan1(2026, 1, 1);
const QDate kMar31(2026, 3, 31);
} // namespace

class TstBooksReports : public QObject {
    Q_OBJECT
    ReportsFixture *fx = nullptr;
    QString acct(const char *number) const { return QStringLiteral("account:%1").arg(fx->account(QLatin1String(number))); }

private slots:
    void initTestCase() { fx = new ReportsFixture; }
    void cleanupTestCase() { delete fx; }

    void profitAndLossAccrual()
    {
        const ReportTable pl = Reports(fx->f()).profitAndLoss({kJan1, kMar31, Basis::Accrual});
        const QString t = QStringLiteral("total");
        QCOMPARE(pl.money(acct("4100"), t), 130000);
        QCOMPARE(pl.money(acct("4000"), t), 20000);
        QCOMPARE(pl.money(QStringLiteral("total:income"), t), 150000);
        QCOMPARE(pl.money(QStringLiteral("total:cogs"), t), 8000);
        QCOMPARE(pl.money(QStringLiteral("total:gross_profit"), t), 142000);
        QCOMPARE(pl.money(acct("6500"), t), 150000);
        QCOMPARE(pl.money(acct("6400"), t), 12000);
        QCOMPARE(pl.money(QStringLiteral("total:expenses"), t), 162000);
        QCOMPARE(pl.money(QStringLiteral("total:net_operating_income"), t), -20000);
        QCOMPARE(pl.money(QStringLiteral("total:net_income"), t), -20000);
        QVERIFY(pl.row(QStringLiteral("section:other_income")) == nullptr); // empty sections are left out
        QCOMPARE(pl.row(acct("6500"))->drill.accountId, fx->account(QStringLiteral("6500")));
        QVERIFY(pl.subtitle.contains(QStringLiteral("Accrual")));
    }

    void profitAndLossCashBasis()
    {
        // Cash: only the 600.00 of INV-1001 that was paid, the 300.00
        // receipt, the paid rent and the card charge. The unpaid widget
        // invoice and the unpaid supplier bill are not recognised.
        const ReportTable pl = Reports(fx->f()).profitAndLoss({kJan1, kMar31, Basis::Cash});
        const QString t = QStringLiteral("total");
        QCOMPARE(pl.money(acct("4100"), t), 90000);
        QCOMPARE(pl.money(acct("4000"), t), 0);
        QCOMPARE(pl.money(QStringLiteral("total:cogs"), t), 0);
        QCOMPARE(pl.money(QStringLiteral("total:expenses"), t), 162000);
        QCOMPARE(pl.money(QStringLiteral("total:net_income"), t), -72000);
        // The recognition lands on the payment date: nothing in January.
        const ReportTable jan = Reports(fx->f()).profitAndLoss({kJan1, QDate(2026, 1, 31), Basis::Cash});
        QCOMPARE(jan.money(acct("4100"), t), 0);
        QCOMPARE(jan.money(QStringLiteral("total:expenses"), t), 12000); // the card charge only
    }

    void profitAndLossColumns()
    {
        PnlOptions months{kJan1, kMar31, Basis::Accrual, Comparison::None, PnlColumns::Months};
        const ReportTable m = Reports(fx->f()).profitAndLoss(months);
        QCOMPARE(m.columns.size(), 5); // label, Jan, Feb, Mar, total
        QCOMPARE(m.money(QStringLiteral("total:income"), QStringLiteral("2026-01")), 120000);
        QCOMPARE(m.money(QStringLiteral("total:income"), QStringLiteral("2026-03")), 30000);
        QCOMPARE(m.money(QStringLiteral("total:cogs"), QStringLiteral("2026-02")), 8000);
        QCOMPARE(m.money(QStringLiteral("total:net_income"), QStringLiteral("2026-01")), -42000);
        QCOMPARE(m.money(QStringLiteral("total:net_income"), QStringLiteral("total")), -20000);

        PnlOptions classes{kJan1, kMar31, Basis::Accrual, Comparison::None, PnlColumns::Classes};
        const ReportTable c = Reports(fx->f()).profitAndLoss(classes);
        QCOMPARE(c.money(QStringLiteral("total:income"), QStringLiteral("class:%1").arg(fx->studioClass)), 100000);
        QCOMPARE(c.money(QStringLiteral("total:income"), QStringLiteral("class:0")), 50000);

        PnlOptions prior{kJan1, kMar31, Basis::Accrual, Comparison::PriorYear, PnlColumns::Total};
        const ReportTable p = Reports(fx->f()).profitAndLoss(prior);
        QCOMPARE(p.columns.last().id, QStringLiteral("change_pct"));
        QCOMPARE(p.money(QStringLiteral("total:income"), QStringLiteral("prior")), 0);
        QCOMPARE(p.money(QStringLiteral("total:income"), QStringLiteral("change")), 150000);
        PnlOptions period{QDate(2026, 3, 1), kMar31, Basis::Accrual, Comparison::PriorPeriod, PnlColumns::Total};
        const ReportTable pp = Reports(fx->f()).profitAndLoss(period);
        QCOMPARE(pp.money(QStringLiteral("total:income"), QStringLiteral("total")), 30000);
        QCOMPARE(pp.money(QStringLiteral("total:income"), QStringLiteral("prior")), 0);     // February
        QCOMPARE(pp.money(QStringLiteral("total:cogs"), QStringLiteral("prior")), 8000);
        const int pct = pp.columnIndex(QStringLiteral("change_pct"));
        QCOMPARE(pp.row(QStringLiteral("total:cogs"))->cells[pct].value, -1000); // −100.0%
    }

    void balanceSheetBalances()
    {
        for (Basis basis : {Basis::Accrual, Basis::Cash}) {
            for (const QDate &asOf : {QDate(2026, 1, 31), QDate(2026, 2, 28), kMar31, QDate(2026, 12, 31)}) {
                const ReportTable bs = Reports(fx->f()).balanceSheet({asOf, basis});
                QCOMPARE(bs.money(QStringLiteral("total:assets"), QStringLiteral("total")),
                         bs.money(QStringLiteral("total:liabilities_and_equity"), QStringLiteral("total")));
            }
        }
        const ReportTable bs = Reports(fx->f()).balanceSheet({kMar31, Basis::Accrual, Comparison::PriorYear});
        const QString t = QStringLiteral("total");
        QCOMPARE(bs.money(acct("1000"), t), 940000);
        QCOMPARE(bs.money(acct("1100"), t), 62000);
        QCOMPARE(bs.money(QStringLiteral("total:assets"), t), 1002000);
        QCOMPARE(bs.money(acct("2000"), t), 8000);
        QCOMPARE(bs.money(acct("2100"), t), 12000);
        QCOMPARE(bs.money(acct("2200"), t), 2000);
        QCOMPARE(bs.money(QStringLiteral("total:liabilities"), t), 22000);
        QCOMPARE(bs.money(acct("3110"), t), 1000000);
        QCOMPARE(bs.money(QStringLiteral("net_income"), t), -20000);
        QCOMPARE(bs.money(QStringLiteral("total:equity"), t), 980000);
        QCOMPARE(bs.money(QStringLiteral("total:liabilities_and_equity"), t), 1002000);
        QCOMPARE(bs.money(QStringLiteral("total:assets"), QStringLiteral("prior")), 0);
        // Sub-account subtotal: Owner's Equity holds contributions.
        QCOMPARE(bs.money(QStringLiteral("subtotal:") + acct("3100"), t), 1000000);

        const ReportTable cash = Reports(fx->f()).balanceSheet({kMar31, Basis::Cash});
        QCOMPARE(cash.money(acct("1100"), t), 0); // A/R does not exist on the cash basis here
        QCOMPARE(cash.money(QStringLiteral("total:assets"), t), 940000);
        QCOMPARE(cash.money(QStringLiteral("total:liabilities"), t), 12000);
        QCOMPARE(cash.money(QStringLiteral("net_income"), t), -72000);
    }

    void netIncomeRollsIntoRetainedEarnings()
    {
        // In 2027 the 2026 result is retained earnings, not net income.
        const ReportTable bs = Reports(fx->f()).balanceSheet({QDate(2027, 1, 15), Basis::Accrual});
        QCOMPARE(bs.money(QStringLiteral("account:%1").arg(fx->role(SystemRole::RetainedEarnings)), QStringLiteral("total")),
                 -20000);
        QCOMPARE(bs.money(QStringLiteral("net_income"), QStringLiteral("total")), 0);
        QCOMPARE(bs.money(QStringLiteral("total:assets"), QStringLiteral("total")),
                 bs.money(QStringLiteral("total:liabilities_and_equity"), QStringLiteral("total")));
    }

    void cashFlowTiesToCash()
    {
        for (Basis basis : {Basis::Accrual, Basis::Cash}) {
            const ReportTable cf = Reports(fx->f()).cashFlow(kJan1, kMar31, basis);
            const QString t = QStringLiteral("total");
            QCOMPARE(cf.money(QStringLiteral("cash_beginning"), t), 0);
            QCOMPARE(cf.money(QStringLiteral("total:net_change"), t), 940000);
            QCOMPARE(cf.money(QStringLiteral("cash_end"), t), 940000);
            QCOMPARE(cf.money(QStringLiteral("total:financing"), t), 1000000);
        }
        const ReportTable cf = Reports(fx->f()).cashFlow(kJan1, kMar31, Basis::Accrual);
        QCOMPARE(cf.money(QStringLiteral("net_income"), QStringLiteral("total")), -20000);
        QCOMPARE(cf.money(acct("1100"), QStringLiteral("total")), -62000);
        QCOMPARE(cf.money(acct("2000"), QStringLiteral("total")), 8000);
        QCOMPARE(cf.money(QStringLiteral("total:operating"), QStringLiteral("total")), -60000);
        // A sub-period ties too: February.
        const ReportTable feb = Reports(fx->f()).cashFlow(QDate(2026, 2, 1), QDate(2026, 2, 28), Basis::Accrual);
        QCOMPARE(feb.money(QStringLiteral("cash_beginning"), QStringLiteral("total")), 1000000);
        QCOMPARE(feb.money(QStringLiteral("cash_end"), QStringLiteral("total")), 910000);
    }

    void trialBalance()
    {
        for (Basis basis : {Basis::Accrual, Basis::Cash}) {
            const ReportTable tb = Reports(fx->f()).trialBalance(kMar31, basis);
            QCOMPARE(tb.money(QStringLiteral("total"), QStringLiteral("debit")),
                     tb.money(QStringLiteral("total"), QStringLiteral("credit")));
        }
        const ReportTable tb = Reports(fx->f()).trialBalance(kMar31, Basis::Accrual);
        QCOMPARE(tb.money(QStringLiteral("total"), QStringLiteral("debit")), 1172000);
        QCOMPARE(tb.money(acct("1000"), QStringLiteral("debit")), 940000);
        QCOMPARE(tb.money(acct("4100"), QStringLiteral("credit")), 130000);
    }

    void agingOnBoundaryDates()
    {
        Reports reports(fx->f());
        // INV-1001 (400.00 open) due Feb 9; INV-1002 (220.00) due Feb 14.
        const ReportTable onTime = reports.arAgingSummary(QDate(2026, 2, 9));
        QCOMPARE(onTime.money(QStringLiteral("total"), QStringLiteral("current")), 62000);
        const ReportTable thirty = reports.arAgingSummary(QDate(2026, 3, 11)); // exactly 30 days late
        QCOMPARE(thirty.money(QStringLiteral("party:%1").arg(fx->acme), QStringLiteral("1_30")), 40000);
        const ReportTable thirtyOne = reports.arAgingSummary(QDate(2026, 3, 12));
        QCOMPARE(thirtyOne.money(QStringLiteral("party:%1").arg(fx->acme), QStringLiteral("31_60")), 40000);
        QCOMPARE(thirtyOne.money(QStringLiteral("party:%1").arg(fx->beta), QStringLiteral("1_30")), 22000);
        const ReportTable ninetyOne = reports.arAgingSummary(QDate(2026, 5, 11)); // 91 days after Feb 9
        QCOMPARE(ninetyOne.money(QStringLiteral("party:%1").arg(fx->acme), QStringLiteral("over_90")), 40000);
        QCOMPARE(ninetyOne.money(QStringLiteral("total"), QStringLiteral("total")), 62000);
        // Before the payment the whole invoice is open.
        QCOMPARE(reports.arAgingSummary(QDate(2026, 2, 4)).money(QStringLiteral("total"), QStringLiteral("total")), 122000);
        const ReportTable detail = reports.arAgingDetail(kMar31);
        QCOMPARE(detail.money(QStringLiteral("doc:%1").arg(fx->inv1001), QStringLiteral("open")), 40000);
        QCOMPARE(detail.row(QStringLiteral("doc:%1").arg(fx->inv1001))->cells[5].value, 50); // days past due
        QCOMPARE(detail.money(QStringLiteral("total"), QStringLiteral("open")), 62000);
        const ReportTable ap = reports.apAgingSummary(kMar31);
        QCOMPARE(ap.money(QStringLiteral("party:%1").arg(fx->supplier), QStringLiteral("1_30")), 8000);
        QCOMPARE(reports.apAgingDetail(kMar31).money(QStringLiteral("total"), QStringLiteral("open")), 8000);
    }

    void balancesAndOpenDocuments()
    {
        Reports reports(fx->f());
        const ReportTable customers = reports.customerBalances(kMar31);
        QCOMPARE(customers.money(QStringLiteral("party:%1").arg(fx->acme), QStringLiteral("balance")), 40000);
        QCOMPARE(customers.money(QStringLiteral("total"), QStringLiteral("balance")), 62000);
        QCOMPARE(reports.vendorBalances(kMar31).money(QStringLiteral("total"), QStringLiteral("balance")), 8000);
        const ReportTable open = reports.openInvoices(kMar31);
        QCOMPARE(open.money(QStringLiteral("total"), QStringLiteral("amount")), 122000);
        QCOMPARE(open.money(QStringLiteral("total"), QStringLiteral("open")), 62000);
        QCOMPARE(reports.unpaidBills(kMar31).money(QStringLiteral("total"), QStringLiteral("open")), 8000);
        QCOMPARE(reports.unpaidBills(QDate(2026, 2, 9)).money(QStringLiteral("total"), QStringLiteral("open")), 150000);
    }

    void salesAndExpenseBreakdowns()
    {
        Reports reports(fx->f());
        const ReportTable byCustomer = reports.salesByCustomer(kJan1, kMar31, Basis::Accrual);
        QCOMPARE(byCustomer.money(QStringLiteral("party:%1").arg(fx->acme), QStringLiteral("total")), 100000);
        QCOMPARE(byCustomer.money(QStringLiteral("party:%1").arg(fx->beta), QStringLiteral("total")), 20000);
        QCOMPARE(byCustomer.money(QStringLiteral("party:%1").arg(fx->walkIn), QStringLiteral("total")), 30000);
        QCOMPARE(byCustomer.money(QStringLiteral("total"), QStringLiteral("total")), 150000);
        QCOMPARE(byCustomer.row(QStringLiteral("party:%1").arg(fx->acme))->cells[2].value, 667); // 66.7%
        const ReportTable cash = reports.salesByCustomer(kJan1, kMar31, Basis::Cash);
        QCOMPARE(cash.money(QStringLiteral("party:%1").arg(fx->acme), QStringLiteral("total")), 60000);
        QVERIFY(cash.row(QStringLiteral("party:%1").arg(fx->beta)) == nullptr);
        const ReportTable byItem = reports.salesByItem(kJan1, kMar31);
        QCOMPARE(byItem.money(QStringLiteral("item:%1").arg(fx->consulting), QStringLiteral("total")), 130000);
        QCOMPARE(byItem.money(QStringLiteral("item:%1").arg(fx->widget), QStringLiteral("total")), 20000);
        const ReportTable byVendor = reports.expensesByVendor(kJan1, kMar31);
        QCOMPARE(byVendor.money(QStringLiteral("party:%1").arg(fx->landlord), QStringLiteral("total")), 150000);
        QCOMPARE(byVendor.money(QStringLiteral("party:%1").arg(fx->officeDepot), QStringLiteral("total")), 12000);
        QCOMPARE(byVendor.money(QStringLiteral("party:%1").arg(fx->supplier), QStringLiteral("total")), 8000);
        const ReportTable byCategory = reports.expensesByCategory(kJan1, kMar31);
        QCOMPARE(byCategory.money(QStringLiteral("total:expenses"), QStringLiteral("total")), 170000);
        QCOMPARE(byCategory.row(acct("6500"))->cells[2].value, 882); // 88.2%
    }

    void salesTaxAndBudget()
    {
        Reports reports(fx->f());
        const ReportTable tax = reports.salesTaxLiability(kJan1, kMar31);
        QCOMPARE(tax.money(QStringLiteral("total"), QStringLiteral("taxable")), 20000);
        QCOMPARE(tax.money(QStringLiteral("total"), QStringLiteral("collected")), 2000);
        QCOMPARE(tax.money(QStringLiteral("total"), QStringLiteral("balance")), 2000);
        QCOMPARE(tax.text(QStringLiteral("total:agency:%1").arg(fx->agency), QStringLiteral("balance")),
                 QStringLiteral("20.00"));
        Budgets budgets(fx->f());
        for (int m = 1; m <= 3; ++m) {
            QVERIFY(budgets.set(fx->account(QStringLiteral("6500")), 2026, m, 150000).ok);
        }
        QVERIFY(budgets.setAnnual(fx->account(QStringLiteral("4100")), 2026, 1200000).ok); // 1,000.00 a month
        QCOMPARE(budgets.total(fx->account(QStringLiteral("4100")), kJan1, kMar31), 300000);
        const ReportTable bva = reports.budgetVsActual(kJan1, kMar31);
        QCOMPARE(bva.money(acct("6500"), QStringLiteral("actual")), 150000);
        QCOMPARE(bva.money(acct("6500"), QStringLiteral("budget")), 450000);
        QCOMPARE(bva.money(acct("6500"), QStringLiteral("over")), -300000);
        QCOMPARE(bva.row(acct("6500"))->cells[4].value, 333); // 33.3% of budget
        QCOMPARE(bva.money(acct("4100"), QStringLiteral("budget")), 300000);
    }

    void ledgerListings()
    {
        Reports reports(fx->f());
        const ReportTable gl = reports.generalLedger(kJan1, kMar31);
        QCOMPARE(gl.money(QStringLiteral("subtotal:") + acct("1000"), QStringLiteral("balance")), 940000);
        QCOMPARE(gl.money(QStringLiteral("subtotal:") + acct("1000"), QStringLiteral("debit")), 1090000);
        QCOMPARE(gl.money(QStringLiteral("subtotal:") + acct("1000"), QStringLiteral("credit")), 150000);
        const ReportTable one = reports.generalLedger(QDate(2026, 3, 1), kMar31, fx->account(QStringLiteral("1000")));
        QCOMPARE(one.money(QStringLiteral("opening:%1").arg(fx->account(QStringLiteral("1000"))), QStringLiteral("balance")),
                 910000);
        const ReportTable journal = reports.journal(kJan1, kMar31);
        QCOMPARE(journal.money(QStringLiteral("total"), QStringLiteral("debit")),
                 journal.money(QStringLiteral("total"), QStringLiteral("credit")));
        const ReportTable list = reports.transactionList(kJan1, kMar31);
        QCOMPARE(list.rows.size(), 10); // JE, 2 invoices, bill, card, payment, deposit, bill payment, bill, receipt
        QCOMPARE(Reports::catalog().size(), 26); // M68: open purchase orders and three time reports
    }

    void cashRecognitionIsExactOverPartialPayments()
    {
        // Three payments of an awkward total recognise exactly the lines.
        TestCompany c;
        const qint64 cust = c.customer(QStringLiteral("Thirds Inc"));
        const qint64 a = c.item(QStringLiteral("A"), 3333, c.account(QStringLiteral("4000")));
        const qint64 b = c.item(QStringLiteral("B"), 6667, c.account(QStringLiteral("4100")));
        BusinessDocument inv;
        inv.partyId = cust;
        inv.date = QDate(2026, 1, 1);
        inv.lines = {itemLine(a), itemLine(b)};
        const Result r = Sales(c.f()).createInvoice(inv);
        QVERIFY(r.ok);
        for (int i = 0; i < 3; ++i) {
            const qint64 part = i < 2 ? 3333 : 3334;
            QVERIFY(Sales(c.f())
                        .receivePayment({cust, QDate(2026, 1, 10 + i), usd(part), 0, {}, {}, {PaymentApplication{r.id, usd(part)}}})
                        .ok);
        }
        const QHash<qint64, qint64> net = LedgerQuery(c.f()).netByAccount({{}, QDate(2026, 12, 31), Basis::Cash, {}});
        QCOMPARE(-net.value(c.account(QStringLiteral("4000"))), 3333);
        QCOMPARE(-net.value(c.account(QStringLiteral("4100"))), 6667);
        QCOMPARE(net.value(c.role(SystemRole::AccountsReceivable)), 0);
        const QList<VirtualLine> first = LedgerQuery(c.f()).cashRecognition(QDate(2026, 1, 10), QDate(2026, 1, 10));
        qint64 sum = 0;
        for (const VirtualLine &v : first) {
            sum += v.debit - v.credit;
        }
        QCOMPARE(sum, 0); // every recognition balances
    }
};

QTEST_GUILESS_MAIN(TstBooksReports)
#include "tst_books_reports.moc"
