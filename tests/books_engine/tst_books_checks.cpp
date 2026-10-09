// SPDX-License-Identifier: GPL-3.0-or-later
// The "to be printed" check queue (M68, D-077): checks and bill payments
// wait unnumbered, printing numbers them consecutively from the account's
// next number (the postings re-made under the number, amounts untouched), a
// misprint goes back to the queue with its number recorded void, a
// cancelled run is taken back, and the refusals.
#include "books_fixture.h"

#include "check_queue.h"
#include "sql_util.h"

using namespace BooksTest;

namespace {

QString str(const char *text)
{
    return QString::fromUtf8(text);
}

struct Office : TestCompany {
    qint64 checking = 0;
    qint64 card = 0;
    qint64 pixel = 0;

    Office()
    {
        checking = account(str("1000"));
        card = account(str("2100"));
        pixel = vendor(str("Pixel Print Works"), account(str("6400")));
        ChartOfAccounts(f()).setOpeningBalance(checking, usd(500000), QDate(2025, 12, 31));
    }
    Result check(qint64 cents, const QDate &date, bool toPrint, const QString &number = {}, qint64 from = 0)
    {
        BusinessDocument d;
        d.kind = DocKind::Check;
        d.partyId = pixel;
        d.date = date;
        d.accountId = from != 0 ? from : checking;
        d.number = number;
        d.toPrint = toPrint;
        d.lines = {accountLine(0, cents, str("Proofs"))};
        return Purchases(f()).createExpense(d);
    }
    QString number(qint64 id) { return Documents(f()).load(id)->number; }
    // The live transaction's number, as the register shows it.
    QString postedNumber(qint64 id)
    {
        return Sql::scalar(f().database(), str("SELECT t.number FROM transactions t JOIN documents d ON d.txn_id = t.id"
                                               " WHERE d.id = ?"),
                           {id})
            .toString();
    }
};

} // namespace

class TstBooksChecks : public QObject {
    Q_OBJECT
private slots:
    void theQueueNumbersChecksAtPrintTime()
    {
        Office c;
        CheckQueue queue(c.f());
        QCOMPARE(queue.nextNumber(c.checking), 1001); // no checks yet
        QVERIFY(c.check(5000, QDate(2026, 3, 1), false, str("1042")).ok);
        QCOMPARE(queue.nextNumber(c.checking), 1043); // a hand-written check moves it on

        // A check saved to print later posts at once, without a number.
        const Result a = c.check(12000, QDate(2026, 3, 2), true, str("999"));
        QVERIFY2(a.ok, qPrintable(a.error));
        QCOMPARE(c.number(a.id), QString());
        QVERIFY(Documents(c.f()).load(a.id)->toPrint);
        QCOMPARE(c.balance(str("1000")), 500000 - 5000 - 12000);
        // A bill paid by check, to print.
        BusinessDocument bill;
        bill.partyId = c.pixel;
        bill.date = QDate(2026, 3, 1);
        bill.lines = {accountLine(0, 30000, str("Posters"))};
        const qint64 billId = Purchases(c.f()).createBill(bill).id;
        QList<qint64> ids;
        QVERIFY(Purchases(c.f()).payBills(QDate(2026, 3, 3), c.checking, {{billId, usd(30000)}}, str("ACH?"), {}, &ids, true).ok);
        const qint64 payment = ids.first();
        QCOMPARE(Documents(c.f()).load(payment)->reference, QString());
        // What cannot wait to print: a card charge, a debit-card expense, a card payment.
        QCOMPARE(c.check(1000, QDate(2026, 3, 2), true, {}, c.card).code, ErrorCode::Invalid);
        BusinessDocument debit;
        debit.kind = DocKind::Expense;
        debit.partyId = c.pixel;
        debit.date = QDate(2026, 3, 2);
        debit.accountId = c.checking;
        debit.toPrint = true;
        debit.lines = {accountLine(0, 1000)};
        QCOMPARE(Purchases(c.f()).createExpense(debit).code, ErrorCode::Invalid);
        QCOMPARE(Purchases(c.f()).payBills(QDate(2026, 3, 3), c.card, {{billId, usd(1)}}, {}, {}, nullptr, true).code,
                 ErrorCode::Invalid);

        const QList<QueuedCheck> waiting = queue.queue(c.checking);
        QCOMPARE(waiting.size(), 2);
        QCOMPARE(waiting.at(0).docId, a.id);
        QCOMPARE(waiting.at(1).docId, payment);
        QCOMPARE(waiting.at(1).amount.minor(), 30000);

        // Print both from 1043: consecutive numbers, the queue empties, the
        // next number moves on, the postings are the same under the number.
        const qint64 bankBefore = c.balance(str("1000"));
        QList<QueuedCheck> numbered;
        const Result printed = queue.assignNumbers(c.checking, {a.id, payment}, 1043, &numbered);
        QVERIFY2(printed.ok, qPrintable(printed.error));
        QCOMPARE(numbered.at(0).number, str("1043"));
        QCOMPARE(c.number(payment), str("1044"));
        QCOMPARE(c.postedNumber(a.id), str("1043"));
        QCOMPARE(c.postedNumber(payment), str("1044"));
        QVERIFY(queue.queue(c.checking).isEmpty());
        QCOMPARE(queue.nextNumber(c.checking), 1045);
        QCOMPARE(c.balance(str("1000")), bankBefore);
        QCOMPARE(Documents(c.f()).openBalance(billId).minor(), 0); // the payment still pays the bill
        QCOMPARE(Ledger(c.f()).transactionsForDocument(a.id).size(), 2); // the first one void, replaced
        QVERIFY(c.f().integrityProblems().isEmpty());
    }

