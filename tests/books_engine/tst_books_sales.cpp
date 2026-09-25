// SPDX-License-Identifier: GPL-3.0-or-later
// The sales cycle end to end with hand-computed numbers: estimate →
// invoice (discount + two-agency tax) → partial payment → overpayment →
// credit applied → credit memo → refunds; plus drafts, edits, voids,
// statements and recurring invoices.
#include "books_fixture.h"
#include "recurring.h"

using namespace BooksTest;

class TstBooksSales : public QObject {
    Q_OBJECT

    struct Setup {
        TestCompany c;
        qint64 acme = 0, design = 0, hosting = 0, state = 0, city = 0, metro = 0;
        Setup()
        {
            acme = c.customer(QStringLiteral("Acme Corp"));
            design = c.item(QStringLiteral("Design"), 100000, c.account(QStringLiteral("4100")), true);
            hosting = c.item(QStringLiteral("Hosting"), 5000, c.account(QStringLiteral("4000")), false);
            SalesTax tax(c.f());
            state = tax.createAgency({0, QStringLiteral("State"), {}, RoundingMode::HalfUp, true}).id;
            city = tax.createAgency({0, QStringLiteral("City"), {}, RoundingMode::HalfUp, true}).id;
            const qint64 s = tax.createComponent({0, state, QStringLiteral("State 6%"), *Decimal::parse(QStringLiteral("6%")), true}).id;
            const qint64 ci = tax.createComponent({0, city, QStringLiteral("City 1.25%"), *Decimal::parse(QStringLiteral("1.25%")), true}).id;
            metro = tax.createRate(QStringLiteral("Metro 7.25%"), {s, ci}).id;
        }
    };

private slots:
    void fullSalesCycle()
    {
        Setup s;
        CompanyFile &f = s.c.f();
        Sales sales(f);
        Documents documents(f);
        QCOMPARE(SalesTax(f).rate(s.metro)->combinedRate().toPercentString(), QStringLiteral("7.25%"));

        // Estimate: 2 × Design + 3 × Hosting, 10% discount, Metro tax.
        BusinessDocument estimate;
        estimate.partyId = s.acme;
        estimate.date = QDate(2026, 3, 25);
        estimate.taxRateId = s.metro;
        estimate.discount = Discount::ofPercent(*Decimal::parse(QStringLiteral("10%")));
        estimate.lines = {itemLine(s.design, QStringLiteral("2")), itemLine(s.hosting, QStringLiteral("3"))};
        const Result est = sales.saveEstimate(estimate);
        QVERIFY2(est.ok, qPrintable(est.error));
        QCOMPARE(documents.load(est.id)->number, QStringLiteral("EST-1001"));
        QCOMPARE(documents.load(est.id)->total.minor(), 206550);
        QCOMPARE(s.c.roleBalance(SystemRole::AccountsReceivable), 0); // estimates never post

        const Result inv = sales.convertEstimateToInvoice(est.id, QDate(2026, 4, 1));
        QVERIFY2(inv.ok, qPrintable(inv.error));
        const BusinessDocument invoice = *documents.load(inv.id);
        QCOMPARE(invoice.number, QStringLiteral("INV-1001"));
        QCOMPARE(invoice.sourceDocId, est.id);
        QCOMPARE(documents.load(est.id)->state, DocState::Closed);
        QCOMPARE(invoice.subtotal.minor(), 215000);
        QCOMPARE(invoice.discountAmount.minor(), 21500);
        QCOMPARE(invoice.taxAmount.minor(), 13050); // 6% + 1.25% of 1,800.00
        QCOMPARE(invoice.total.minor(), 206550);
        QCOMPARE(invoice.dueDate, QDate(2026, 5, 1)); // default terms Net 30
        const DocumentTotals totals = documents.computeTotals(invoice);
        QCOMPARE(totals.taxableBase.minor(), 180000);
        QCOMPARE(totals.taxes.size(), 2);
        QCOMPARE(totals.taxes[0].amount.minor(), 10800);
        QCOMPARE(totals.taxes[1].amount.minor(), 2250);
        QCOMPARE(s.c.roleBalance(SystemRole::AccountsReceivable), 206550);
        QCOMPARE(s.c.roleBalance(SystemRole::SalesTaxPayable), 13050);
        QCOMPARE(s.c.roleBalance(SystemRole::SalesDiscounts), -21500); // contra-income
        QCOMPARE(s.c.balance(QStringLiteral("4100")), 200000);
        QCOMPARE(s.c.balance(QStringLiteral("4000")), 15000);

        // Partial payment.
        ReceivePaymentInput p1{s.acme, QDate(2026, 4, 10), usd(100000), 0, QStringLiteral("chk 1001"), {},
                               {PaymentApplication{inv.id, usd(100000)}}};
        const Result pay1 = sales.receivePayment(p1);
        QVERIFY2(pay1.ok, qPrintable(pay1.error));
        QCOMPARE(documents.openBalance(inv.id).minor(), 106550);
        QCOMPARE(documents.status(*documents.load(inv.id), QDate(2026, 4, 11)), DocumentStatus::Partial);
        QCOMPARE(documents.status(*documents.load(inv.id), QDate(2026, 5, 2)), DocumentStatus::Overdue);

        // Over-applying is refused, whichever way it is attempted.
        ReceivePaymentInput tooMuch{s.acme, QDate(2026, 4, 11), usd(200000), 0, {}, {},
                                    {PaymentApplication{inv.id, usd(106551)}}};
        QCOMPARE(sales.receivePayment(tooMuch).code, ErrorCode::OverApplied);
        ReceivePaymentInput moreThanPaid{s.acme, QDate(2026, 4, 11), usd(100), 0, {}, {},
                                         {PaymentApplication{inv.id, usd(200)}}};
        QCOMPARE(sales.receivePayment(moreThanPaid).code, ErrorCode::OverApplied);

        // Overpayment: 1,200.00 pays the 1,065.50 balance, 134.50 is credit.
        ReceivePaymentInput p2{s.acme, QDate(2026, 4, 20), usd(120000), 0, {}, {},
                               {PaymentApplication{inv.id, usd(106550)}}};
        const Result pay2 = sales.receivePayment(p2);
        QVERIFY(pay2.ok);
        QCOMPARE(documents.status(*documents.load(inv.id), QDate(2026, 6, 1)), DocumentStatus::Paid);
        QCOMPARE(documents.openBalance(pay2.id).minor(), 13450);
        QCOMPARE(sales.openCredits(s.acme).size(), 1);
        QCOMPARE(sales.customerBalance(s.acme).minor(), -13450);

        // The credit pays a second invoice.
        BusinessDocument second;
        second.partyId = s.acme;
        second.date = QDate(2026, 4, 25);
        second.lines = {itemLine(s.hosting)};
        const Result inv2 = sales.createInvoice(second);
        QVERIFY(inv2.ok);
        QCOMPARE(documents.load(inv2.id)->number, QStringLiteral("INV-1002"));
        QCOMPARE(sales.applyCredit(pay2.id, inv2.id, usd(5001), QDate(2026, 4, 26)).code, ErrorCode::OverApplied);
        QVERIFY(sales.applyCredit(pay2.id, inv2.id, usd(5000), QDate(2026, 4, 26)).ok);
        QCOMPARE(documents.openBalance(inv2.id).minor(), 0);
        QCOMPARE(documents.openBalance(pay2.id).minor(), 8450);

        // Credit memo for 100.00 of design work, taxed.
        BusinessDocument memo;
        memo.partyId = s.acme;
        memo.date = QDate(2026, 4, 28);
        memo.taxRateId = s.metro;
        DocumentLine line = itemLine(s.design);
        line.unitPrice = usd(10000);
        memo.lines = {line};
        const Result cm = sales.createCreditMemo(memo);
        QVERIFY2(cm.ok, qPrintable(cm.error));
        QCOMPARE(documents.load(cm.id)->total.minor(), 10725);
        QCOMPARE(documents.load(cm.id)->number, QStringLiteral("CM-1001"));

        // Refund both credits from checking.
        const qint64 checking = s.c.account(QStringLiteral("1000"));
        QCOMPARE(sales.refund({s.acme, cm.id, checking, usd(10726), QDate(2026, 4, 30), {}}).code, ErrorCode::OverApplied);
        QVERIFY(sales.refund({s.acme, cm.id, checking, usd(10725), QDate(2026, 4, 30), QStringLiteral("refund CM")}).ok);
        QVERIFY(sales.refund({s.acme, pay2.id, checking, usd(8450), QDate(2026, 4, 30), QStringLiteral("refund overpayment")}).ok);

        // Everything settles to zero, and every account lands where the
        // hand computation says.
        QCOMPARE(sales.customerBalance(s.acme).minor(), 0);
        QCOMPARE(s.c.roleBalance(SystemRole::AccountsReceivable), 0);
        QCOMPARE(s.c.roleBalance(SystemRole::SalesTaxPayable), 12325);
        QCOMPARE(SalesTax(f).liability(s.state).minor(), 10200);
        QCOMPARE(SalesTax(f).liability(s.city).minor(), 2125);
        QCOMPARE(s.c.roleBalance(SystemRole::UndepositedFunds), 220000);
        QCOMPARE(s.c.balance(QStringLiteral("1000")), -19175);
        QCOMPARE(s.c.balance(QStringLiteral("4100")), 190000);
        QCOMPARE(s.c.balance(QStringLiteral("4000")), 20000);
        QVERIFY(sales.openInvoices(s.acme).isEmpty());
        QVERIFY(sales.openCredits(s.acme).isEmpty());
        QVERIFY(f.integrityProblems().isEmpty());

        // The statement walks the same A/R lines.
        const CustomerStatement st = sales.statement(s.acme, QDate(2026, 4, 1), QDate(2026, 4, 30));
        QCOMPARE(st.opening.minor(), 0);
        QCOMPARE(st.closing.minor(), 0);
        QCOMPARE(st.lines.size(), 7); // 2 invoices, 2 payments, 1 credit memo, 2 refunds
        QCOMPARE(st.lines.first().amount.minor(), 206550);
        QCOMPARE(st.lines[1].balance.minor(), 106550);
        QCOMPARE(st.aging.total().minor(), 0);

        // Remittance to one agency.
        QVERIFY(SalesTax(f).recordPayment(s.state, checking, usd(10200), QDate(2026, 5, 15)).ok);
        QCOMPARE(SalesTax(f).liability(s.state).minor(), 0);
        QCOMPARE(SalesTax(f).liability(s.city).minor(), 2125);
    }

