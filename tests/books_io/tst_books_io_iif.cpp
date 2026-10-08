// SPDX-License-Identifier: GPL-3.0-or-later
// QuickBooks Desktop migration (M63, D-071): IIF import of lists and
// transactions. A hand-written QuickBooks-shaped file (header rows, quoted
// names, MM/DD/YYYY dates, signed amounts, a sub-account, a hidden and a
// non-posting account, every transaction type of the brief) imports and
// balances to the cent; the preview changes nothing; the unbalanced and the
// unmapped transactions are refused with their lines; a mapping lets the
// latter in; importing twice adds nothing; QindaBooks' own IIF export
// round-trips; QuickBooks Online list exports (CSV) import.

#include "books_io_fixture.h"

#include "banking.h"
#include "documents.h"
#include "iif_import.h"
#include "iif_reader.h"
#include "journal.h"
#include "ledger_export.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace QindaOffice::Books;

namespace {

QString str(const char *text)
{
    return QString::fromUtf8(text);
}

// '|' stands for a tab; lines end in CRLF, as QuickBooks writes them.
QByteArray iifText(const char *text)
{
    QByteArray bytes(text);
    bytes.replace('|', '\t');
    bytes.replace("\n", "\r\n");
    return bytes;
}

int lineOf(const QByteArray &text, const char *needle)
{
    const QList<QByteArray> lines = text.split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        if (lines.at(i).contains(needle)) {
            return i + 1;
        }
    }
    return -1;
}

