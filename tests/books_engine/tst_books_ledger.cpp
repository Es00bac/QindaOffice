// SPDX-License-Identifier: GPL-3.0-or-later
// The journal: balanced postings, the lock date and its audited override,
// void and edit (void + repost) with the audit trail, and immutability
// enforced by the file itself.
#include "books_fixture.h"
#include "sql_util.h"

#include <QJsonDocument>
#include <QJsonObject>

using namespace BooksTest;

class TstBooksLedger : public QObject {
    Q_OBJECT
private slots:
    void balancedAndUnbalanced()
    {
        TestCompany c;
        Ledger ledger(c.f());
        const qint64 bank = c.account(QStringLiteral("1000"));
        const qint64 equity = c.account(QStringLiteral("3100"));
        const qint64 rent = c.account(QStringLiteral("6500"));
        JournalEntryInput ok{QDate(2026, 1, 1), QStringLiteral("JE-1"), QStringLiteral("capital"),
                             {debit(bank, 100000), credit(equity, 100000)}};
        const Result posted = ledger.postJournalEntry(ok);
        QVERIFY2(posted.ok, qPrintable(posted.error));
        QCOMPARE(c.balance(QStringLiteral("1000")), 100000);
        QCOMPARE(c.balance(QStringLiteral("3100")), 100000); // normal sign: credit-positive

        JournalEntryInput unbalanced{QDate(2026, 1, 2), {}, {}, {debit(rent, 5000), credit(bank, 4999)}};
        QCOMPARE(ledger.postJournalEntry(unbalanced).code, ErrorCode::Unbalanced);
        JournalEntryInput single{QDate(2026, 1, 2), {}, {}, {debit(rent, 5000)}};
        QCOMPARE(ledger.postJournalEntry(single).code, ErrorCode::Invalid);
        JournalEntryInput unknown{QDate(2026, 1, 2), {}, {}, {debit(99999, 5000), credit(bank, 5000)}};
        QCOMPARE(ledger.postJournalEntry(unknown).code, ErrorCode::UnknownAccount);
        JournalLine both = debit(rent, 10);
        both.credit = usd(10);
        JournalEntryInput twoSided{QDate(2026, 1, 2), {}, {}, {both, credit(bank, 0)}};
        QVERIFY(!ledger.postJournalEntry(twoSided).ok);
        JournalLine euro;
        euro.accountId = rent;
        euro.debit = Money(500, QStringLiteral("EUR"));
        JournalEntryInput foreign{QDate(2026, 1, 2), {}, {}, {euro, credit(bank, 500)}};
        QCOMPARE(ledger.postJournalEntry(foreign).code, ErrorCode::CurrencyMismatch);
        // A multi-line entry.
        JournalEntryInput multi{QDate(2026, 1, 3), {}, {},
                                {debit(rent, 30000), debit(c.account(QStringLiteral("6900")), 5000), credit(bank, 35000)}};
        QVERIFY(ledger.postJournalEntry(multi).ok);
        QCOMPARE(c.balance(QStringLiteral("1000")), 65000);
        // Nothing unbalanced ever reached the ledger.
        QVERIFY(c.f().integrityProblems().isEmpty());
        QCOMPARE(Sql::scalarInt(c.f().database(), QStringLiteral("SELECT COUNT(*) FROM transactions")), 2);
    }