    void draftsEditsAndVoids()
    {
        Setup s;
        CompanyFile &f = s.c.f();
        Sales sales(f);
        Documents documents(f);
        BusinessDocument draft;
        draft.partyId = s.acme;
        draft.date = QDate(2026, 6, 1);
        draft.state = DocState::Draft;
        draft.lines = {itemLine(s.hosting, QStringLiteral("4"))};
        const Result d = sales.createInvoice(draft);
        QVERIFY(d.ok);
        QCOMPARE(documents.status(*documents.load(d.id), QDate(2026, 6, 1)), DocumentStatus::Draft);
        QCOMPARE(s.c.roleBalance(SystemRole::AccountsReceivable), 0);
        QVERIFY(sales.postDraft(d.id).ok);
        QCOMPARE(s.c.roleBalance(SystemRole::AccountsReceivable), 20000);

        // Payment of 150, then an edit that would drop the total below it.
        ReceivePaymentInput p{s.acme, QDate(2026, 6, 5), usd(15000), 0, {}, {}, {PaymentApplication{d.id, usd(15000)}}};
        const Result pay = sales.receivePayment(p);
        QVERIFY(pay.ok);
        BusinessDocument edited = *documents.load(d.id);
        edited.lines = {itemLine(s.hosting, QStringLiteral("2"))};
        edited.lines[0].id = 0;
        QCOMPARE(sales.updateInvoice(edited, QStringLiteral("fewer months")).code, ErrorCode::OverApplied);
        edited.lines = {itemLine(s.hosting, QStringLiteral("5"))};
        const Result up = sales.updateInvoice(edited, QStringLiteral("one more month"));
        QVERIFY2(up.ok, qPrintable(up.error));
        QCOMPARE(up.id, d.id);
        QCOMPARE(s.c.roleBalance(SystemRole::AccountsReceivable), 25000 - 15000);
        QCOMPARE(documents.openBalance(d.id).minor(), 10000); // the payment stays applied
        // Posting the draft made the first transaction; the edit voided it
        // and posted the second.
        QCOMPARE(Ledger(f).transactionsForDocument(d.id).size(), 2);

        // Voiding the payment reopens the invoice.
        QVERIFY(Ledger(f).voidDocument(pay.id, QStringLiteral("bounced")).ok);
        QCOMPARE(documents.openBalance(d.id).minor(), 25000);
        QCOMPARE(s.c.roleBalance(SystemRole::AccountsReceivable), 25000);
        // Voiding the invoice zeroes it.
        QVERIFY(Ledger(f).voidDocument(d.id, QStringLiteral("cancelled")).ok);
        QCOMPARE(s.c.roleBalance(SystemRole::AccountsReceivable), 0);
        QCOMPARE(documents.status(*documents.load(d.id), QDate(2026, 6, 1)), DocumentStatus::Void);
        // Validation.
        BusinessDocument nobody;
        nobody.date = QDate(2026, 6, 1);
        nobody.lines = {itemLine(s.hosting)};
        QCOMPARE(sales.createInvoice(nobody).code, ErrorCode::UnknownParty);
        BusinessDocument empty;
        empty.partyId = s.acme;
        empty.date = QDate(2026, 6, 1);
        QCOMPARE(sales.createInvoice(empty).code, ErrorCode::Invalid);
        BusinessDocument dup;
        dup.partyId = s.acme;
        dup.date = QDate(2026, 6, 1);
        dup.number = documents.load(d.id)->number;
        dup.lines = {itemLine(s.hosting)};
        QCOMPARE(sales.createInvoice(dup).code, ErrorCode::Duplicate);
    }

