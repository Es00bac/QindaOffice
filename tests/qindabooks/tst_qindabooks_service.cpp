// SPDX-License-Identifier: GPL-3.0-or-later
// BooksService's QML API over a real company file (M47): settings, money
// text, the name lists, every editable document kind, payments across
// invoices, bill payments, journal entries, void, estimates, drafts, the
// lock date and the changed() signal the views refresh on.

#include "books_app_test_support.h"

#include <QSignalSpy>

using namespace BooksAppTest;

class QindaBooksServiceTest final : public QObject {
    Q_OBJECT

private slots:
    void createsACompanyFromATemplate()
    {
        Company c;
        QVERIFY2(ok(c.created), qPrintable(c.created.value(QStringLiteral("error")).toString()));
        QVERIFY(c.service.hasCompany());
        QCOMPARE(c.service.companyName(), QStringLiteral("Test Books LLC"));
        QCOMPARE(c.service.currency(), QStringLiteral("USD"));
        QCOMPARE(c.service.today(), QStringLiteral("2026-03-31"));
        QVERIFY(c.account(QStringLiteral("1000")) != 0);
        // One company per window: a second create is refused.
        QVERIFY(!ok(c.service.createCompany(url(c.dir, QStringLiteral("other.qbooks")), {{QStringLiteral("name"), QStringLiteral("X")}},
                                            QStringLiteral("general"))));
        QCOMPARE(c.service.templates().size(), 4);
        QCOMPARE(c.service.templates().first().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("general"));
        const QVariantList currencies = c.service.currencies();
        QCOMPARE(currencies.first().toMap().value(QStringLiteral("code")).toString(), QStringLiteral("USD"));
        QCOMPARE(currencies.first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("US Dollar"));
        for (const char *code : {"EUR", "GBP", "CAD", "AUD", "NZD", "CHF", "JPY", "SEK", "NOK", "DKK", "MXN", "INR"}) {
            QVERIFY2(!find(currencies, QStringLiteral("code"), QString::fromLatin1(code)).isEmpty(), code);
        }
    }

    void settingsRoundTrip()
    {
        Company c;
        const QVariantMap saved = c.service.saveSettings({{QStringLiteral("name"), QStringLiteral("Loom Studio")},
                                                          {QStringLiteral("legalName"), QStringLiteral("Loom Studio LLC")},
                                                          {QStringLiteral("address"), QStringLiteral("1 Main St\nSpringfield")},
                                                          {QStringLiteral("email"), QStringLiteral("hello@example.test")},
                                                          {QStringLiteral("fiscalYearStartMonth"), 7},
                                                          {QStringLiteral("basis"), QStringLiteral("cash")},
                                                          {QStringLiteral("invoicePrefix"), QStringLiteral("LS-")},
                                                          {QStringLiteral("nextInvoiceNumber"), 500},
                                                          {QStringLiteral("invoiceNotes"), QStringLiteral("Thank you!")},
                                                          {QStringLiteral("paymentInstructions"), QStringLiteral("Pay by ACH")},
                                                          {QStringLiteral("accentColor"), QStringLiteral("#123456")},
                                                          {QStringLiteral("unknownKey"), 42}});
        QVERIFY2(ok(saved), qPrintable(saved.value(QStringLiteral("error")).toString()));
        const QVariantMap s = c.service.settings();
        QCOMPARE(s.value(QStringLiteral("name")).toString(), QStringLiteral("Loom Studio"));
        QCOMPARE(s.value(QStringLiteral("legalName")).toString(), QStringLiteral("Loom Studio LLC"));
        QCOMPARE(s.value(QStringLiteral("address")).toString(), QStringLiteral("1 Main St\nSpringfield"));
        QCOMPARE(s.value(QStringLiteral("fiscalYearStartMonth")).toInt(), 7);
        QCOMPARE(s.value(QStringLiteral("basis")).toString(), QStringLiteral("cash"));
        QCOMPARE(s.value(QStringLiteral("invoicePrefix")).toString(), QStringLiteral("LS-"));
        QCOMPARE(s.value(QStringLiteral("nextInvoiceNumber")).toLongLong(), 500);
        QCOMPARE(s.value(QStringLiteral("invoiceNotes")).toString(), QStringLiteral("Thank you!"));
        QCOMPARE(s.value(QStringLiteral("paymentInstructions")).toString(), QStringLiteral("Pay by ACH"));
        QCOMPARE(s.value(QStringLiteral("accentColor")).toString(), QStringLiteral("#123456"));
        QCOMPARE(c.service.companyName(), QStringLiteral("Loom Studio"));
        QVERIFY(!ok(c.service.saveSettings({{QStringLiteral("fiscalYearStartMonth"), 13}})));
        QVERIFY(!ok(c.service.saveSettings({{QStringLiteral("currency"), QStringLiteral("DOLLARS")}})));
    }