// A small design studio's books as QuickBooks Desktop writes them.
const char *const kStudio = R"IIF(!ACCNT|NAME|REFNUM|ACCNTTYPE|OBAMOUNT|DESC|ACCNUM|EXTRA|HIDDEN
ACCNT|Operating Checking|1|BANK|0.00|Main account|1015||N
ACCNT|Reserve Savings|2|BANK|500.00||1020||N
ACCNT|Accounts Receivable|3|AR|0.00||1200||N
ACCNT|Undeposited Funds|4|OCASSET|0.00||1499|UNDEPOSIT|N
ACCNT|Accounts Payable|5|AP|0.00||2000||N
ACCNT|Visa|6|CCARD|0.00||2050||N
ACCNT|Sales Tax Payable|7|OCLIAB|0.00||2210||N
ACCNT|Opening Bal Equity|8|EQUITY|0.00||3010|OPENBAL|N
ACCNT|Design Income|9|INC|0.00||4010||N
ACCNT|Design Income:Rush Fees|10|INC|0.00|Two-day turnaround|4011||N
ACCNT|Interest Income|11|EXINC|0.00||7010||N
ACCNT|Printing|12|COGS|0.00||5010||N
ACCNT|Studio Rent|13|EXP|0.00||6110||N
ACCNT|Office Supplies|14|EXP|0.00||6210||N
ACCNT|Old Promotions|15|EXP|0.00||6910||Y
ACCNT|Estimates|16|NONPOSTING|0.00||||N
!CUST|NAME|BADDR1|BADDR2|BADDR3|PHONE1|EMAIL|CONT1|TERMS|COMPANYNAME|HIDDEN
CUST|"Bluebird Bakery, LLC"|"Bluebird Bakery, LLC"|12 Elm St|"Eugene, OR 97401"|541-555-0101|ap@bluebird.test|Ann Lee|Net 30|"Bluebird Bakery, LLC"|N
CUST|Cedar Clinic|Cedar Clinic|88 Pine Ave||||||Net 15||N
!VEND|NAME|ADDR1|ADDR2|TAXID|1099|TERMS|HIDDEN
VEND|Paper Mill Supply|Paper Mill Supply|4 Mill Rd||N|Net 30|N
VEND|Lakeside Properties|||93-1234567|Y||N
!INVITEM|NAME|INVITEMTYPE|DESC|ACCNT|COGSACCNT|PRICE|COST|TAXABLE|HIDDEN
INVITEM|Logo Design|SERV|Logo design package|Design Income||750.00|0|N|N
INVITEM|Posters|PART|Printed posters|Design Income|Printing|25.00|10.00|Y|N
INVITEM|Easel|INVENTORY|Display easel|Design Income|Printing|90.00|40.00|Y|N
INVITEM|Loyalty Discount|DISC||Design Income||10%||N|N
!CLASS|NAME|HIDDEN
CLASS|Studio|N
!TRNS|TRNSID|TRNSTYPE|DATE|ACCNT|NAME|CLASS|AMOUNT|DOCNUM|MEMO|DUEDATE
!SPL|SPLID|TRNSTYPE|DATE|ACCNT|NAME|CLASS|AMOUNT|DOCNUM|MEMO|QNTY|PRICE|INVITEM
!ENDTRNS
TRNS||GENERAL JOURNAL|1/1/2026|Operating Checking|||10000.00|1|Owner investment
SPL||GENERAL JOURNAL|1/1/2026|Opening Bal Equity|||-10000.00|1|
ENDTRNS
TRNS||INVOICE|01/15/2026|Accounts Receivable|"Bluebird Bakery, LLC"|Studio|1081.25|1001|Spring campaign|02/14/2026
SPL||INVOICE|01/15/2026|Design Income|"Bluebird Bakery, LLC"|Studio|-750.00|1001|Logo design package|-1|750.00|Logo Design
SPL||INVOICE|01/15/2026|Design Income|"Bluebird Bakery, LLC"|Studio|-250.00|1001|Printed posters|-10|25.00|Posters
SPL||INVOICE|01/15/2026|Sales Tax Payable|Lane County||-81.25|1001|Sales tax
ENDTRNS
TRNS||PAYMENT|02/01/2026|Undeposited Funds|"Bluebird Bakery, LLC"||1081.25|3317|Check 3317
SPL||PAYMENT|02/01/2026|Accounts Receivable|"Bluebird Bakery, LLC"||-1081.25|3317|
ENDTRNS
TRNS||DEPOSIT|02/02/2026|Operating Checking|||1131.25||Deposit
SPL||DEPOSIT|02/02/2026|Undeposited Funds|"Bluebird Bakery, LLC"||-1081.25|||
SPL||DEPOSIT|02/02/2026|Interest Income|||-50.00||Interest
ENDTRNS
TRNS||BILL|02/03/2026|Accounts Payable|Lakeside Properties||-1500.00|FEB-RENT|February rent|03/05/2026
SPL||BILL|02/03/2026|Studio Rent|Lakeside Properties||1500.00|FEB-RENT|
ENDTRNS
TRNS||BILL PMT|02/20/2026|Operating Checking|Lakeside Properties||-1500.00|1042|
SPL||BILL PMT|02/20/2026|Accounts Payable|Lakeside Properties||1500.00|1042|
ENDTRNS
TRNS||CHECK|02/21/2026|Operating Checking|Paper Mill Supply||-212.40|1043|Paper stock
SPL||CHECK|02/21/2026|Printing|Paper Mill Supply||180.00|1043|
SPL||CHECK|02/21/2026|Office Supplies|Paper Mill Supply||32.40|1043|
ENDTRNS
TRNS||CREDIT CARD|02/22/2026|Visa|Paper Mill Supply||-45.00||Toner
SPL||CREDIT CARD|02/22/2026|Office Supplies|||45.00||
ENDTRNS
TRNS||INVOICE|02/25/2026|Accounts Receivable|Cedar Clinic||400.00|1002|Brochure|03/12/2026
SPL||INVOICE|02/25/2026|Design Income:Rush Fees|Cedar Clinic||-400.00|1002|
ENDTRNS
TRNS||CREDIT MEMO|02/27/2026|Accounts Receivable|Cedar Clinic||-100.00|CM-7|Proof correction
SPL||CREDIT MEMO|02/27/2026|Design Income|Cedar Clinic||100.00|CM-7|
ENDTRNS
TRNS||TRANSFER|02/28/2026|Operating Checking|||-2000.00||To savings
SPL||TRANSFER|02/28/2026|Reserve Savings|||2000.00||
ENDTRNS
TRNS||GENERAL JOURNAL|02/28/2026|Petty Cash|||10.00|J2|Cash for stamps
SPL||GENERAL JOURNAL|02/28/2026|Office Supplies|||-10.00|J2|
ENDTRNS
TRNS||GENERAL JOURNAL|02/28/2026|Studio Rent|||100.00|J3|Typo
SPL||GENERAL JOURNAL|02/28/2026|Operating Checking|||-90.00|J3|
ENDTRNS
)IIF";

qint64 balanceOf(CompanyFile &f, const char *name)
{
    ChartOfAccounts chart(f);
    return chart.balance(chart.find(str(name))).minor();
}

