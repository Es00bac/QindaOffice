// SPDX-License-Identifier: GPL-3.0-or-later
// Banking: registers, transfers, deposits, the bank feed (dedup, rules,
// matching, categorizing, exclusion) and reconciliation (finish only at
// zero difference, undo, the report, reconciled lines lock their
// transaction).
#include "books_fixture.h"
#include "reconciliation.h"

using namespace BooksTest;

class TstBooksBanking : public QObject {
    Q_OBJECT
private slots:
    void transfersAndRegister()
    {
        TestCompany c;
        Banking banking(c.f());
        const qint64 checking = c.account(QStringLiteral("1000"));
        const qint64 savings = c.account(QStringLiteral("1010"));
        QVERIFY(ChartOfAccounts(c.f()).setOpeningBalance(checking, usd(100000), QDate(2026, 1, 1)).ok);
        QVERIFY(banking.transfer(checking, savings, usd(25000), QDate(2026, 1, 5), QStringLiteral("to savings")).ok);
        QCOMPARE(banking.transfer(checking, checking, usd(1), QDate(2026, 1, 5)).code, ErrorCode::UnknownAccount);
        QCOMPARE(banking.transfer(checking, savings, usd(-1), QDate(2026, 1, 5)).code, ErrorCode::Invalid);
        QCOMPARE(c.balance(QStringLiteral("1000")), 75000);
        QCOMPARE(c.balance(QStringLiteral("1010")), 25000);
        const QList<RegisterEntry> reg = banking.registerFor(checking);
        QCOMPARE(reg.size(), 2);
        QCOMPARE(reg[0].increase.minor(), 100000);
        QCOMPARE(reg[1].decrease.minor(), 25000);
        QCOMPARE(reg[1].balance.minor(), 75000);
        QCOMPARE(reg[1].otherAccount, QStringLiteral("Savings"));
        QCOMPARE(reg[1].kind, DocKind::Transfer);
        // A register from a later date starts at the running balance.
        const QList<RegisterEntry> later = banking.registerFor(checking, QDate(2026, 1, 3));
        QCOMPARE(later.size(), 1);
        QCOMPARE(later[0].balance.minor(), 75000);
        // Credit-card registers run in the card's normal (owed) sign.
        const qint64 card = c.account(QStringLiteral("2100"));
        BusinessDocument charge;
        charge.kind = DocKind::CreditCardCharge;
        charge.date = QDate(2026, 1, 6);
        charge.accountId = card;
        charge.lines = {accountLine(c.account(QStringLiteral("6400")), 3000)};
        QVERIFY(Purchases(c.f()).createExpense(charge).ok);
        QVERIFY(banking.transfer(checking, card, usd(1000), QDate(2026, 1, 7), QStringLiteral("card payment")).ok);
        const QList<RegisterEntry> cardReg = banking.registerFor(card);
        QCOMPARE(cardReg[0].increase.minor(), 3000);
        QCOMPARE(cardReg[1].decrease.minor(), 1000);
        QCOMPARE(cardReg[1].balance.minor(), 2000);
    }

    void depositsFromUndepositedFunds()
    {
        TestCompany c;
        const qint64 acme = c.customer(QStringLiteral("Acme"));
        Sales sales(c.f());
        const Result p1 = sales.receivePayment({acme, QDate(2026, 2, 1), usd(10000), 0, {}, {}, {}});
        const Result p2 = sales.receivePayment({acme, QDate(2026, 2, 2), usd(2550), 0, {}, {}, {}});
        QVERIFY(p1.ok && p2.ok);
        Banking banking(c.f());
        QCOMPARE(banking.undepositedPayments(), (QList<qint64>{p1.id, p2.id}));
        const qint64 checking = c.account(QStringLiteral("1000"));
        QCOMPARE(banking.deposit(c.account(QStringLiteral("6500")), QDate(2026, 2, 3), {p1.id}).code,
                 ErrorCode::UnknownAccount);
        const Result dep = banking.deposit(checking, QDate(2026, 2, 3), {p1.id, p2.id}, QStringLiteral("branch deposit"));
        QVERIFY2(dep.ok, qPrintable(dep.error));
        QCOMPARE(c.balance(QStringLiteral("1000")), 12550);
        QCOMPARE(c.roleBalance(SystemRole::UndepositedFunds), 0);
        QVERIFY(banking.undepositedPayments().isEmpty());
        QCOMPARE(banking.deposit(checking, QDate(2026, 2, 4), {p1.id}).code, ErrorCode::NotFound); // no double deposit
        // Voiding the deposit puts the payments back in the queue.
        QVERIFY(Ledger(c.f()).voidDocument(dep.id, QStringLiteral("wrong account")).ok);
        QCOMPARE(banking.undepositedPayments().size(), 2);
    }

