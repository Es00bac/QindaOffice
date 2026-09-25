// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Test helpers for books_engine: a company in a temp dir, account lookup
// by template number, and the hand-computed "reports fixture" company
// whose numbers tst_books_reports and tst_books_analysis assert.

#include "accounts.h"
#include "company_file.h"
#include "journal.h"
#include "lists.h"
#include "purchases.h"
#include "sales.h"
#include "tax.h"
#include "banking.h"

#include <QTemporaryDir>
#include <QTest>

#include <memory>

namespace BooksTest {

using namespace QindaOffice::Books;

inline Money usd(qint64 cents)
{
    return Money(cents, QStringLiteral("USD"));
}

struct TestCompany {
    QTemporaryDir dir;
    std::unique_ptr<CompanyFile> file;

    explicit TestCompany(const QString &templateId = QStringLiteral("general"))
    {
        CompanySettings s;
        s.legalName = QStringLiteral("Test Books LLC");
        s.address = QStringLiteral("1 Main St\nSpringfield");
        QString error;
        file = CompanyFile::create(dir.filePath(QStringLiteral("test.qbooks")), s, templateId, &error);
        if (!file) {
            qFatal("could not create the test company: %s", qPrintable(error));
        }
    }
    CompanyFile &f() { return *file; }
    qint64 account(const QString &numberOrName) const { return ChartOfAccounts(*file).find(numberOrName); }
    qint64 role(const QString &role) const { return ChartOfAccounts(*file).idForRole(role); }
    qint64 customer(const QString &name, qint64 termsId = 0)
    {
        Party p;
        p.kind = PartyKind::Customer;
        p.displayName = name;
        p.billingAddress = name + QStringLiteral("\n42 Client Road");
        p.termsId = termsId;
        return Lists(*file).createParty(p).id;
    }
    qint64 vendor(const QString &name, qint64 defaultAccount = 0, qint64 termsId = 0)
    {
        Party p;
        p.kind = PartyKind::Vendor;
        p.displayName = name;
        p.defaultAccountId = defaultAccount;
        p.termsId = termsId;
        return Lists(*file).createParty(p).id;
    }
    qint64 item(const QString &name, qint64 priceCents, qint64 incomeAccount, bool taxable = false)
    {
        Item i;
        i.name = name;
        i.price = usd(priceCents);
        i.incomeAccountId = incomeAccount;
        i.taxable = taxable;
        return Lists(*file).createItem(i).id;
    }
    qint64 terms(const QString &name) const { return Lists(*file).findTerms(name); }
    qint64 balance(const QString &numberOrName, const QDate &asOf = {}) const
    {
        return ChartOfAccounts(*file).balance(account(numberOrName), asOf).minor();
    }
    qint64 roleBalance(const QString &r, const QDate &asOf = {}) const
    {
        return ChartOfAccounts(*file).balance(role(r), asOf).minor();
    }
};

inline DocumentLine itemLine(qint64 itemId, const QString &qty = QStringLiteral("1"), qint64 classId = 0)
{
    DocumentLine l;
    l.itemId = itemId;
    l.quantity = Decimal::parse(qty).value_or(Decimal(1, 0));
    l.classId = classId;
    return l;
}

inline DocumentLine accountLine(qint64 accountId, qint64 cents, const QString &description = {})
{
    DocumentLine l;
    l.accountId = accountId;
    l.amount = usd(cents);
    l.description = description;
    return l;
}

inline JournalLine debit(qint64 account, qint64 cents)
{
    JournalLine l;
    l.accountId = account;
    l.debit = usd(cents);
    return l;
}

inline JournalLine credit(qint64 account, qint64 cents)
{
    JournalLine l;
    l.accountId = account;
    l.credit = usd(cents);
    return l;
}

// The reports fixture (docs/books.md "Worked example"): Q1 2026 of a small
// studio on the general template. Hand-computed results are asserted in
// tst_books_reports / tst_books_analysis.
struct ReportsFixture : TestCompany {
    qint64 acme = 0, beta = 0, walkIn = 0, landlord = 0, supplier = 0, officeDepot = 0;
    qint64 consulting = 0, widget = 0, agency = 0, rate = 0, studioClass = 0;
    qint64 inv1001 = 0, inv1002 = 0, rentBill = 0, cogsBill = 0;