    void salesReceiptGoesToUndepositedFunds()
    {
        Setup s;
        BusinessDocument r;
        r.partyId = s.acme;
        r.date = QDate(2026, 7, 1);
        r.lines = {itemLine(s.hosting, QStringLiteral("1.5"))};
        const Result sr = Sales(s.c.f()).createSalesReceipt(r);
        QVERIFY(sr.ok);
        QCOMPARE(s.c.roleBalance(SystemRole::UndepositedFunds), 7500);
        QCOMPARE(s.c.roleBalance(SystemRole::AccountsReceivable), 0);
        QCOMPARE(Documents(s.c.f()).load(sr.id)->number, QStringLiteral("SR-1001"));
        QCOMPARE(Banking(s.c.f()).undepositedPayments(), QList<qint64>{sr.id});
    }

    void recurringIsDeterministic()
    {
        RecurringSchedule monthly{Frequency::Monthly, 1, 31, QDate(2026, 1, 31), {}, 0};
        QCOMPARE(Recurring::occurrence(monthly, 0), QDate(2026, 1, 31));
        QCOMPARE(Recurring::occurrence(monthly, 1), QDate(2026, 2, 28));
        QCOMPARE(Recurring::occurrence(monthly, 2), QDate(2026, 3, 31));
        QCOMPARE(Recurring::occurrence(monthly, 3), QDate(2026, 4, 30));
        QCOMPARE(Recurring::occurrence(monthly, 13), QDate(2027, 2, 28));
        RecurringSchedule leap{Frequency::Yearly, 1, 0, QDate(2024, 2, 29), {}, 0};
        QCOMPARE(Recurring::occurrence(leap, 1), QDate(2025, 2, 28));
        QCOMPARE(Recurring::occurrence(leap, 4), QDate(2028, 2, 29));
        RecurringSchedule weekly{Frequency::Weekly, 2, 0, QDate(2026, 1, 5), QDate(2026, 2, 1), 0};
        QCOMPARE(Recurring::occurrence(weekly, 1), QDate(2026, 1, 19));
        QVERIFY(!Recurring::occurrence(weekly, 3).isValid()); // past the end date
        RecurringSchedule capped{Frequency::Daily, 1, 0, QDate(2026, 1, 1), {}, 2};
        QVERIFY(!Recurring::occurrence(capped, 2).isValid());

        Setup s;
        Recurring recurring(s.c.f());
        BusinessDocument body;
        body.partyId = s.acme;
        body.lines = {itemLine(s.hosting)};
        const Result t = recurring.create(QStringLiteral("Monthly hosting"), body, monthly);
        QVERIFY2(t.ok, qPrintable(t.error));
        QList<qint64> created;
        QVERIFY(recurring.generateDue(QDate(2026, 5, 15), &created).ok);
        QCOMPARE(created.size(), 4);
        QList<QDate> dates;
        for (qint64 id : created) {
            dates << Documents(s.c.f()).load(id)->date;
        }
        QCOMPARE(dates, (QList<QDate>{QDate(2026, 1, 31), QDate(2026, 2, 28), QDate(2026, 3, 31), QDate(2026, 4, 30)}));
        QVERIFY(recurring.generateDue(QDate(2026, 5, 15), &created).ok);
        QVERIFY(created.isEmpty()); // same "today": nothing new
        QVERIFY(recurring.generateDue(QDate(2026, 6, 1), &created).ok);
        QCOMPARE(created.size(), 1);
        QCOMPARE(Documents(s.c.f()).load(created.first())->date, QDate(2026, 5, 31));
        QCOMPARE(Documents(s.c.f()).load(created.first())->templateId, t.id);
        QCOMPARE(s.c.roleBalance(SystemRole::AccountsReceivable), 25000);
        QVERIFY(recurring.setActive(t.id, false).ok);
        QVERIFY(recurring.generateDue(QDate(2027, 1, 1), &created).ok);
        QVERIFY(created.isEmpty());
        // The template body never posts and is not an invoice.
        DocumentFilter filter;
        filter.kinds = {DocKind::Invoice};
        QCOMPARE(Documents(s.c.f()).list(filter).size(), 5);
    }
};

QTEST_GUILESS_MAIN(TstBooksSales)
#include "tst_books_sales.moc"