    void feedDedupRulesAndMatching()
    {
        TestCompany c;
        Banking banking(c.f());
        const qint64 checking = c.account(QStringLiteral("1000"));
        const qint64 meals = c.account(QStringLiteral("6300"));
        const qint64 software = c.account(QStringLiteral("6400"));
        const QList<BankLine> statement{
            {QDate(2026, 3, 2), -450, QStringLiteral("BLUE BOTTLE COFFEE #12"), {}, {}, {}},
            {QDate(2026, 3, 2), -450, QStringLiteral("BLUE BOTTLE COFFEE #12"), {}, {}, {}}, // a second real coffee
            {QDate(2026, 3, 3), -2999, QStringLiteral("ADOBE *CREATIVE CLOUD"), QStringLiteral("subscription"),
             QStringLiteral("FIT-778"), {}},
            {QDate(2026, 3, 5), 150000, QStringLiteral("ACME CORP PAYMENT"), {}, QStringLiteral("FIT-779"), {}},
            {QDate(2026, 3, 6), -12000, QStringLiteral("CHECK 1044"), {}, {}, QStringLiteral("1044")},
        };
        const ImportSummary first = banking.importLines(checking, statement, QStringLiteral("march.csv"));
        QCOMPARE(first.added, 5);
        QCOMPARE(first.duplicates, 0);
        const ImportSummary again = banking.importLines(checking, statement, QStringLiteral("march.csv"));
        QCOMPARE(again.added, 0);
        QCOMPARE(again.duplicates, 5);
        // FITID wins over content: same FITID with other text is a duplicate.
        QList<BankLine> renamed{statement[2]};
        renamed[0].payee = QStringLiteral("Adobe Systems");
        QCOMPARE(banking.importLines(checking, renamed).duplicates, 1);
        QCOMPARE(banking.feed(checking).size(), 5);

        // Rules: payee text + direction + amount range, priority wins.
        BankRule coffee{0, QStringLiteral("Coffee"), 1, QStringLiteral("coffee"), {}, 1000, RuleDirection::MoneyOut, 0,
                        meals, 0, 0, {}, true};
        BankRule adobe{0, QStringLiteral("Adobe"), 5, QStringLiteral("adobe"), {}, {}, RuleDirection::MoneyOut, checking,
                       software, 0, 0, {}, true};
        BankRule catchAll{0, QStringLiteral("Anything out"), 0, {}, {}, {}, RuleDirection::MoneyOut, 0,
                          c.account(QStringLiteral("6990")), 0, 0, {}, true};
        QVERIFY(banking.createRule(coffee).ok && banking.createRule(adobe).ok && banking.createRule(catchAll).ok);
        QCOMPARE(banking.applyRules(checking), 4); // everything out; the incoming payment has no rule
        QList<BankFeedEntry> lines = banking.feed(checking, FeedStatus::New);
        QCOMPARE(lines[0].suggestedAccountId, meals);
        QCOMPARE(lines[2].suggestedAccountId, software);
        QCOMPARE(lines[3].suggestedAccountId, 0);
        QCOMPARE(lines[4].suggestedAccountId, c.account(QStringLiteral("6990"))); // the 120.00 check is above coffee's max

        // Accept the rule suggestions for the coffees and Adobe.
        for (int i : {0, 1, 2}) {
            const Result r = banking.acceptSuggestion(lines[i].id);
            QVERIFY2(r.ok, qPrintable(r.error));
        }
        QCOMPARE(c.balance(QStringLiteral("6300")), 900);
        QCOMPARE(c.balance(QStringLiteral("6400")), 2999);
        QCOMPARE(banking.feedEntry(lines[0].id)->status, FeedStatus::Categorized);

        // Matching: the customer payment already exists in the books.
        const qint64 acme = c.customer(QStringLiteral("Acme Corp"));
        const Result pay = Sales(c.f()).receivePayment({acme, QDate(2026, 3, 4), usd(150000), checking, {}, {}, {}});
        QVERIFY(pay.ok);
        const qint64 decoyCustomer = c.customer(QStringLiteral("Zeta Industries"));
        QVERIFY(Sales(c.f()).receivePayment({decoyCustomer, QDate(2026, 3, 14), usd(150000), checking, {}, {}, {}}).ok);
        const QList<MatchSuggestion> suggestions = banking.suggestMatches(lines[3].id);
        QCOMPARE(suggestions.size(), 2);
        QCOMPARE(suggestions.first().docId, pay.id); // closer date + payee similarity
        QVERIFY(suggestions.first().score > suggestions.last().score);
        QVERIFY(banking.match(lines[3].id, suggestions.first().journalLineId).ok);
        QCOMPARE(banking.feedEntry(lines[3].id)->status, FeedStatus::Matched);
        QVERIFY(banking.suggestMatches(lines[3].id).size() == 1); // the matched line is cleared now
        QCOMPARE(banking.match(lines[3].id, suggestions.last().journalLineId).code, ErrorCode::Invalid);

        // Exclude the check (it was already entered elsewhere), then restore.
        QVERIFY(banking.exclude(lines[4].id).ok);
        QCOMPARE(banking.feed(checking, FeedStatus::Excluded).size(), 1);
        QVERIFY(banking.restore(lines[4].id).ok);
        const Result cat = banking.categorize(lines[4].id, c.account(QStringLiteral("6600")), 0, 0, QStringLiteral("Plumber"));
        QVERIFY(cat.ok);
        QCOMPARE(Documents(c.f()).load(cat.id)->kind, DocKind::Check); // it had a check number
        QVERIFY(banking.feed(checking, FeedStatus::New).isEmpty());
        // Similarity helpers.
        QCOMPARE(Banking::payeeSimilarity(QStringLiteral("ACME CORP PAYMENT"), QStringLiteral("Acme Corp")), 100);
        QCOMPARE(Banking::payeeSimilarity(QStringLiteral("Foo"), QStringLiteral("Bar")), 0);
        QCOMPARE(Banking::normalizePayee(QStringLiteral("  BLUE-Bottle #12 ")), QStringLiteral("blue bottle"));
    }