    void aMisprintGoesBackAndItsNumberIsRecordedVoid()
    {
        Office c;
        CheckQueue queue(c.f());
        const qint64 a = c.check(12000, QDate(2026, 3, 2), true).id;
        const qint64 b = c.check(8000, QDate(2026, 3, 2), true).id;
        QVERIFY(queue.assignNumbers(c.checking, {a, b}, 2001).ok);
        // b jammed: back in the queue, 2002 recorded void.
        QVERIFY(queue.confirmPrinted({b}, QDate(2026, 3, 31)).ok);
        QCOMPARE(c.number(b), QString());
        QVERIFY(Documents(c.f()).load(b)->toPrint);
        QCOMPARE(c.number(a), str("2001"));
        DocumentFilter voids{{DocKind::Check}, 0, {}, {}, true, true};
        QList<BusinessDocument> spoiled;
        for (const BusinessDocument &d : Documents(c.f()).list(voids)) {
            if (d.state == DocState::Void) {
                spoiled << d;
            }
        }
        QCOMPARE(spoiled.size(), 1);
        QCOMPARE(spoiled.first().number, str("2002"));
        QCOMPARE(spoiled.first().sourceDocId, b);
        QCOMPARE(spoiled.first().accountId, c.checking);
        QCOMPARE(spoiled.first().partyId, c.pixel);
        QCOMPARE(spoiled.first().total.minor(), 0);
        // The reprint takes the next number; 2002 is never handed out again.
        QCOMPARE(queue.nextNumber(c.checking), 2003);
        QCOMPARE(queue.assignNumbers(c.checking, {b}, 2002).code, ErrorCode::Duplicate);
        QVERIFY(queue.assignNumbers(c.checking, {b}, queue.nextNumber(c.checking)).ok);
        QCOMPARE(c.number(b), str("2003"));
        QCOMPARE(c.balance(str("1000")), 500000 - 20000);
        QVERIFY(c.f().integrityProblems().isEmpty());
    }

    void aCancelledRunIsTakenBack()
    {
        Office c;
        CheckQueue queue(c.f());
        const qint64 a = c.check(12000, QDate(2026, 3, 2), true).id;
        const qint64 b = c.check(8000, QDate(2026, 3, 2), true).id;
        QVERIFY(queue.assignNumbers(c.checking, {a, b}, 3001).ok);
        QCOMPARE(queue.nextNumber(c.checking), 3003);
        QVERIFY(queue.takeBack(c.checking, {a, b}).ok);
        QCOMPARE(queue.queue(c.checking).size(), 2);
        QCOMPARE(c.number(a), QString());
        QCOMPARE(queue.nextNumber(c.checking), 3001); // the numbers were never used
        QCOMPARE(Documents(c.f()).list({{DocKind::Check}, 0, {}, {}, true, true}).size(), 2); // nothing recorded void
    }

    void refusals()
    {
        Office c;
        CheckQueue queue(c.f());
        const qint64 a = c.check(12000, QDate(2026, 3, 2), true).id;
        QVERIFY(c.check(100, QDate(2026, 3, 1), false, str("4002")).ok);
        // A number already used on the account refuses the whole run.
        const qint64 b = c.check(8000, QDate(2026, 3, 3), true).id;
        QCOMPARE(queue.assignNumbers(c.checking, {a, b}, 4001).code, ErrorCode::Duplicate);
        QCOMPARE(c.number(a), QString()); // nothing changed
        QCOMPARE(queue.queue(c.checking).size(), 2);
        // Not queued, wrong account, nothing chosen, no first number.
        QCOMPARE(queue.assignNumbers(c.card, {a}, 5001).code, ErrorCode::Invalid);
        QCOMPARE(queue.assignNumbers(c.checking, {}, 5001).code, ErrorCode::Invalid);
        QCOMPARE(queue.assignNumbers(c.checking, {a}, 0).code, ErrorCode::Invalid);
        // A check that has cleared the bank keeps its number.
        QVERIFY(Sql::exec(c.f().database(), str("UPDATE journal_lines SET cleared = 1 WHERE txn_id = ?"),
                          {Documents(c.f()).load(b)->txnId}));
        QCOMPARE(queue.assignNumbers(c.checking, {b}, 5001).code, ErrorCode::Reconciled);
        // The lock date applies to the re-posting.
        QVERIFY(c.f().setLockDate(QDate(2026, 3, 2)).ok);
        QCOMPARE(queue.assignNumbers(c.checking, {a}, 5001).code, ErrorCode::ClosedPeriod);
        // Queue and unqueue by hand.
        QVERIFY(queue.setToPrint(a, false).ok);
        QVERIFY(queue.queue(c.checking).size() == 1);
        QVERIFY(!CheckQueue::canQueue(c.f(), 999999));
    }
};

QTEST_GUILESS_MAIN(TstBooksChecks)
#include "tst_books_checks.moc"
