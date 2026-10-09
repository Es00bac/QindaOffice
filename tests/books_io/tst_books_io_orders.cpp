// SPDX-License-Identifier: GPL-3.0-or-later
// M68 (D-077) in books_io: a purchase order printed through the forms path
// (vendor, ship-to, expected date, item and customer:job columns, the
// vendor message); QuickBooks' open purchase orders (PURCHORDR) imported
// from IIF as purchase orders that never post — and not twice; the time
// reports' durations as h:mm in CSV and as [h]:mm numbers in Excel.

#include "books_io_fixture.h"

#include "employees.h"
#include "form_renderer.h"
#include "iif_import.h"
#include "purchase_orders.h"
#include "report_output.h"
#include "report_xlsx.h"
#include "reports.h"
#include "sql_util.h"
#include "time_tracking.h"

#include <QTemporaryDir>
#include <QtTest>

#include <xlsxcell.h>
#include <xlsxdocument.h>
#include <xlsxformat.h>

using namespace QindaOffice::Books;
using namespace BooksIoFixture;

namespace {

QString str(const char *text)
{
    return QString::fromUtf8(text);
}

QByteArray iifText(const char *text)
{
    QByteArray bytes(text);
    bytes.replace('|', '\t');
    bytes.replace("\n", "\r\n");
    return bytes;
}

// QuickBooks Desktop's export of two purchase orders (one open, one
// received — PAID Y) on the non-posting "Purchase Orders" account, and a
// bill.
const char *const kOrders = R"IIF(!ACCNT|NAME|ACCNTTYPE|ACCNUM|EXTRA|HIDDEN
ACCNT|Operating Checking|BANK|1015||N
ACCNT|Accounts Payable|AP|2000||N
ACCNT|Design Income|INC|4010||N
ACCNT|Printing|COGS|5010||N
ACCNT|Office Supplies|EXP|6210||N
ACCNT|Freight|EXP|6300||N
ACCNT|Purchase Orders|NONPOSTING|||N
!CUST|NAME
CUST|Bluebird Bakery
!VEND|NAME|ADDR1|ADDR2
VEND|Paper Mill Supply|Paper Mill Supply|4 Mill Rd
!INVITEM|NAME|INVITEMTYPE|DESC|ACCNT|COGSACCNT|PRICE|COST|TAXABLE|HIDDEN
INVITEM|Card Stock|PART|100 lb card stock|Design Income|Printing|40.00|25.00|N|N
!TRNS|TRNSID|TRNSTYPE|DATE|ACCNT|NAME|AMOUNT|DOCNUM|MEMO|DUEDATE|PAID|SADDR1|SADDR2|SADDR3
!SPL|SPLID|TRNSTYPE|DATE|ACCNT|NAME|AMOUNT|DOCNUM|MEMO|QNTY|PRICE|INVITEM
!ENDTRNS
TRNS||PURCHORDR|03/02/2026|Purchase Orders|Paper Mill Supply|-345.00|PO-77|Spring menus|03/16/2026|N|Bluebird Studio|12 Ember Lane|"Eugene, OR 97401"
SPL||PURCHORDR|03/02/2026|Printing|Bluebird Bakery|300.00|PO-77|Card stock for the menus|12|25.00|Card Stock
SPL||PURCHORDR|03/02/2026|Freight||45.00|PO-77|Freight|||
ENDTRNS
TRNS||PURCHORDR|02/10/2026|Purchase Orders|Paper Mill Supply|-80.00|PO-70|Toner|02/20/2026|Y
SPL||PURCHORDR|02/10/2026|Office Supplies||80.00|PO-70|Toner cartridges|4|20.00|
ENDTRNS
TRNS||BILL|03/05/2026|Accounts Payable|Paper Mill Supply|-45.00|INV-9|Freight bill|04/04/2026
SPL||BILL|03/05/2026|Freight|Paper Mill Supply|45.00|INV-9|
ENDTRNS
)IIF";

const char *const kNoVendor = R"IIF(!TRNS|TRNSID|TRNSTYPE|DATE|ACCNT|NAME|AMOUNT|DOCNUM
!SPL|SPLID|TRNSTYPE|DATE|ACCNT|NAME|AMOUNT|DOCNUM|QNTY|PRICE
!ENDTRNS
TRNS||PURCHORDR|03/02/2026|Purchase Orders||-10.00|PO-9
SPL||PURCHORDR|03/02/2026|Freight||10.00|PO-9|1|10.00
ENDTRNS
)IIF";