std::unique_ptr<CompanyFile> company(const QTemporaryDir &dir, const char *name, const char *chart = "general")
{
    CompanySettings s;
    s.legalName = str("Target Books LLC");
    return CompanyFile::create(dir.filePath(str(name)), s, str(chart));
}

} // namespace

class TestBooksIoIif final : public QObject {
    Q_OBJECT

private slots:
    void readerSplitsQuotesAndBlocks();
    void previewChangesNothing();
    void studioImportsAndBalances();
    void mappingLetsTheUnknownAccountIn();
    void importingTwiceAddsNothing();
    void ownExportRoundTrips();
    void quickBooksOnlineListsImport();
};

void TestBooksIoIif::readerSplitsQuotesAndBlocks()
{
    QCOMPARE(IifReader::splitLine(str("CUST\t\"Smith, \"\"Jo\"\"\"\t\tx")),
             (QStringList{str("CUST"), str("Smith, \"Jo\""), QString(), str("x")}));
    QCOMPARE(IifReader::parseDate(str("02/14/2026")), QDate(2026, 2, 14));
    QCOMPARE(IifReader::parseDate(str("2/3/26")), QDate(2026, 2, 3));
    QCOMPARE(IifReader::parseDate(str("12/31/99")), QDate(1999, 12, 31));
    QVERIFY(!IifReader::parseDate(str("31/12/2026")).isValid());
    QCOMPARE(IifReader::normalizedType(str("Bill Pmt -Check")), str("BILLPMTCHECK"));
    // Windows-1252 bytes, a header redefined half-way, a block without
    // ENDTRNS and an SPL outside any transaction.
    const QByteArray bytes = "!CUST\tNAME\r\nCUST\tCaf\xe9 Rio\r\n!CUST\tEMAIL\tNAME\r\nCUST\tx@y.test\tZed\r\n"
                             "!TRNS\tTRNSTYPE\tDATE\r\n!SPL\tTRNSTYPE\r\nSPL\tCHECK\r\nTRNS\tCHECK\t1/2/2026\r\n"
                             "SPL\tCHECK\r\nTRNS\tCHECK\t1/3/2026\r\nSPL\tCHECK\r\nENDTRNS\r\n";
    const IifDocument doc = IifReader::parse(bytes);
    QCOMPARE(doc.lists.size(), 2);
    QCOMPARE(doc.lists.at(0).value(str("NAME")), str("Café Rio"));
    QCOMPARE(doc.lists.at(1).value(str("NAME")), str("Zed"));
    QCOMPARE(doc.lists.at(1).value(str("EMAIL")), str("x@y.test"));
    QCOMPARE(doc.transactions.size(), 2);
    QVERIFY(!doc.transactions.at(0).error.isEmpty()); // no ENDTRNS
    QVERIFY(doc.transactions.at(1).error.isEmpty());
    QCOMPARE(doc.problems.size(), 1);
    QVERIFY(doc.problems.first().startsWith(str("line 7:")));
}

void TestBooksIoIif::previewChangesNothing()
{
    QTemporaryDir dir;
    std::unique_ptr<CompanyFile> f = company(dir, "preview.qbooks");
    const int accounts = int(ChartOfAccounts(*f).accounts(true).size());
    const IifImportReport preview = IifImport::run(*f, iifText(kStudio));
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QVERIFY(!preview.committed);
    QCOMPARE(preview.posted, 11);
    QCOMPARE(int(ChartOfAccounts(*f).accounts(true).size()), accounts);
    QVERIFY(Lists(*f).parties(PartyKind::Customer, true).isEmpty());
    QCOMPARE(Ledger(*f).auditTrail(str("document")).size(), 0);
    // Committing with refusals is refused unless asked to skip them.
    IifImportOptions strict;
    strict.commit = true;
    const IifImportReport refused = IifImport::run(*f, iifText(kStudio), strict);
    QVERIFY(!refused.ok);
    QVERIFY(!refused.committed);
    QVERIFY(Lists(*f).parties(PartyKind::Customer, true).isEmpty());
    QVERIFY(!IifImport::run(*f, "Name,Email\nNot,IIF\n").ok);
}