    void moneyTextBothWays()
    {
        Company c;
        QCOMPARE(c.service.formatMoney(123456), QStringLiteral("$1,234.56"));
        QCOMPARE(c.service.formatPlain(-5), QStringLiteral("-0.05"));
        QVariantMap p = c.service.parseMoney(QStringLiteral("1,234.5"));
        QVERIFY(ok(p));
        QCOMPARE(p.value(QStringLiteral("minor")).toLongLong(), 123450);
        p = c.service.parseMoney(QStringLiteral("(12.00)"));
        QCOMPARE(p.value(QStringLiteral("minor")).toLongLong(), -1200);
        p = c.service.parseMoney(QStringLiteral("1.234")); // three decimals of a two-decimal currency
        QVERIFY(!ok(p));
        QVERIFY(!p.value(QStringLiteral("error")).toString().isEmpty());
        QCOMPARE(c.service.parseMoney(QString()).value(QStringLiteral("minor")).toLongLong(), 0);
    }

    void listsCreateReadUpdate()
    {
        Company c;
        const qint64 acme = c.customer(QStringLiteral("Acme Corp"), QStringLiteral("ap@acme.test"));
        QVERIFY(acme != 0);
        QVariantMap row = find(c.service.parties(QStringLiteral("customer")), QStringLiteral("id"), acme);
        QCOMPARE(row.value(QStringLiteral("name")).toString(), QStringLiteral("Acme Corp"));
        QCOMPARE(row.value(QStringLiteral("balanceText")).toString(), QStringLiteral("$0.00"));
        QVERIFY(ok(c.service.saveParty({{QStringLiteral("id"), acme},
                                        {QStringLiteral("address"), QStringLiteral("9 Road\nCity")},
                                        {QStringLiteral("is1099"), false},
                                        {QStringLiteral("notes"), QStringLiteral("net 15 ok")}})));
        const QVariantMap party = c.service.party(acme);
        QCOMPARE(party.value(QStringLiteral("address")).toString(), QStringLiteral("9 Road\nCity"));
        QCOMPARE(party.value(QStringLiteral("email")).toString(), QStringLiteral("ap@acme.test"));
        QCOMPARE(party.value(QStringLiteral("kind")).toString(), QStringLiteral("customer"));
        QVERIFY(ok(c.service.setPartyActive(acme, false)));
        QVERIFY(find(c.service.parties(QStringLiteral("customer")), QStringLiteral("id"), acme).isEmpty());
        QVERIFY(!find(c.service.parties(QStringLiteral("customer"), true), QStringLiteral("id"), acme).isEmpty());
        QVERIFY(c.vendor(QStringLiteral("Landlord"), c.account(QStringLiteral("6500"))) != 0);
        QCOMPARE(c.service.parties(QStringLiteral("vendor")).size(), 1);

        const qint64 consulting = c.item(QStringLiteral("Consulting"), QStringLiteral("95.00"), QStringLiteral("4100"));
        QVariantMap item = find(c.service.items(), QStringLiteral("id"), consulting);
        QCOMPARE(item.value(QStringLiteral("price")).toString(), QStringLiteral("95.00"));
        QCOMPARE(item.value(QStringLiteral("priceText")).toString(), QStringLiteral("$95.00"));
        QCOMPARE(item.value(QStringLiteral("type")).toString(), QStringLiteral("service"));
        QVERIFY(ok(c.service.saveItem({{QStringLiteral("id"), consulting},
                                       {QStringLiteral("price"), QStringLiteral("110")},
                                       {QStringLiteral("type"), QStringLiteral("product")}})));
        item = find(c.service.items(), QStringLiteral("id"), consulting);
        QCOMPARE(item.value(QStringLiteral("priceMinor")).toLongLong(), 11000);
        QCOMPARE(item.value(QStringLiteral("type")).toString(), QStringLiteral("product"));
        QVERIFY(!ok(c.service.saveItem({{QStringLiteral("name"), QStringLiteral("Bad")}, {QStringLiteral("price"), QStringLiteral("abc")}})));

        const QVariantMap petty = c.service.saveAccount({{QStringLiteral("type"), QStringLiteral("bank")},
                                                         {QStringLiteral("name"), QStringLiteral("Petty Cash")},
                                                         {QStringLiteral("number"), QStringLiteral("1050")},
                                                         {QStringLiteral("openingBalance"), QStringLiteral("250.00")},
                                                         {QStringLiteral("openingDate"), QStringLiteral("2026-01-01")}});
        QVERIFY2(ok(petty), qPrintable(petty.value(QStringLiteral("error")).toString()));
        const QVariantMap account = find(c.service.accounts(), QStringLiteral("id"), idOf(petty));
        QCOMPARE(account.value(QStringLiteral("balance")).toLongLong(), 25000);
        QCOMPARE(account.value(QStringLiteral("type")).toString(), QStringLiteral("bank"));
        QVERIFY(!find(c.service.paymentAccounts(), QStringLiteral("id"), idOf(petty)).isEmpty());
        QVERIFY(!find(c.service.paymentAccounts(), QStringLiteral("id"), c.account(QStringLiteral("1200"))).isEmpty());
        QVERIFY(!find(c.service.expenseAccounts(), QStringLiteral("id"), c.account(QStringLiteral("6500"))).isEmpty());
        QVERIFY(!find(c.service.incomeAccounts(), QStringLiteral("id"), c.account(QStringLiteral("4000"))).isEmpty());
        QCOMPARE(c.service.accountTypes().size(), 15);
        QVERIFY(!c.service.accountTypes().first().toMap().value(QStringLiteral("id")).toString().isEmpty());
        QVERIFY(!ok(c.service.setAccountActive(c.account(QStringLiteral("1100")), false))); // system account

        QVERIFY(!c.service.terms().isEmpty());
        QVERIFY(ok(c.service.saveClass({{QStringLiteral("name"), QStringLiteral("Studio")}})));
        QCOMPARE(c.service.classes().size(), 1);
        QVERIFY(ok(c.service.saveTaxRate({{QStringLiteral("name"), QStringLiteral("State 10%")},
                                          {QStringLiteral("percent"), QStringLiteral("10")},
                                          {QStringLiteral("agencyName"), QStringLiteral("State Revenue")}})));
        QVERIFY(ok(c.service.saveTaxRate({{QStringLiteral("name"), QStringLiteral("State 6%")},
                                          {QStringLiteral("percent"), QStringLiteral("6")},
                                          {QStringLiteral("agencyName"), QStringLiteral("state revenue")}})));
        QCOMPARE(c.service.taxAgencies().size(), 1); // the second rate reuses the agency
        const QVariantMap rate = find(c.service.taxRates(), QStringLiteral("name"), QStringLiteral("State 10%"));
        QCOMPARE(rate.value(QStringLiteral("percent")).toString(), QStringLiteral("10"));
        QCOMPARE(rate.value(QStringLiteral("rateText")).toString(), QStringLiteral("10%"));
        QCOMPARE(rate.value(QStringLiteral("agencyName")).toString(), QStringLiteral("State Revenue"));
    }

