// SPDX-License-Identifier: GPL-3.0-or-later
// The purchases cycle: bills, paying several bills at once, expenses /
// checks / credit-card charges, vendor credits, A/P balances.
#include "books_fixture.h"

using namespace BooksTest;

class TstBooksPurchases : public QObject {
    Q_OBJECT
private slots:
    void purchasesCycle()
    {
        TestCompany c;
        CompanyFile &f = c.f();
        Purchases purchases(f);
        Documents documents(f);
        const qint64 checking = c.account(QStringLiteral("1000"));
        const qint64 card = c.account(QStringLiteral("2100"));
        const qint64 rentAcct = c.account(QStringLiteral("6500"));
        const qint64 landlord = c.vendor(QStringLiteral("Landlord"), rentAcct, c.terms(QStringLiteral("Net 15")));
        const qint64 printer = c.vendor(QStringLiteral("PrintCo"), c.account(QStringLiteral("5000")));
        {
            Party p = *Lists(f).party(printer);
            p.is1099 = true;
            QVERIFY(Lists(f).updateParty(p).ok);
            QVERIFY(Lists(f).party(printer)->is1099);
        }

        BusinessDocument rent;
        rent.partyId = landlord;
        rent.date = QDate(2026, 3, 1);
        rent.reference = QStringLiteral("L-0301");
        rent.lines = {accountLine(0, 180000, QStringLiteral("March rent"))};
        const Result b1 = purchases.createBill(rent);
        QVERIFY2(b1.ok, qPrintable(b1.error));
        QCOMPARE(documents.load(b1.id)->dueDate, QDate(2026, 3, 16));
        QCOMPARE(documents.load(b1.id)->lines.first().accountId, rentAcct); // vendor default

        BusinessDocument print;
        print.partyId = printer;
        print.date = QDate(2026, 3, 3);
        print.lines = {accountLine(0, 45000, QStringLiteral("Posters")), accountLine(c.account(QStringLiteral("6000")), 5000,
                                                                                        QStringLiteral("Flyers"))};
        const Result b2 = purchases.createBill(print);
        QVERIFY(b2.ok);
        QCOMPARE(documents.load(b2.id)->total.minor(), 50000);
        QCOMPARE(c.roleBalance(SystemRole::AccountsPayable), 230000);
        QCOMPARE(purchases.vendorBalance(printer).minor(), 50000);
        QCOMPARE(purchases.unpaidBills().size(), 2);

        // A vendor credit reduces what we owe PrintCo.
        BusinessDocument credit;
        credit.partyId = printer;
        credit.date = QDate(2026, 3, 5);
        credit.lines = {accountLine(c.account(QStringLiteral("5000")), 10000, QStringLiteral("Misprint"))};
        const Result vc = purchases.createVendorCredit(credit);
        QVERIFY(vc.ok);
        QCOMPARE(purchases.vendorBalance(printer).minor(), 40000);
        QCOMPARE(purchases.applyVendorCredit(vc.id, b2.id, usd(10001), QDate(2026, 3, 6)).code, ErrorCode::OverApplied);
        QVERIFY(purchases.applyVendorCredit(vc.id, b2.id, usd(10000), QDate(2026, 3, 6)).ok);
        QCOMPARE(documents.openBalance(b2.id).minor(), 40000);

        // Pay both bills in one run: one payment per vendor.
        QList<qint64> paymentIds;
        QCOMPARE(purchases.payBills(QDate(2026, 3, 10), checking,
                                    {PaymentApplication{b1.id, usd(180001)}}).code,
                 ErrorCode::OverApplied);
        const Result paid = purchases.payBills(QDate(2026, 3, 10), checking,
                                               {PaymentApplication{b1.id, usd(180000)}, PaymentApplication{b2.id, usd(25000)}},
                                               QStringLiteral("chk 2001"), {}, &paymentIds);
        QVERIFY2(paid.ok, qPrintable(paid.error));
        QCOMPARE(paymentIds.size(), 2);
        QCOMPARE(documents.openBalance(b1.id).minor(), 0);
        QCOMPARE(documents.openBalance(b2.id).minor(), 15000);
        QCOMPARE(documents.status(*documents.load(b2.id), QDate(2026, 3, 11)), DocumentStatus::Partial);
        QCOMPARE(documents.status(*documents.load(b1.id), QDate(2026, 3, 11)), DocumentStatus::Paid);
        QCOMPARE(c.roleBalance(SystemRole::AccountsPayable), 15000);
        QCOMPARE(c.balance(QStringLiteral("1000")), -205000);
        QCOMPARE(purchases.unpaidBills(printer).size(), 1);
        QCOMPARE(purchases.unpaidBills(0, QDate(2026, 3, 9)).size(), 2); // as of before the payment

        // Expense / check / card charge.
        BusinessDocument lunch;
        lunch.date = QDate(2026, 3, 12);
        lunch.accountId = card;
        lunch.kind = DocKind::CreditCardCharge;
        lunch.lines = {accountLine(c.account(QStringLiteral("6300")), 4250, QStringLiteral("Client lunch"))};
        QVERIFY(purchases.createExpense(lunch).ok);
        BusinessDocument check;
        check.date = QDate(2026, 3, 13);
        check.accountId = checking;
        check.kind = DocKind::Check;
        check.reference = QStringLiteral("2002");
        check.lines = {accountLine(c.account(QStringLiteral("6700")), 75000, QStringLiteral("Accountant"))};
        const Result chk = purchases.createExpense(check);
        QVERIFY(chk.ok);
        QCOMPARE(Documents(f).load(chk.id)->kind, DocKind::Check);
        QCOMPARE(c.balance(QStringLiteral("2100")), 4250); // owed on the card
        QCOMPARE(c.balance(QStringLiteral("6300")), 4250);
        QCOMPARE(c.balance(QStringLiteral("1000")), -280000);
        BusinessDocument noPayAccount = lunch;
        noPayAccount.accountId = c.account(QStringLiteral("6500"));
        QCOMPARE(purchases.createExpense(noPayAccount).code, ErrorCode::UnknownAccount);
        BusinessDocument noVendor = rent;
        noVendor.partyId = 0;
        QCOMPARE(purchases.createBill(noVendor).code, ErrorCode::UnknownParty);

        // Editing a paid bill below its payments is refused; above is fine.
        BusinessDocument edit = *documents.load(b2.id);
        edit.lines = {accountLine(c.account(QStringLiteral("5000")), 20000, QStringLiteral("Posters"))};
        QCOMPARE(purchases.updateBill(edit, QStringLiteral("price change")).code, ErrorCode::OverApplied);
        edit.lines = {accountLine(c.account(QStringLiteral("5000")), 60000, QStringLiteral("Posters"))};
        QVERIFY(purchases.updateBill(edit, QStringLiteral("price change")).ok);
        QCOMPARE(documents.openBalance(b2.id).minor(), 25000);
        QVERIFY(f.integrityProblems().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstBooksPurchases)
#include "tst_books_purchases.moc"