void TestBooksIoIif::studioImportsAndBalances()
{
    QTemporaryDir dir;
    std::unique_ptr<CompanyFile> f = company(dir, "studio.qbooks");
    const QByteArray text = iifText(kStudio);
    IifImportOptions options;
    options.commit = true;
    options.skipRefused = true;
    const IifImportReport r = IifImport::run(*f, text, options);
    QVERIFY2(r.ok && r.committed, qPrintable(r.error));
    // Lists: the system accounts are this company's own; the non-posting
    // one and the discount item are left out; the hidden account is
    // created and deactivated.
    QCOMPARE(r.accounts.added, 9);
    // A/R, A/P, Undeposited Funds, Sales Tax Payable and Interest Income by
    // name; Opening Bal Equity by QuickBooks' name for it.
    QCOMPARE(r.accounts.existing, 6);
    QCOMPARE(r.accounts.skipped, 1); // Estimates (non-posting)
    QCOMPARE(r.customers.added, 2);
    QCOMPARE(r.vendors.added, 2);
    QCOMPARE(r.items.added, 3);
    QCOMPARE(r.items.skipped, 1);
    QCOMPARE(r.classes.added, 1);
    ChartOfAccounts chart(*f);
    QCOMPARE(chart.account(chart.find(str("Design Income:Rush Fees")))->number, str("4011"));
    QVERIFY(!chart.account(chart.find(str("Old Promotions")))->active);
    const std::optional<Party> bluebird = Lists(*f).party(Lists(*f).findParty(PartyKind::Customer, str("Bluebird Bakery, LLC")));
    QVERIFY(bluebird.has_value());
    QCOMPARE(bluebird->billingAddress, str("12 Elm St\nEugene, OR 97401"));
    QCOMPARE(bluebird->contactName, str("Ann Lee"));
    QVERIFY(Lists(*f).party(Lists(*f).findParty(PartyKind::Vendor, str("Lakeside Properties")))->is1099);
    QCOMPARE(Lists(*f).findParty(PartyKind::Customer, str("Lane County")), 0); // the tax agency is no customer
    QVERIFY(std::any_of(r.notes.cbegin(), r.notes.cend(), [](const QString &n) { return n.contains(str("OBAMOUNT")); }));

    // Transactions: 11 posted (one journal entry, ten documents), two refused.
    QCOMPARE(r.posted, 11);
    QCOMPARE(r.refused.size(), 2);
    QCOMPARE(r.refused.at(0).firstLine, lineOf(text, "Cash for stamps"));
    QCOMPARE(r.refused.at(1).firstLine, lineOf(text, "Typo"));
    QCOMPARE(r.refused.at(1).lastLine, lineOf(text, "Typo") + 2);
    QVERIFY(r.refused.at(1).reason.contains(str("do not balance")));
    QCOMPARE(r.unmappedAccounts.size(), 1);
    QCOMPARE(r.unmappedAccounts.first().name, str("Petty Cash"));
    int documents = 0;
    int journal = 0;
    for (const IifTypeCount &t : r.transactions) {
        documents += t.documents;
        journal += t.journal;
    }
    QCOMPARE(documents, 10);
    QCOMPARE(journal, 1);

    // The ledger is the file's, to the cent.
    QCOMPARE(balanceOf(*f, "Operating Checking"), 741885);
    QCOMPARE(balanceOf(*f, "Reserve Savings"), 200000);
    QCOMPARE(balanceOf(*f, "Accounts Receivable"), 30000);
    QCOMPARE(balanceOf(*f, "Undeposited Funds"), 0);
    QCOMPARE(balanceOf(*f, "Accounts Payable"), 0);
    QCOMPARE(balanceOf(*f, "Visa"), 4500);
    QCOMPARE(balanceOf(*f, "Sales Tax Payable"), 8125);
    QCOMPARE(balanceOf(*f, "Opening Balance Equity"), 1000000);
    QCOMPARE(balanceOf(*f, "Design Income"), 90000);
    QCOMPARE(balanceOf(*f, "Design Income:Rush Fees"), 40000);
    QCOMPARE(balanceOf(*f, "Interest Income"), 5000);
    QCOMPARE(balanceOf(*f, "Printing"), 18000);
    QCOMPARE(balanceOf(*f, "Office Supplies"), 7740);
    QCOMPARE(balanceOf(*f, "Studio Rent"), 150000);
    QVERIFY(f->integrityProblems().isEmpty());

    // Documents where QuickBooks had them, linked the way QuickBooks would.
    Documents docs(*f);
    const QList<BusinessDocument> invoices = docs.list({{DocKind::Invoice}, 0, {}, {}, false, false});
    QCOMPARE(invoices.size(), 2);
    for (const BusinessDocument &inv : invoices) {
        QCOMPARE(docs.openBalance(inv.id).minor(), inv.number == str("1001") ? 0 : 30000); // paid; credit applied
    }
    QCOMPARE(invoices.first().dueDate, QDate(2026, 2, 14));
    QVERIFY(Banking(*f).undepositedPayments().isEmpty()); // the deposit took the payment
    QCOMPARE(docs.list({{DocKind::Bill}, 0, {}, {}, false, false}).size(), 1);
    QCOMPARE(docs.openBalance(docs.list({{DocKind::Bill}, 0, {}, {}, false, false}).first().id).minor(), 0);
    for (DocKind kind : {DocKind::Check, DocKind::CreditCardCharge, DocKind::Deposit, DocKind::Transfer,
                         DocKind::CreditMemo, DocKind::ReceivePayment, DocKind::BillPayment}) {
        QCOMPARE(docs.list({{kind}, 0, {}, {}, false, false}).size(), 1);
    }
}