    void invoiceWithTaxAndDiscount()
    {
        Company c;
        QVERIFY(ok(c.service.saveTaxRate({{QStringLiteral("name"), QStringLiteral("State 10%")},
                                          {QStringLiteral("percent"), QStringLiteral("10")},
                                          {QStringLiteral("agencyName"), QStringLiteral("State Revenue")}})));
        const qint64 rate = c.service.taxRates().first().toMap().value(QStringLiteral("id")).toLongLong();
        const qint64 acme = c.customer(QStringLiteral("Acme Corp"), {}, rate);
        const qint64 widget = c.item(QStringLiteral("Widget"), QStringLiteral("200.00"), QStringLiteral("4000"), true);
        const qint64 consulting = c.item(QStringLiteral("Consulting"), QStringLiteral("500.00"), QStringLiteral("4100"));
        const QVariantMap draft{
            {QStringLiteral("kind"), QStringLiteral("invoice")},
            {QStringLiteral("partyId"), acme},
            {QStringLiteral("date"), QStringLiteral("2026-03-01")},
            {QStringLiteral("discountPercent"), QStringLiteral("10")},
            {QStringLiteral("notes"), QStringLiteral("Thanks for the work")},
            {QStringLiteral("memo"), QStringLiteral("internal")},
            {QStringLiteral("lines"),
             QVariantList{QVariantMap{{QStringLiteral("itemId"), widget}, {QStringLiteral("quantity"), QStringLiteral("2")}},
                          QVariantMap{{QStringLiteral("itemId"), consulting}, {QStringLiteral("quantity"), QStringLiteral("1")}},
                          QVariantMap{}}}};
        // 2 × 200 + 500 = 900; 10% off = 90 (40 on the taxable widgets);
        // tax 10% × 360 = 36; total 846.
        const QVariantMap totals = c.service.computeTotals(draft);
        QVERIFY2(ok(totals), qPrintable(totals.value(QStringLiteral("error")).toString()));
        QCOMPARE(totals.value(QStringLiteral("subtotalText")).toString(), QStringLiteral("$900.00"));
        QCOMPARE(totals.value(QStringLiteral("discountText")).toString(), QStringLiteral("$90.00"));
        QCOMPARE(totals.value(QStringLiteral("taxText")).toString(), QStringLiteral("$36.00"));
        QCOMPARE(totals.value(QStringLiteral("totalText")).toString(), QStringLiteral("$846.00"));
        QCOMPARE(totals.value(QStringLiteral("total")).toLongLong(), 84600);
        QCOMPARE(totals.value(QStringLiteral("dueDate")).toString(), QStringLiteral("2026-03-31")); // Net 30
        const QVariantList lines = totals.value(QStringLiteral("lines")).toList();
        QCOMPARE(lines.size(), 3); // one per INPUT row, the blank one included
        QCOMPARE(lines.at(0).toMap().value(QStringLiteral("amountText")).toString(), QStringLiteral("$400.00"));
        QCOMPARE(lines.at(2).toMap().value(QStringLiteral("amountText")).toString(), QString());

        const QVariantMap saved = c.service.saveDocument(draft);
        QVERIFY2(ok(saved), qPrintable(saved.value(QStringLiteral("error")).toString()));
        QCOMPARE(saved.value(QStringLiteral("number")).toString(), QStringLiteral("INV-1001"));
        const QVariantMap doc = c.service.document(idOf(saved));
        QCOMPARE(doc.value(QStringLiteral("kind")).toString(), QStringLiteral("invoice"));
        QCOMPARE(doc.value(QStringLiteral("number")).toString(), QStringLiteral("INV-1001"));
        QCOMPARE(doc.value(QStringLiteral("partyName")).toString(), QStringLiteral("Acme Corp"));
        QCOMPARE(doc.value(QStringLiteral("discountPercent")).toString(), QStringLiteral("10"));
        QCOMPARE(doc.value(QStringLiteral("taxRateId")).toLongLong(), rate);
        QCOMPARE(doc.value(QStringLiteral("notes")).toString(), QStringLiteral("Thanks for the work"));
        QCOMPARE(doc.value(QStringLiteral("memo")).toString(), QStringLiteral("internal"));
        QCOMPARE(doc.value(QStringLiteral("totalText")).toString(), QStringLiteral("$846.00"));
        QCOMPARE(doc.value(QStringLiteral("balanceText")).toString(), QStringLiteral("$846.00"));
        QCOMPARE(doc.value(QStringLiteral("status")).toString(), QStringLiteral("open"));
        QCOMPARE(doc.value(QStringLiteral("statusLabel")).toString(), QStringLiteral("Open"));
        QCOMPARE(doc.value(QStringLiteral("lines")).toList().size(), 2);
        const QVariantMap line0 = doc.value(QStringLiteral("lines")).toList().first().toMap();
        QCOMPARE(line0.value(QStringLiteral("quantity")).toString(), QStringLiteral("2"));
        QCOMPARE(line0.value(QStringLiteral("rate")).toString(), QStringLiteral("200.00"));
        QCOMPARE(line0.value(QStringLiteral("amount")).toLongLong(), 40000);
        QVERIFY(line0.value(QStringLiteral("taxable")).toBool());
        QVERIFY(doc.value(QStringLiteral("canEdit")).toBool());
        QVERIFY(doc.value(QStringLiteral("canPrint")).toBool());

        // Lists, filters and search.
        QCOMPARE(c.service.documents({{QStringLiteral("kind"), QStringLiteral("invoice")}}).size(), 1);
        QCOMPARE(c.service.documents({{QStringLiteral("kind"), QStringLiteral("sales")}, {QStringLiteral("status"), QStringLiteral("open")}}).size(), 1);
        QCOMPARE(c.service.documents({{QStringLiteral("kind"), QStringLiteral("purchases")}}).size(), 0);
        QCOMPARE(c.service.documents({{QStringLiteral("search"), QStringLiteral("inv-1001")}}).size(), 1);
        QCOMPARE(c.service.documents({{QStringLiteral("search"), QStringLiteral("nobody")}}).size(), 0);
        QCOMPARE(c.service.documents({{QStringLiteral("status"), QStringLiteral("paid")}}).size(), 0);
        const QVariantMap row = c.service.documents({}).first().toMap();
        for (const char *key : {"id", "kind", "kindLabel", "number", "date", "dueDate", "partyId", "partyName", "total",
                                "totalText", "balance", "balanceText", "status", "statusLabel", "memo"}) {
            QVERIFY2(row.contains(QString::fromLatin1(key)), key);
        }

        // Edit = void + repost under the same id.
        QVariantMap edit = draft;
        edit.insert(QStringLiteral("id"), idOf(saved));
        edit.insert(QStringLiteral("discountPercent"), QString());
        QVERIFY(ok(c.service.saveDocument(edit)));
        QCOMPARE(c.service.document(idOf(saved)).value(QStringLiteral("totalText")).toString(), QStringLiteral("$940.00"));
    }