    void lockDateAndOverride()
    {
        TestCompany c;
        Ledger ledger(c.f());
        const qint64 bank = c.account(QStringLiteral("1000"));
        const qint64 equity = c.account(QStringLiteral("3100"));
        QVERIFY(c.f().setLockDate(QDate(2025, 12, 31), QStringLiteral("2025 closed")).ok);
        JournalEntryInput late{QDate(2025, 12, 31), {}, {}, {debit(bank, 100), credit(equity, 100)}};
        QCOMPARE(ledger.postJournalEntry(late).code, ErrorCode::ClosedPeriod);
        PostOptions noReason;
        noReason.overrideLock = true;
        QCOMPARE(ledger.postJournalEntry(late, noReason).code, ErrorCode::ClosedPeriod);
        PostOptions withReason{true, QStringLiteral("auditor adjustment AJE-3")};
        const Result r = ledger.postJournalEntry(late, withReason);
        QVERIFY2(r.ok, qPrintable(r.error));
        const QList<TransactionView> txns = ledger.transactionsForDocument(r.id);
        QCOMPARE(txns.size(), 1);
        QCOMPARE(txns.first().lockOverrideReason, QStringLiteral("auditor adjustment AJE-3"));
        const QList<AuditEntry> overrides = ledger.auditTrail(QStringLiteral("transaction"), txns.first().id);
        QCOMPARE(overrides.size(), 1);
        QCOMPARE(overrides.first().action, QStringLiteral("lock-override"));
        QCOMPARE(overrides.first().reason, QStringLiteral("auditor adjustment AJE-3"));
        // After the lock date is fine.
        JournalEntryInput open{QDate(2026, 1, 1), {}, {}, {debit(bank, 100), credit(equity, 100)}};
        QVERIFY(ledger.postJournalEntry(open).ok);
        // Voiding something in the closed period is refused too.
        QCOMPARE(ledger.voidDocument(r.id, QStringLiteral("oops")).code, ErrorCode::ClosedPeriod);
        QCOMPARE(ledger.auditTrail(QStringLiteral("settings")).last().action, QStringLiteral("lock"));
    }

    void voidKeepsTheTrail()
    {
        TestCompany c;
        Ledger ledger(c.f());
        const qint64 bank = c.account(QStringLiteral("1000"));
        const qint64 rent = c.account(QStringLiteral("6500"));
        JournalEntryInput je{QDate(2026, 2, 1), QStringLiteral("JE-9"), QStringLiteral("rent"),
                             {debit(rent, 150000), credit(bank, 150000)}};
        const Result r = ledger.postJournalEntry(je);
        QVERIFY(r.ok);
        QCOMPARE(c.balance(QStringLiteral("6500")), 150000);
        QVERIFY(ledger.voidDocument(r.id, QStringLiteral("posted twice")).ok);
        QCOMPARE(c.balance(QStringLiteral("6500")), 0); // zero effect
        const QList<TransactionView> history = ledger.transactionsForDocument(r.id);
        QCOMPARE(history.size(), 1);
        QVERIFY(history.first().voided);
        QCOMPARE(history.first().voidReason, QStringLiteral("posted twice"));
        QCOMPARE(history.first().lines.size(), 2); // the lines are still there
        QCOMPARE(Documents(c.f()).load(r.id)->state, DocState::Void);
        const QList<AuditEntry> trail = ledger.auditTrail(QStringLiteral("document"), r.id);
        QCOMPARE(trail.size(), 2); // post + void
        QCOMPARE(trail.last().action, QStringLiteral("void"));
        QVERIFY(trail.last().beforeJson.contains(QStringLiteral("1500.00")));
        QCOMPARE(ledger.voidDocument(r.id, QStringLiteral("again")).code, ErrorCode::AlreadyVoided);
    }