    ReportsFixture()
    {
        CompanyFile &c = f();
        Lists lists(c);
        studioClass = lists.createClass(ClassTag{0, QStringLiteral("Studio"), 0, true}).id;
        acme = customer(QStringLiteral("Acme Corp"));
        beta = customer(QStringLiteral("Beta LLC"));
        walkIn = customer(QStringLiteral("Walk-in"));
        landlord = vendor(QStringLiteral("Landlord"), account(QStringLiteral("6500")), terms(QStringLiteral("Net 15")));
        supplier = vendor(QStringLiteral("Supplier"), account(QStringLiteral("5000")));
        officeDepot = vendor(QStringLiteral("Office Depot"), account(QStringLiteral("6400")));
        consulting = item(QStringLiteral("Consulting"), 50000, account(QStringLiteral("4100")));
        widget = item(QStringLiteral("Widget"), 20000, account(QStringLiteral("4000")), true);
        SalesTax tax(c);
        agency = tax.createAgency(TaxAgency{0, QStringLiteral("State Revenue"), {}, RoundingMode::HalfUp, true}).id;
        rate = tax.createSimpleRate(agency, QStringLiteral("State 10%"), *Decimal::parse(QStringLiteral("10%"))).id;

        JournalEntryInput owner{QDate(2026, 1, 1), QStringLiteral("JE-1"), QStringLiteral("Owner contribution"),
                                {debit(account(QStringLiteral("1000")), 1000000),
                                 credit(account(QStringLiteral("3110")), 1000000)}};
        QVERIFY_RESULT(Ledger(c).postJournalEntry(owner));

        Sales sales(c);
        BusinessDocument a;
        a.partyId = acme;
        a.date = QDate(2026, 1, 10);
        a.lines = {itemLine(consulting, QStringLiteral("2"), studioClass)};
        const Result r1 = sales.createInvoice(a);
        QVERIFY_RESULT(r1);
        inv1001 = r1.id;

        BusinessDocument b;
        b.partyId = beta;
        b.date = QDate(2026, 1, 15);
        b.taxRateId = rate;
        b.lines = {itemLine(widget)};
        const Result r2 = sales.createInvoice(b);
        QVERIFY_RESULT(r2);
        inv1002 = r2.id;

        Purchases purchases(c);
        BusinessDocument rent;
        rent.partyId = landlord;
        rent.date = QDate(2026, 1, 20);
        rent.lines = {accountLine(0, 150000, QStringLiteral("January rent"))};
        const Result r3 = purchases.createBill(rent);
        QVERIFY_RESULT(r3);
        rentBill = r3.id;

        BusinessDocument card;
        card.kind = DocKind::CreditCardCharge;
        card.partyId = officeDepot;
        card.date = QDate(2026, 1, 25);
        card.accountId = account(QStringLiteral("2100"));
        card.lines = {accountLine(0, 12000, QStringLiteral("Paper and toner"))};
        QVERIFY_RESULT(purchases.createExpense(card));

        ReceivePaymentInput pay;
        pay.customerId = acme;
        pay.date = QDate(2026, 2, 5);
        pay.amount = usd(60000);
        pay.applications = {PaymentApplication{inv1001, usd(60000)}};
        const Result r4 = sales.receivePayment(pay);
        QVERIFY_RESULT(r4);
        QVERIFY_RESULT(Banking(c).deposit(account(QStringLiteral("1000")), QDate(2026, 2, 6), {r4.id}));
        QVERIFY_RESULT(purchases.payBills(QDate(2026, 2, 10), account(QStringLiteral("1000")),
                                          {PaymentApplication{rentBill, usd(150000)}}));
        BusinessDocument goods;
        goods.partyId = supplier;
        goods.date = QDate(2026, 2, 20);
        goods.termsId = terms(QStringLiteral("Net 30"));
        goods.lines = {accountLine(0, 8000, QStringLiteral("Widget stock"))};
        const Result r5 = purchases.createBill(goods);
        QVERIFY_RESULT(r5);
        cogsBill = r5.id;

        BusinessDocument receipt;
        receipt.partyId = walkIn;
        receipt.date = QDate(2026, 3, 1);
        receipt.accountId = account(QStringLiteral("1000"));
        receipt.lines = {itemLine(consulting, QStringLiteral("0.6"))};
        QVERIFY_RESULT(sales.createSalesReceipt(receipt));
    }

private:
    // QVERIFY cannot return from a constructor; fail loudly instead.
    static void QVERIFY_RESULT(const Result &r)
    {
        if (!r.ok) {
            qFatal("fixture step failed: %s", qPrintable(r.error));
        }
    }
};

} // namespace BooksTest