    void draftsEstimatesAndVoid()
    {
        Company c;
        const qint64 acme = c.customer(QStringLiteral("Acme"));
        QVariantMap draft{{QStringLiteral("kind"), QStringLiteral("invoice")},
                          {QStringLiteral("state"), QStringLiteral("draft")},
                          {QStringLiteral("partyId"), acme},
                          {QStringLiteral("lines"), QVariantList{QVariantMap{{QStringLiteral("description"), QStringLiteral("Deposit")},
                                                                            {QStringLiteral("accountId"), c.account(QStringLiteral("4100"))},
                                                                            {QStringLiteral("amount"), QStringLiteral("100.00")}}}}};
        const qint64 id = idOf(c.service.saveDocument(draft));
        QCOMPARE(c.service.document(id).value(QStringLiteral("status")).toString(), QStringLiteral("draft"));
        QCOMPARE(c.service.document(id).value(QStringLiteral("date")).toString(), QStringLiteral("2026-03-31")); // today
        QVERIFY(ok(c.service.postDraft(id)));
        QCOMPARE(c.service.document(id).value(QStringLiteral("status")).toString(), QStringLiteral("open"));
        // Only invoices are kept as drafts.
        draft.insert(QStringLiteral("kind"), QStringLiteral("salesReceipt"));
        QVERIFY(!ok(c.service.saveDocument(draft)));

        draft.insert(QStringLiteral("kind"), QStringLiteral("estimate"));
        draft.remove(QStringLiteral("state"));
        const qint64 estimate = idOf(c.service.saveDocument(draft));
        QCOMPARE(c.service.document(estimate).value(QStringLiteral("status")).toString(), QStringLiteral("pending"));
        const QVariantMap converted = c.service.convertEstimate(estimate);
        QVERIFY2(ok(converted), qPrintable(converted.value(QStringLiteral("error")).toString()));
        QCOMPARE(c.service.document(idOf(converted)).value(QStringLiteral("kind")).toString(), QStringLiteral("invoice"));
        QCOMPARE(c.service.document(estimate).value(QStringLiteral("status")).toString(), QStringLiteral("closed"));

        QVERIFY(ok(c.service.voidDocument(id, QStringLiteral("duplicate"))));
        const QVariantMap voided = c.service.document(id);
        QCOMPARE(voided.value(QStringLiteral("status")).toString(), QStringLiteral("void"));
        QCOMPARE(voided.value(QStringLiteral("balance")).toLongLong(), 0);
        QVERIFY(!voided.value(QStringLiteral("canVoid")).toBool());
        QCOMPARE(c.service.documents({{QStringLiteral("status"), QStringLiteral("void")}}).size(), 1);
        QVERIFY(!c.service.auditTrail(QStringLiteral("document"), id).isEmpty());
    }