const IifTypeCount *countOf(const IifImportReport &r, const char *type)
{
    for (const IifTypeCount &c : r.transactions) {
        if (c.type == QLatin1String(type)) {
            return &c;
        }
    }
    return nullptr;
}

std::unique_ptr<CompanyFile> blank(const QString &path)
{
    CompanySettings s;
    s.legalName = str("Bluebird Studio LLC");
    return CompanyFile::create(path, s, str("general"));
}

} // namespace

class TestBooksIoOrders final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QVERIFY(m_dir.isValid()); }

    void aPurchaseOrderPrints()
    {
        Company c = build(m_dir.filePath(str("po.qbooks")));
        BusinessDocument po;
        po.partyId = c.vendor;
        po.date = QDate(2026, 3, 2);
        po.dueDate = QDate(2026, 3, 16);
        po.shipTo = str("Northwind Creative\n418 Alder Street, Suite 3\nPortland, OR 97204");
        po.message = str("Please deliver to the loading dock.");
        DocumentLine menus;
        menus.itemId = c.itemPrint;
        menus.quantity = Decimal::fromInteger(400);
        menus.partyId = c.customer;
        DocumentLine freight;
        freight.accountId = ChartOfAccounts(*c.file).find(str("Production Costs"));
        freight.description = str("Freight");
        freight.amount = usd(4500);
        po.lines = {menus, freight};
        const Result r = PurchaseOrders(*c.file).create(po);
        QVERIFY2(r.ok, qPrintable(r.error));
        PrintableDocument printable;
        QString error;
        QVERIFY2(buildPrintableDocument(*c.file, r.id, &printable, &error), qPrintable(error));
        QCOMPARE(printable.kind, PrintableDocument::Kind::PurchaseOrder);
        QCOMPARE(printable.columns.size(), 6); // Item, Description, Customer, Qty, Rate, Amount
        QCOMPARE(printable.rows.at(0).at(0), str("Printed menus"));
        QCOMPARE(printable.rows.at(0).at(2), str("Harbor & Pine Coffee Co."));
        QCOMPARE(printable.rows.at(0).at(4), str("0.90")); // the item's cost
        QCOMPARE(printable.rows.at(0).at(5), str("360.00"));
        const QString pdf = m_dir.filePath(str("po.pdf"));
        QVERIFY2(FormRenderer::writeDocumentPdf(*c.file, r.id, pdf, &error), qPrintable(error));
        const QStringList pages = pdfPages(pdf);
        QCOMPARE(pages.size(), 1);
        for (const char *expected : {"PURCHASE ORDER", "PO-1001", "Pixel Print Works", "77 SE Water Ave", "SHIP TO",
                                     "418 Alder Street", "Expected", "Mar 16, 2026", "Printed menus", "Harbor",
                                     "Freight", "Order total", "$405.00", "Please deliver to the loading dock."}) {
            QVERIFY2(pages.first().contains(str(expected)), expected);
        }
        QVERIFY(!pages.first().contains(str("PAYMENT INSTRUCTIONS"))); // a vendor is not asked to pay
        saveGalleryPng(pdf, str("books-purchase-order.png"));
    }

    void quickBooksPurchaseOrdersImport()
    {
        std::unique_ptr<CompanyFile> file = blank(m_dir.filePath(str("iif.qbooks")));
        QVERIFY(file);
        const QByteArray text = iifText(kOrders);
        IifImportOptions preview;
        const IifImportReport dry = IifImport::run(*file, text, preview);
        QVERIFY2(dry.ok, qPrintable(dry.error));
        const IifTypeCount *orders = countOf(dry, "PURCHORDR");
        QVERIFY(orders != nullptr);
        QCOMPARE(orders->count, 2);
        QCOMPARE(orders->documents, 2);
        QVERIFY(dry.unmappedAccounts.isEmpty()); // "Purchase Orders" is not an account to map
        QVERIFY(dry.refused.isEmpty());
        QCOMPARE(Sql::scalarInt(file->database(), str("SELECT COUNT(*) FROM documents WHERE kind = 19")), 0); // rolled back

        IifImportOptions commit;
        commit.commit = true;
        const IifImportReport done = IifImport::run(*file, text, commit);
        QVERIFY2(done.committed, qPrintable(done.error));
        QCOMPARE(done.posted, 1); // the bill; purchase orders never post
        DocumentFilter filter;
        filter.kinds = {DocKind::PurchaseOrder};
        const QList<BusinessDocument> pos = Documents(*file).list(filter);
        QCOMPARE(pos.size(), 2);
        const BusinessDocument &toner = pos.at(0);
        const BusinessDocument &menus = pos.at(1);
        QCOMPARE(menus.number, str("PO-77"));
        QCOMPARE(menus.total.minor(), 34500);
        QCOMPARE(menus.dueDate, QDate(2026, 3, 16));
        QCOMPARE(menus.shipTo, str("Bluebird Studio\n12 Ember Lane\nEugene, OR 97401"));
        QCOMPARE(menus.memo, str("Spring menus"));
        QCOMPARE(menus.txnId, 0);
        QCOMPARE(menus.lines.size(), 2);
        QCOMPARE(menus.lines.at(0).quantity, Decimal::fromInteger(12));
        QCOMPARE(menus.lines.at(0).unitPrice.minor(), 2500);
        QCOMPARE(menus.lines.at(0).description, str("Card stock for the menus"));
        QVERIFY(menus.lines.at(0).itemId != 0);
        QCOMPARE(Lists(*file).party(menus.lines.at(0).partyId)->displayName, str("Bluebird Bakery"));
        QCOMPARE(menus.lines.at(1).amount.minor(), 4500);
        QCOMPARE(Documents(*file).status(menus, QDate(2026, 3, 31)), DocumentStatus::Open);
        QCOMPARE(toner.number, str("PO-70"));
        QCOMPARE(Documents(*file).status(toner, QDate(2026, 3, 31)), DocumentStatus::Closed); // PAID Y
        QCOMPARE(Sql::scalarInt(file->database(), str("SELECT COUNT(*) FROM journal_lines")), 2); // the bill's two
        QVERIFY(file->integrityProblems().isEmpty());

        // Importing the same file again adds nothing.
        const IifImportReport again = IifImport::run(*file, text, commit);
        QVERIFY(again.committed);
        QCOMPARE(countOf(again, "PURCHORDR")->duplicates, 2);
        QCOMPARE(countOf(again, "PURCHORDR")->documents, 0);
        QCOMPARE(Documents(*file).list(filter).size(), 2);
        // A purchase order without a vendor is refused with its lines.
        const IifImportReport refused = IifImport::run(*file, iifText(kNoVendor), preview);
        QCOMPARE(refused.refused.size(), 1);
        QVERIFY2(refused.refused.first().reason.contains(str("vendor")), qPrintable(refused.refused.first().reason));
        QCOMPARE(refused.refused.first().firstLine, 4);
    }

    // 1:30 + 4:35 of Design for one customer: "6:05" in the CSV and 365
    // minutes as a fraction of a day under [h]:mm in Excel.
    void durationsLeaveAsHoursAndMinutes()
    {
        std::unique_ptr<CompanyFile> file = blank(m_dir.filePath(str("time.qbooks")));
        QVERIFY(file);
        Party customer;
        customer.kind = PartyKind::Customer;
        customer.displayName = str("Bluebird Bakery");
        const qint64 bluebird = Lists(*file).createParty(customer).id;
        Employee maya;
        maya.name = str("Maya Okafor");
        const qint64 mayaId = Employees(*file).create(maya).id;
        for (const int minutes : {90, 275}) {
            TimeEntry e;
            e.date = QDate(2026, 3, 4);
            e.worker = Worker::employee(mayaId);
            e.customerId = bluebird;
            e.minutes = minutes;
            QVERIFY(TimeTracking(*file).create(e).ok);
        }
        const ReportTable t = Reports(*file).timeByJob(QDate(2026, 3, 1), QDate(2026, 3, 31));
        const QString csv = QString::fromUtf8(reportToCsv(t));
        QVERIFY2(csv.contains(str("6:05")), qPrintable(csv));
        const QString path = m_dir.filePath(str("time.xlsx"));
        QString error;
        QVERIFY2(ReportXlsx::write(t, path, &error), qPrintable(error));
        QXlsx::Document doc(path);
        QVERIFY(doc.isLoadPackage());
        int total = 0;
        for (int row = 1; row <= doc.dimension().lastRow(); ++row) {
            if (doc.read(row, 1).toString().trimmed() == str("Total")) {
                total = row;
            }
        }
        QVERIFY(total > 0);
        const std::shared_ptr<QXlsx::Cell> cell = doc.cellAt(total, 2);
        QCOMPARE(cell->value().typeId(), QMetaType::Double);
        QCOMPARE(qRound64(cell->value().toDouble() * 24 * 60), 365);
        QCOMPARE(cell->format().numberFormat(), str("[h]:mm"));
    }

private:
    QTemporaryDir m_dir;
};

QTEST_MAIN(TestBooksIoOrders)
#include "tst_books_io_orders.moc"