    void reconciliation()
    {
        TestCompany c;
        const qint64 checking = c.account(QStringLiteral("1000"));
        QVERIFY(ChartOfAccounts(c.f()).setOpeningBalance(checking, usd(100000), QDate(2026, 3, 31)).ok);
        Purchases purchases(c.f());
        auto spend = [&](const QDate &d, qint64 cents) {
            BusinessDocument e;
            e.date = d;
            e.accountId = checking;
            e.lines = {accountLine(c.account(QStringLiteral("6100")), cents)};
            return purchases.createExpense(e).id;
        };
        const qint64 fee1 = spend(QDate(2026, 4, 3), 1500);
        spend(QDate(2026, 4, 20), 2500);
        spend(QDate(2026, 5, 2), 999); // after the statement date: not a candidate
        Reconciliation rec(c.f());
        const Result session = rec.start(checking, QDate(2026, 4, 30), usd(98500));
        QVERIFY2(session.ok, qPrintable(session.error));
        QCOMPARE(rec.start(checking, QDate(2026, 4, 30), usd(1)).code, ErrorCode::Duplicate);
        const QList<ReconcileCandidate> candidates = rec.candidates(session.id);
        QCOMPARE(candidates.size(), 3); // opening balance + two April fees
        QCOMPARE(rec.session(session.id)->beginningBalance.minor(), 0);
        // Mark the opening balance and the first fee: 1,000.00 − 15.00 = 985.00.
        QList<qint64> marks;
        for (const ReconcileCandidate &cand : candidates) {
            if (cand.amount.minor() != -2500) { // the 25.00 fee is not on this statement
                marks << cand.lineId;
            }
        }
        QCOMPARE(marks.size(), 2);
        QVERIFY(rec.mark(session.id, {marks.first()}, true).ok);
        QCOMPARE(rec.difference(session.id).minor(), 98500 - 100000);
        QCOMPARE(rec.finish(session.id).code, ErrorCode::NotZero); // not until it is zero
        QVERIFY(rec.mark(session.id, marks, true).ok);
        QCOMPARE(rec.clearedBalance(session.id).minor(), 98500);
        QCOMPARE(rec.difference(session.id).minor(), 0);
        const ReportTable before = rec.report(session.id);
        QCOMPARE(before.money(QStringLiteral("difference"), QStringLiteral("amount")), 0);
        QVERIFY(rec.finish(session.id).ok);
        QCOMPARE(rec.session(session.id)->state, ReconciliationState::Finished);

        const ReportTable report = rec.report(session.id);
        QCOMPARE(report.money(QStringLiteral("beginning"), QStringLiteral("amount")), 0);
        QCOMPARE(report.money(QStringLiteral("cleared_in"), QStringLiteral("amount")), 100000);
        QCOMPARE(report.money(QStringLiteral("cleared_out"), QStringLiteral("amount")), -1500);
        QCOMPARE(report.money(QStringLiteral("cleared_balance"), QStringLiteral("amount")), 98500);
        QCOMPARE(report.money(QStringLiteral("register_balance"), QStringLiteral("amount")), 96000);

        // Reconciled lines lock their transaction.
        QCOMPARE(Ledger(c.f()).voidDocument(fee1, QStringLiteral("oops")).code, ErrorCode::Reconciled);

        // Next month starts from the reconciled balance.
        const Result may = rec.start(checking, QDate(2026, 5, 31), usd(95001));
        QVERIFY(may.ok);
        QCOMPARE(rec.session(may.id)->beginningBalance.minor(), 98500);
        QCOMPARE(rec.candidates(may.id).size(), 2);
        QCOMPARE(rec.undoLast(checking).code, ErrorCode::Invalid); // one is in progress
        QVERIFY(rec.cancel(may.id).ok);
        QVERIFY(rec.undoLast(checking).ok);
        QCOMPARE(rec.session(session.id)->state, ReconciliationState::Undone);
        QVERIFY(Ledger(c.f()).voidDocument(fee1, QStringLiteral("now allowed")).ok);
        QCOMPARE(rec.history(checking).size(), 1);
        QCOMPARE(rec.undoLast(checking).code, ErrorCode::NotFound);
    }
};

QTEST_GUILESS_MAIN(TstBooksBanking)
#include "tst_books_banking.moc"