    void billsExpensesAndPayBills()
    {
        Company c;
        const qint64 landlord = c.vendor(QStringLiteral("Landlord"), c.account(QStringLiteral("6500")));
        const qint64 depot = c.vendor(QStringLiteral("Office Depot"));
        const qint64 rent = c.bill(landlord, QStringLiteral("1500.00"));
        const qint64 supplies = c.bill(depot, QStringLiteral("80.00"), QStringLiteral("6400"), QStringLiteral("2026-01-10"));
        QVERIFY(rent != 0 && supplies != 0);
        QCOMPARE(c.service.document(rent).value(QStringLiteral("balanceText")).toString(), QStringLiteral("$1,500.00"));
        QCOMPARE(c.service.document(supplies).value(QStringLiteral("status")).toString(), QStringLiteral("overdue"));
        const QVariantMap expense = c.service.saveDocument(
            {{QStringLiteral("kind"), QStringLiteral("expense")},
             {QStringLiteral("paymentAccountId"), c.account(QStringLiteral("2100"))},
             {QStringLiteral("date"), QStringLiteral("2026-03-05")},
             {QStringLiteral("lines"), QVariantList{QVariantMap{{QStringLiteral("accountId"), c.account(QStringLiteral("6400"))},
                                                               {QStringLiteral("amount"), QStringLiteral("120.00")}}}}});
        QVERIFY2(ok(expense), qPrintable(expense.value(QStringLiteral("error")).toString()));
        QCOMPARE(c.service.document(idOf(expense)).value(QStringLiteral("paymentAccountId")).toLongLong(),
                 c.account(QStringLiteral("2100")));
        QCOMPARE(c.service.documents({{QStringLiteral("kind"), QStringLiteral("purchases")}}).size(), 3);
        // A posted expense is voided and re-entered, never edited.
        QVariantMap edit = c.service.document(idOf(expense));
        QVERIFY(!ok(c.service.saveDocument(edit)));

        QCOMPARE(c.service.unpaidBills(0).size(), 2);
        QCOMPARE(c.service.unpaidBills(landlord).size(), 1);
        const QVariantMap paid = c.service.payBills({{QStringLiteral("date"), QStringLiteral("2026-03-20")},
                                                     {QStringLiteral("paymentAccountId"), c.account(QStringLiteral("1000"))},
                                                     {QStringLiteral("reference"), QStringLiteral("101")},
                                                     {QStringLiteral("bills"), QVariantList{
                                                          QVariantMap{{QStringLiteral("billId"), rent}, {QStringLiteral("amount"), QStringLiteral("1500.00")}},
                                                          QVariantMap{{QStringLiteral("billId"), supplies}, {QStringLiteral("amount"), QStringLiteral("30.00")}}}}});
        QVERIFY2(ok(paid), qPrintable(paid.value(QStringLiteral("error")).toString()));
        QCOMPARE(paid.value(QStringLiteral("ids")).toList().size(), 2); // one payment per vendor
        const QVariantList unpaid = c.service.unpaidBills(0);
        QCOMPARE(unpaid.size(), 1);
        QCOMPARE(unpaid.first().toMap().value(QStringLiteral("balancePlain")).toString(), QStringLiteral("50.00"));
        QCOMPARE(unpaid.first().toMap().value(QStringLiteral("partyName")).toString(), QStringLiteral("Office Depot"));
        QCOMPARE(c.service.document(rent).value(QStringLiteral("status")).toString(), QStringLiteral("paid"));
        QCOMPARE(find(c.service.parties(QStringLiteral("vendor")), QStringLiteral("id"), depot).value(QStringLiteral("overdueCount")).toInt(), 1);
    }

