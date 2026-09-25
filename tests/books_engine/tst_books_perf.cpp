// SPDX-License-Identifier: GPL-3.0-or-later
// Large-company smoke test: 50,000 journal lines over three years; the
// Profit and Loss and the Balance Sheet must each come back in under a
// second (SQL aggregation over the indexed journal_lines, D-045).
#include "books_fixture.h"
#include "reports.h"
#include "sql_util.h"

#include <QElapsedTimer>

using namespace BooksTest;

class TstBooksPerf : public QObject {
    Q_OBJECT
private slots:
    void fiftyThousandLines()
    {
        TestCompany c;
        const QList<qint64> expenses{c.account(QStringLiteral("6000")), c.account(QStringLiteral("6100")),
                                     c.account(QStringLiteral("6300")), c.account(QStringLiteral("6400")),
                                     c.account(QStringLiteral("6500")), c.account(QStringLiteral("6900"))};
        const qint64 bank = c.account(QStringLiteral("1000"));
        const qint64 income = c.account(QStringLiteral("4100"));
        QElapsedTimer build;
        build.start();
        {
            SqlTransaction bulk(c.f());
            Ledger ledger(c.f());
            // 10,000 entries × 5 lines = 50,000 lines.
            for (int i = 0; i < 10000; ++i) {
                const QDate date = QDate(2024, 1, 1).addDays(i % 1095);
                const qint64 a = 1000 + (i * 37) % 9000;
                const qint64 b = 500 + (i * 53) % 4000;
                // Five lines: debits a + b/2 + (b − b/2) + 1 = credits a + b + 1.
                const JournalEntryInput je{date, {}, {},
                                           {debit(bank, a), credit(income, a + b + 1), debit(expenses[i % 6], b / 2),
                                            debit(expenses[(i + 1) % 6], b - b / 2), debit(expenses[(i + 2) % 6], 1)}};
                const Result r = ledger.postJournalEntry(je);
                QVERIFY2(r.ok, qPrintable(r.error));
            }
            QVERIFY(bulk.commit());
        }
        const qint64 lines = Sql::scalarInt(c.f().database(), QStringLiteral("SELECT COUNT(*) FROM journal_lines"));
        QVERIFY2(lines >= 50000, qPrintable(QString::number(lines)));
        qInfo("built %lld journal lines in %lld ms", lines, build.elapsed());

        Reports reports(c.f());
        QElapsedTimer timer;
        timer.start();
        const ReportTable pl = reports.profitAndLoss({QDate(2024, 1, 1), QDate(2026, 12, 31), Basis::Accrual});
        const qint64 plMs = timer.elapsed();
        timer.restart();
        const ReportTable bs = reports.balanceSheet({QDate(2026, 12, 31), Basis::Accrual});
        const qint64 bsMs = timer.elapsed();
        timer.restart();
        const ReportTable months = reports.profitAndLoss(
            {QDate(2026, 1, 1), QDate(2026, 12, 31), Basis::Accrual, Comparison::None, PnlColumns::Months});
        const qint64 monthsMs = timer.elapsed();
        qInfo("P&L %lld ms, balance sheet %lld ms, P&L by month %lld ms", plMs, bsMs, monthsMs);
        QVERIFY2(plMs < 1000, qPrintable(QString::number(plMs)));
        QVERIFY2(bsMs < 1000, qPrintable(QString::number(bsMs)));
        QCOMPARE(bs.money(QStringLiteral("total:assets"), QStringLiteral("total")),
                 bs.money(QStringLiteral("total:liabilities_and_equity"), QStringLiteral("total")));
        QVERIFY(pl.money(QStringLiteral("total:income"), QStringLiteral("total")) > 0);
        QCOMPARE(months.columns.size(), 14);
    }
};

QTEST_GUILESS_MAIN(TstBooksPerf)
#include "tst_books_perf.moc"