void TestBooksIoIif::mappingLetsTheUnknownAccountIn()
{
    QTemporaryDir dir;
    std::unique_ptr<CompanyFile> f = company(dir, "mapped.qbooks");
    IifImportOptions options;
    options.commit = true;
    options.skipRefused = true;
    options.accountMap.insert(str("Petty Cash"), ChartOfAccounts(*f).find(str("Checking")));
    const IifImportReport r = IifImport::run(*f, iifText(kStudio), options);
    QVERIFY2(r.committed, qPrintable(r.error));
    QCOMPARE(r.posted, 12);
    QCOMPARE(r.refused.size(), 1); // only the unbalanced one
    QVERIFY(r.unmappedAccounts.isEmpty());
    QCOMPARE(balanceOf(*f, "Checking"), 1000);
}

void TestBooksIoIif::importingTwiceAddsNothing()
{
    QTemporaryDir dir;
    std::unique_ptr<CompanyFile> f = company(dir, "twice.qbooks");
    IifImportOptions options;
    options.commit = true;
    options.skipRefused = true;
    QVERIFY(IifImport::run(*f, iifText(kStudio), options).committed);
    const qint64 checking = balanceOf(*f, "Operating Checking");
    const IifImportReport again = IifImport::run(*f, iifText(kStudio), options);
    QVERIFY(again.committed);
    QCOMPARE(again.posted, 0);
    QCOMPARE(again.accounts.added, 0);
    QCOMPARE(again.customers.added, 0);
    int duplicates = 0;
    for (const IifTypeCount &t : again.transactions) {
        duplicates += t.duplicates;
    }
    QCOMPARE(duplicates, 11);
    QCOMPARE(balanceOf(*f, "Operating Checking"), checking);
}

void TestBooksIoIif::ownExportRoundTrips()
{
    QTemporaryDir dir;
    BooksIoFixture::Company source = BooksIoFixture::build(dir.filePath(str("source.qbooks")));
    CompanyFile &a = *source.file;
    // A deactivated, unused account travels as HIDDEN.
    ChartOfAccounts(a).create(Account{0, str("8800"), str("Retired Fees"), AccountType::Expense, 0, {}, {}, {}, true, {}, 0});
    ChartOfAccounts(a).setActive(ChartOfAccounts(a).find(str("Retired Fees")), false);
    const QByteArray iif = LedgerExport::iif(a);
    std::unique_ptr<CompanyFile> b = company(dir, "target.qbooks", "creative");
    IifImportOptions options;
    options.commit = true;
    const IifImportReport r = IifImport::run(*b, iif, options);
    QVERIFY2(r.committed, qPrintable(r.error + QLatin1Char(' ') + r.notes.join(QLatin1Char('|'))));
    QVERIFY(r.refused.isEmpty());
    QVERIFY(r.unmappedAccounts.isEmpty());
    // Every balance and every list is the source's.
    for (const Account &acc : ChartOfAccounts(a).accounts(true)) {
        const qint64 there = ChartOfAccounts(*b).find(acc.fullName);
        QVERIFY2(there != 0, qPrintable(acc.fullName));
        QCOMPARE(ChartOfAccounts(*b).balance(there).minor(), ChartOfAccounts(a).balance(acc.id).minor());
    }
    for (ListKind kind : {ListKind::Accounts, ListKind::Customers, ListKind::Vendors}) {
        QCOMPARE(LedgerExport::listCsv(*b, kind), LedgerExport::listCsv(a, kind));
    }
    QCOMPARE(Lists(*b).items(true).size(), Lists(a).items(true).size());
    const Item print = *Lists(*b).item(Lists(*b).findItem(str("Printed menus")));
    QCOMPARE(print.kind, ItemKind::NonInventory);
    QCOMPARE(print.price.minor(), 185);
    QVERIFY(print.taxable);
    QVERIFY(b->integrityProblems().isEmpty());
    QVERIFY(iif.contains("\tPART\t"));
    // Importing it again adds nothing.
    QCOMPARE(IifImport::run(*b, iif, options).posted, 0);
}