    void paymentAcrossTwoInvoices()
    {
        Company c;
        const qint64 acme = c.customer(QStringLiteral("Acme"));
        const qint64 first = c.invoice(acme, QStringLiteral("300.00"));
        const qint64 second = c.invoice(acme, QStringLiteral("500.00"));
        QCOMPARE(c.service.openInvoices(acme).size(), 2);
        const QVariantMap payment = c.service.receivePayment(
            {{QStringLiteral("customerId"), acme},
             {QStringLiteral("date"), QStringLiteral("2026-03-15")},
             {QStringLiteral("amount"), QStringLiteral("600.00")},
             {QStringLiteral("depositAccountId"), c.account(QStringLiteral("1000"))},
             {QStringLiteral("method"), QStringLiteral("Check")},
             {QStringLiteral("reference"), QStringLiteral("4417")},
             {QStringLiteral("applications"), QVariantList{
                  QVariantMap{{QStringLiteral("invoiceId"), first}, {QStringLiteral("amount"), QStringLiteral("300.00")}},
                  QVariantMap{{QStringLiteral("invoiceId"), second}, {QStringLiteral("amount"), QStringLiteral("300.00")}}}}});
        QVERIFY2(ok(payment), qPrintable(payment.value(QStringLiteral("error")).toString()));
        QCOMPARE(c.service.document(first).value(QStringLiteral("status")).toString(), QStringLiteral("paid"));
        QCOMPARE(c.service.document(second).value(QStringLiteral("status")).toString(), QStringLiteral("partial"));
        const QVariantList open = c.service.openInvoices(acme);
        QCOMPARE(open.size(), 1);
        QCOMPARE(open.first().toMap().value(QStringLiteral("balanceText")).toString(), QStringLiteral("$200.00"));
        QCOMPARE(open.first().toMap().value(QStringLiteral("balancePlain")).toString(), QStringLiteral("200.00"));
        QCOMPARE(c.service.document(second).value(QStringLiteral("applications")).toList().size(), 1);
        QCOMPARE(c.service.party(acme).value(QStringLiteral("balance")).toLongLong(), 20000);
        QCOMPARE(c.service.document(idOf(payment)).value(QStringLiteral("memo")).toString(), QStringLiteral("Check"));
    }