    void editIsVoidAndRepost()
    {
        TestCompany c;
        Ledger ledger(c.f());
        const qint64 bank = c.account(QStringLiteral("1000"));
        const qint64 rent = c.account(QStringLiteral("6500"));
        const qint64 utilities = c.account(QStringLiteral("6900"));
        const Result r = ledger.postJournalEntry({QDate(2026, 3, 1), {}, {}, {debit(rent, 1000), credit(bank, 1000)}});
        QVERIFY(r.ok);
        const Result e = ledger.replaceJournalEntry(
            r.id, {QDate(2026, 3, 2), {}, QStringLiteral("fixed"), {debit(utilities, 1200), credit(bank, 1200)}},
            QStringLiteral("wrong account and amount"));
        QVERIFY2(e.ok, qPrintable(e.error));
        QCOMPARE(e.id, r.id); // the document keeps its identity
        QCOMPARE(c.balance(QStringLiteral("6500")), 0);
        QCOMPARE(c.balance(QStringLiteral("6900")), 1200);
        const QList<TransactionView> history = ledger.transactionsForDocument(r.id);
        QCOMPARE(history.size(), 2);
        QVERIFY(history[0].voided);
        QCOMPARE(history[0].replacedBy, history[1].id);
        QVERIFY(!history[1].voided);
        const AuditEntry edit = ledger.auditTrail(QStringLiteral("document"), r.id).last();
        QCOMPARE(edit.action, QStringLiteral("edit"));
        QCOMPARE(edit.reason, QStringLiteral("wrong account and amount"));
        QVERIFY(!edit.beforeJson.isEmpty() && !edit.afterJson.isEmpty());
        // A failed edit changes nothing (one SQL transaction).
        const Result bad = ledger.replaceJournalEntry(
            r.id, {QDate(2026, 3, 2), {}, {}, {debit(utilities, 1), credit(bank, 2)}}, QStringLiteral("typo"));
        QCOMPARE(bad.code, ErrorCode::Unbalanced);
        QCOMPARE(ledger.transactionsForDocument(r.id).size(), 2);
        QCOMPARE(c.balance(QStringLiteral("6900")), 1200);
    }

    void postedLinesAreImmutableInTheFile()
    {
        TestCompany c;
        const Result r = Ledger(c.f()).postJournalEntry(
            {QDate(2026, 1, 1), {}, {}, {debit(c.account(QStringLiteral("1000")), 10), credit(c.account(QStringLiteral("3100")), 10)}});
        QVERIFY(r.ok);
        const QSqlDatabase db = c.f().database();
        QString error;
        QVERIFY(!Sql::exec(db, QStringLiteral("UPDATE journal_lines SET debit = 999 WHERE debit = 10"), {}, &error));
        QVERIFY2(error.contains(QStringLiteral("immutable")), qPrintable(error));
        QVERIFY(!Sql::exec(db, QStringLiteral("DELETE FROM journal_lines"), {}, &error));
        QVERIFY(!Sql::exec(db, QStringLiteral("UPDATE transactions SET date = '2020-01-01'"), {}, &error));
        QVERIFY(!Sql::exec(db, QStringLiteral("DELETE FROM audit_log"), {}, &error));
        QVERIFY(!Sql::exec(db, QStringLiteral("UPDATE audit_log SET user = 'someone else'"), {}, &error));
        QVERIFY(Ledger(c.f()).voidDocument(r.id, QStringLiteral("test")).ok);
        QVERIFY(!Sql::exec(db, QStringLiteral("UPDATE transactions SET voided = 0"), {}, &error)); // no un-void
        // Clearing state is not an amount: allowed.
        QVERIFY(Sql::exec(db, QStringLiteral("UPDATE journal_lines SET cleared = 1")));
    }

    void openingBalances()
    {
        TestCompany c;
        ChartOfAccounts chart(c.f());
        QVERIFY(chart.setOpeningBalance(c.account(QStringLiteral("1000")), usd(500000), QDate(2025, 12, 31)).ok);
        QVERIFY(chart.setOpeningBalance(c.account(QStringLiteral("2700")), usd(200000), QDate(2025, 12, 31)).ok);
        QCOMPARE(c.balance(QStringLiteral("1000")), 500000);
        QCOMPARE(c.balance(QStringLiteral("2700")), 200000);
        QCOMPARE(c.roleBalance(SystemRole::OpeningBalanceEquity), 300000);
        QCOMPARE(chart.setOpeningBalance(c.account(QStringLiteral("6500")), usd(1), QDate(2025, 12, 31)).code,
                 ErrorCode::Invalid); // only balance-sheet accounts
    }
};

QTEST_GUILESS_MAIN(TstBooksLedger)
#include "tst_books_ledger.moc"