void TestBooksIoIif::quickBooksOnlineListsImport()
{
    QTemporaryDir dir;
    std::unique_ptr<CompanyFile> f = company(dir, "qbo.qbooks");
    const ImportReport accounts = ListImport::importCsv(
        *f, ListKind::Accounts,
        "Account,Type,Detail Type,Description,Balance\n"
        "Accounts Receivable (A/R),Accounts receivable (A/R),Accounts Receivable (A/R),,1200.00\n"
        "Customer Deposits,Other Current Liabilities,Other Current Liabilities,Held for orders,0\n"
        "Equipment Loan,Long Term Liabilities,Notes Payable,,0\n"
        "Fees Billed,Income,Service/Fee Income,,0\n");
    QCOMPARE(accounts.added, 3);
    QCOMPARE(accounts.skipped, 1); // the company's own A/R
    QVERIFY2(accounts.errors.isEmpty(), qPrintable(accounts.errors.join(QLatin1Char('|'))));
    ChartOfAccounts chart(*f);
    QCOMPARE(chart.account(chart.find(str("Equipment Loan")))->type, AccountType::LongTermLiability);
    QCOMPARE(chart.account(chart.find(str("Customer Deposits")))->type, AccountType::OtherCurrentLiability);
    const ImportReport customers = ListImport::importCsv(
        *f, ListKind::Customers,
        "Customer,Phone Numbers,Email,Full Name,Billing Address\n"
        "Acme Corp,(555) 010-1234,ap@acme.test,Wile Coyote,\"1 Canyon Rd, Desert, AZ 85001\"\n"
        "Beta Books,,hello@beta.test,,\n");
    QCOMPARE(customers.added, 2);
    const Party acme = *Lists(*f).party(Lists(*f).findParty(PartyKind::Customer, str("Acme Corp")));
    QCOMPARE(acme.contactName, str("Wile Coyote"));
    QCOMPARE(acme.phone, str("(555) 010-1234"));
    QCOMPARE(acme.billingAddress, str("1 Canyon Rd, Desert, AZ 85001"));
    const ImportReport parts = ListImport::importCsv(
        *f, ListKind::Customers, "Name,Company,Email,Street,City,State,ZIP,Country\nRiver Co,River Co LLC,hi@river.test,5 Main St,Salem,OR,97301,USA\n");
    QCOMPARE(parts.added, 1);
    QCOMPARE(Lists(*f).party(Lists(*f).findParty(PartyKind::Customer, str("River Co")))->billingAddress,
             str("5 Main St\nSalem, OR 97301\nUSA"));
    const ImportReport items = ListImport::importCsv(
        *f, ListKind::Items,
        "Product/Service,Type,Description,Price,Cost,Taxable,Income Account,SKU,Qty On Hand\n"
        "Website build,Service,Five pages,1500,0,No,Fees Billed,WB-1,\n"
        "Printed mugs,Inventory,Ceramic mug,12.5,4,Yes,Fees Billed,MUG-1,40\n");
    QCOMPARE(items.added, 2);
    const Item mug = *Lists(*f).item(Lists(*f).findItem(str("Printed mugs")));
    QCOMPARE(mug.kind, ItemKind::NonInventory);
    QCOMPARE(mug.price.minor(), 1250);
    QVERIFY(mug.taxable);
    QCOMPARE(mug.incomeAccountId, chart.find(str("Fees Billed")));
}

QTEST_MAIN(TestBooksIoIif)

#include "tst_books_io_iif.moc"