    void journalEntriesTransfersDeposits()
    {
        Company c;
        const qint64 checking = c.account(QStringLiteral("1000"));
        const qint64 owner = c.account(QStringLiteral("3110"));
        const auto line = [](qint64 account, const QString &debit, const QString &credit) {
            return QVariantMap{{QStringLiteral("accountId"), account}, {QStringLiteral("debit"), debit},
                               {QStringLiteral("credit"), credit}};
        };
        QVariantMap entry{{QStringLiteral("date"), QStringLiteral("2026-01-01")},
                          {QStringLiteral("number"), QStringLiteral("JE-1")},
                          {QStringLiteral("memo"), QStringLiteral("Owner contribution")},
                          {QStringLiteral("lines"), QVariantList{line(checking, QStringLiteral("10000.00"), QString()),
                                                                 line(owner, QString(), QStringLiteral("9000.00")), QVariantMap{}}}};
        QVariantMap check = c.service.checkJournalEntry(entry);
        QVERIFY(!check.value(QStringLiteral("balanced")).toBool());
        QCOMPARE(check.value(QStringLiteral("differenceText")).toString(), QStringLiteral("$1,000.00"));
        const QVariantMap refused = c.service.postJournalEntry(entry);
        QVERIFY(!ok(refused));
        QVERIFY(!refused.value(QStringLiteral("error")).toString().isEmpty());
        entry.insert(QStringLiteral("lines"), QVariantList{line(checking, QStringLiteral("10000.00"), QString()),
                                                          line(owner, QString(), QStringLiteral("10000.00"))});
        check = c.service.checkJournalEntry(entry);
        QVERIFY(check.value(QStringLiteral("balanced")).toBool());
        QCOMPARE(check.value(QStringLiteral("debitText")).toString(), QStringLiteral("$10,000.00"));
        QVERIFY(ok(c.service.postJournalEntry(entry)));
        QCOMPARE(c.service.documents({{QStringLiteral("kind"), QStringLiteral("journal")}}).size(), 1);

        QVERIFY(ok(c.service.transfer({{QStringLiteral("fromAccountId"), checking},
                                       {QStringLiteral("toAccountId"), c.account(QStringLiteral("1010"))},
                                       {QStringLiteral("amount"), QStringLiteral("2500.00")},
                                       {QStringLiteral("date"), QStringLiteral("2026-01-02")}})));
        QCOMPARE(find(c.service.accounts(), QStringLiteral("id"), checking).value(QStringLiteral("balance")).toLongLong(), 750000);

        const qint64 acme = c.customer(QStringLiteral("Acme"));
        const qint64 inv = c.invoice(acme, QStringLiteral("250.00"));
        QVERIFY(ok(c.service.receivePayment({{QStringLiteral("customerId"), acme},
                                             {QStringLiteral("applications"), QVariantList{QVariantMap{
                                                  {QStringLiteral("invoiceId"), inv}, {QStringLiteral("amount"), QStringLiteral("250.00")}}}}})));
        const QVariantList waiting = c.service.undepositedPayments();
        QCOMPARE(waiting.size(), 1);
        QCOMPARE(waiting.first().toMap().value(QStringLiteral("amountText")).toString(), QStringLiteral("$250.00"));
        QCOMPARE(waiting.first().toMap().value(QStringLiteral("partyName")).toString(), QStringLiteral("Acme"));
        QVERIFY(ok(c.service.deposit({{QStringLiteral("accountId"), checking},
                                      {QStringLiteral("date"), QStringLiteral("2026-03-16")},
                                      {QStringLiteral("paymentIds"), QVariantList{waiting.first().toMap().value(QStringLiteral("id"))}}})));
        QVERIFY(c.service.undepositedPayments().isEmpty());
        QCOMPARE(find(c.service.accounts(), QStringLiteral("id"), checking).value(QStringLiteral("balance")).toLongLong(), 775000);
    }

    void lockDateRefusesEarlierPostings()
    {
        Company c;
        QVERIFY(ok(c.service.setLockDate(QStringLiteral("2026-01-31"), QStringLiteral("January closed"))));
        QCOMPARE(c.service.lockDate(), QStringLiteral("2026-01-31"));
        const qint64 acme = c.customer(QStringLiteral("Acme"));
        QCOMPARE(c.invoice(acme, QStringLiteral("10.00"), QStringLiteral("2026-01-15")), 0);
        QVERIFY(c.invoice(acme, QStringLiteral("10.00"), QStringLiteral("2026-02-01")) != 0);
        QVERIFY(!ok(c.service.setLockDate(QStringLiteral("not a date"), QString())));
    }

    void changedFiresAfterEveryCommit()
    {
        Company c;
        QSignalSpy changed(&c.service, &BooksService::changed);
        const int revision = c.service.revision();
        c.customer(QStringLiteral("Acme"));
        QVERIFY(changed.count() >= 1);
        QVERIFY(c.service.revision() > revision);
        changed.clear();
        QVERIFY(!ok(c.service.saveParty({{QStringLiteral("kind"), QStringLiteral("customer")}}))); // no name
        QCOMPARE(changed.count(), 0); // a refused call commits nothing
    }
};

BOOKS_APP_TEST_MAIN(QindaBooksServiceTest)

#include "tst_qindabooks_service.moc"
