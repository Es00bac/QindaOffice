// SPDX-License-Identifier: GPL-3.0-or-later
// M68 (D-077) through BooksService and BooksHost: purchase orders saved,
// received in part and in full by bills, put back by a void, closed by
// hand, printed and e-mailed; employees and time — a single activity, the
// weekly timesheet, billable time onto an invoice and off it again; the
// check print queue numbered, confirmed with a misprint, taken back; the
// printer alignment kept per printer, its test page and the "checks" and
// "alignment" print sources; the new reports run and export.

#include "books_app_test_support.h"

#include <QFileInfo>

using namespace BooksAppTest;

namespace {

QString str(const char *text)
{
    return QString::fromUtf8(text);
}

QVariantMap lineOf(const QVariantMap &doc, int index)
{
    return doc.value(str("lines")).toList().value(index).toMap();
}

struct Studio : Company {
    qint64 copperleaf = 0;
    qint64 harbor = 0;
    qint64 cover = 0;
    qint64 design = 0;

    Studio()
    {
        copperleaf = idOf(service.saveParty({{str("kind"), str("vendor")}, {str("name"), str("Copperleaf Paper")},
                                             {str("email"), str("orders@copperleaf.test")},
                                             {str("defaultAccountId"), account(str("5000"))}}));
        harbor = customer(str("Harbor Coffee"), str("ap@harbor.test"));
        cover = idOf(service.saveItem({{str("name"), str("Cover stock")}, {str("type"), str("product")},
                                       {str("cost"), str("25.00")}, {str("expenseAccountId"), account(str("5000"))}}));
        design = item(str("Design"), str("120.00"), str("4100"));
    }
    QVariantMap order()
    {
        return service.saveDocument(
            {{str("kind"), str("purchaseOrder")},
             {str("partyId"), copperleaf},
             {str("date"), str("2026-03-02")},
             {str("dueDate"), str("2026-03-16")},
             {str("shipTo"), str("Test Books LLC\n12 Ember Lane")},
             {str("notes"), str("Ship by ground")},
             {str("lines"), QVariantList{QVariantMap{{str("itemId"), cover}, {str("quantity"), str("10")},
                                                     {str("customerId"), harbor}},
                                         QVariantMap{{str("accountId"), account(str("6400"))},
                                                     {str("description"), str("Delivery")},
                                                     {str("amount"), str("20.00")}}}}});
    }
    QVariantMap bill(const QVariantList &lines, const char *date = "2026-03-10")
    {
        return service.saveDocument({{str("kind"), str("bill")}, {str("partyId"), copperleaf},
                                     {str("date"), str(date)}, {str("lines"), lines}});
    }
};

} // namespace

class QindaBooksM68Test final : public QObject {
    Q_OBJECT

private slots:
    void purchaseOrdersThroughTheService()
    {
        Studio c;
        QVERIFY(ok(c.created));
        const QVariantMap saved = c.order();
        QVERIFY2(ok(saved), qPrintable(saved.value(str("error")).toString()));
        QCOMPARE(saved.value(str("number")).toString(), str("PO-1001"));
        const qint64 po = idOf(saved);
        QVariantMap doc = c.service.document(po);
        QCOMPARE(doc.value(str("kind")).toString(), str("purchaseOrder"));
        QCOMPARE(doc.value(str("status")).toString(), str("open"));
        QCOMPARE(doc.value(str("totalText")).toString(), str("$270.00"));
        QCOMPARE(doc.value(str("balanceText")).toString(), str("$270.00")); // all of it still to receive
        QCOMPARE(doc.value(str("shipTo")).toString(), str("Test Books LLC\n12 Ember Lane"));
        QCOMPARE(lineOf(doc, 0).value(str("rate")).toString(), str("25.00"));
        QCOMPARE(lineOf(doc, 0).value(str("customerId")).toLongLong(), c.harbor);
        QCOMPARE(lineOf(doc, 0).value(str("remaining")).toString(), str("10"));
        QVERIFY(doc.value(str("canPrint")).toBool());
        QCOMPARE(c.service.documents({{str("kind"), str("orders")}, {str("status"), str("open")}}).size(), 1);
        QVERIFY(c.service.documents({{str("kind"), str("purchases")}}).isEmpty()); // not a bill
        const QVariantMap vendor = find(c.service.parties(str("vendor")), str("id"), c.copperleaf);
        QCOMPARE(vendor.value(str("openOrders")).toInt(), 1);
        QCOMPARE(vendor.value(str("toReceiveText")).toString(), str("$270.00"));

        // Receive 6 of the 10 on a bill.
        const QVariantList open = c.service.openOrderLines(c.copperleaf);
        QCOMPARE(open.size(), 2);
        const qint64 coverLine = open.first().toMap().value(str("lineId")).toLongLong();
        const qint64 deliveryLine = open.last().toMap().value(str("lineId")).toLongLong();
        const QVariantMap received = c.service.receiveOrderLines({QVariantMap{{str("lineId"), coverLine}, {str("quantity"), str("6")}}});
        QVERIFY2(ok(received), qPrintable(received.value(str("error")).toString()));
        const QVariantMap first = c.bill(received.value(str("lines")).toList());
        QVERIFY2(ok(first), qPrintable(first.value(str("error")).toString()));
        QCOMPARE(lineOf(c.service.document(idOf(first)), 0).value(str("orderNumber")).toString(), str("PO-1001"));
        doc = c.service.document(po);
        QCOMPARE(doc.value(str("status")).toString(), str("partiallyReceived"));
        QCOMPARE(doc.value(str("statusLabel")).toString(), str("Partially received"));
        QCOMPARE(doc.value(str("balanceText")).toString(), str("$120.00")); // 4 × 25.00 + 20.00
        QCOMPARE(lineOf(doc, 0).value(str("received")).toString(), str("6"));
        const QVariantMap tooMany = c.service.receiveOrderLines({QVariantMap{{str("lineId"), coverLine}, {str("quantity"), str("5")}}});
        QVERIFY(!ok(tooMany));
        QVERIFY2(tooMany.value(str("error")).toString().contains(str("at most 4")), qPrintable(tooMany.value(str("error")).toString()));

        // Create Bill receives the rest; voiding that bill puts it back.
        const QVariantMap draft = c.service.billFromOrder(po);
        QVERIFY(ok(draft));
        QCOMPARE(draft.value(str("draft")).toMap().value(str("lines")).toList().size(), 2);
        const QVariantMap rest = c.service.saveDocument(draft.value(str("draft")).toMap());
        QVERIFY2(ok(rest), qPrintable(rest.value(str("error")).toString()));
        QCOMPARE(c.service.document(po).value(str("status")).toString(), str("closed"));
        QVERIFY(!ok(c.service.billFromOrder(po))); // nothing left
        QVERIFY(ok(c.service.voidDocument(idOf(rest), str("wrong invoice"))));
        QCOMPARE(c.service.document(po).value(str("status")).toString(), str("partiallyReceived"));

        // Closing by hand: a line, then the whole order.
        QVERIFY(ok(c.service.setOrderLineClosed(deliveryLine, true)));
        doc = c.service.document(po);
        QVERIFY(lineOf(doc, 1).value(str("closed")).toBool());
        QCOMPARE(doc.value(str("balanceText")).toString(), str("$100.00"));
        QVERIFY(ok(c.service.setOrderClosed(po, true)));
        QCOMPARE(c.service.documents({{str("kind"), str("orders")}, {str("status"), str("closed")}}).size(), 1);
        QVERIFY(ok(c.service.setOrderClosed(po, false)));
        QVERIFY(!ok(c.service.voidDocument(po, str("cancel")))); // a bill received it

        // Print, PDF and e-mail go through the forms path.
        const QString pdf = c.dir.filePath(str("po.pdf"));
        QVERIFY(ok(c.service.exportDocumentPdf(po, QUrl::fromLocalFile(pdf))));
        QVERIFY(QFileInfo(pdf).size() > 1000);
        QStringList arguments;
        c.service.setLauncher([&arguments](const QString &, const QStringList &a) {
            arguments = a;
            return true;
        });
        const QVariantMap mailed = c.service.emailDocument(po);
        QVERIFY(ok(mailed));
        QCOMPARE(mailed.value(str("subject")).toString(), str("Purchase Order PO-1001 from Test Books LLC"));
        QVERIFY(arguments.value(2).startsWith(str("mailto:orders@copperleaf.test")));
        QVERIFY(!arguments.value(2).contains(str("PAYMENT"), Qt::CaseInsensitive));

        // The report, worked by hand: PO-1001 270.00 with 100.00 to receive.
        const QVariantMap report = c.service.runReport(str("open_purchase_orders"), {{str("asOf"), str("2026-03-31")}});
        QVERIFY(ok(report));
        const QVariantMap total = find(report.value(str("rows")).toList(), str("key"), str("total"));
        QCOMPARE(total.value(str("cells")).toList().at(3).toMap().value(str("value")).toLongLong(), 27000);
        QCOMPARE(total.value(str("cells")).toList().at(4).toMap().value(str("value")).toLongLong(), 10000);
        QVERIFY(ok(c.service.exportReport(str("open_purchase_orders"), {}, url(c.dir, str("po.xlsx")), str("xlsx"))));
    }

    void timeThroughTheService()
    {
        Studio c;
        const QVariantMap maya = c.service.saveEmployee({{str("name"), str("Maya Okafor")}, {str("email"), str("maya@test")}});
        QVERIFY2(ok(maya), qPrintable(maya.value(str("error")).toString()));
        QVERIFY(!ok(c.service.saveEmployee({{str("name"), str("maya okafor")}})));         // the same name
        QVERIFY(!ok(c.service.saveEmployee({{str("name"), str("Lee")}, {str("rate"), str("12.345")}}))); // too many decimals
        const qint64 lee = idOf(c.service.saveEmployee({{str("name"), str("Lee Brandt")}, {str("rate"), str("90.00")}}));
        QCOMPARE(c.service.employee(lee).value(str("rateText")).toString(), str("$90.00"));
        const QString mayaKey = str("employee:%1").arg(idOf(maya));
        const QVariantList workers = c.service.workers();
        QCOMPARE(find(workers, str("key"), mayaKey).value(str("kindLabel")).toString(), str("Employee"));
        QCOMPARE(find(workers, str("key"), str("vendor:%1").arg(c.copperleaf)).value(str("kindLabel")).toString(), str("Vendor"));

        const auto activity = [&](const QString &worker, const char *date, const char *duration, const char *notes) {
            return c.service.saveTimeEntry({{str("worker"), worker}, {str("customerId"), c.harbor}, {str("itemId"), c.design},
                                            {str("date"), str(date)}, {str("duration"), str(duration)},
                                            {str("billable"), true}, {str("notes"), str(notes)}});
        };
        const QVariantMap sketches = activity(mayaKey, "2026-03-02", "1:30", "Logo sketches");
        QVERIFY2(ok(sketches), qPrintable(sketches.value(str("error")).toString()));
        QVERIFY(!ok(activity(mayaKey, "2026-03-02", "abc", "")));
        QVERIFY(ok(activity(str("employee:%1").arg(lee), "2026-03-03", "1.0", "Proofs")));
        QCOMPARE(c.service.timeEntry(idOf(sketches)).value(str("duration")).toString(), str("1:30"));
        QCOMPARE(c.service.timeEntries({{str("status"), str("unbilled")}}).size(), 2);

        // The weekly timesheet: Monday 1:30 → 2:00, Tuesday 0:45 more.
        QVariantMap sheet = c.service.timesheet(mayaKey, str("2026-03-04"));
        QVERIFY(ok(sheet));
        QCOMPARE(sheet.value(str("weekStart")).toString(), str("2026-03-02"));
        QVariantList rows = sheet.value(str("rows")).toList();
        QCOMPARE(rows.size(), 1);
        QVariantMap row = rows.first().toMap();
        QVariantList cells = row.value(str("cells")).toList();
        QCOMPARE(cells.at(0).toMap().value(str("duration")).toString(), str("1:30"));
        QVariantMap monday = cells.at(0).toMap();
        monday.insert(str("duration"), str("2:00"));
        cells[0] = monday;
        QVariantMap tuesday = cells.at(1).toMap();
        tuesday.insert(str("duration"), str("0:45"));
        cells[1] = tuesday;
        row.insert(str("cells"), cells);
        const QVariantMap savedWeek = c.service.saveTimesheet({{str("worker"), mayaKey}, {str("weekStart"), str("2026-03-02")},
                                                              {str("rows"), QVariantList{row}}});
        QVERIFY2(ok(savedWeek), qPrintable(savedWeek.value(str("error")).toString()));
        sheet = c.service.timesheet(mayaKey, str("2026-03-02"));
        QCOMPARE(sheet.value(str("totalText")).toString(), str("2:45"));
        QCOMPARE(sheet.value(str("dayTotals")).toList().at(1).toString(), str("0:45"));

        // Billable time onto an invoice: Maya 2:00 + 0:45 at 120.00, Lee 1:00 at his 90.00.
        const QVariantList unbilled = c.service.unbilledTime(c.harbor);
        QCOMPARE(unbilled.size(), 3);
        QVariantList ids;
        for (const QVariant &v : unbilled) {
            ids << v.toMap().value(str("id"));
        }
        QCOMPARE(find(unbilled, str("workerName"), str("Lee Brandt")).value(str("amountText")).toString(), str("$90.00"));
        const QVariantMap lines = c.service.timeInvoiceLines(c.harbor, ids);
        QVERIFY(ok(lines));
        const QVariantMap invoice = c.service.saveDocument({{str("kind"), str("invoice")}, {str("partyId"), c.harbor},
                                                           {str("date"), str("2026-03-31")},
                                                           {str("lines"), lines.value(str("lines"))}});
        QVERIFY2(ok(invoice), qPrintable(invoice.value(str("error")).toString()));
        // 2.0 × 120.00 + 0.75 × 120.00 + 1.0 × 90.00
        QCOMPARE(c.service.document(idOf(invoice)).value(str("totalText")).toString(), str("$420.00"));
        QCOMPARE(c.service.timeEntries({{str("status"), str("billed")}}).size(), 3);
        QVERIFY(c.service.unbilledTime(c.harbor).isEmpty());
        QCOMPARE(c.service.timeEntry(idOf(sketches)).value(str("invoiceNumber")).toString(),
                 c.service.document(idOf(invoice)).value(str("number")).toString());
        QVERIFY(!ok(c.service.deleteTimeEntry(idOf(sketches)))); // billed
        QVERIFY(ok(c.service.voidDocument(idOf(invoice), str("redo"))));
        QCOMPARE(c.service.unbilledTime(c.harbor).size(), 3);

        // The time reports run and export (durations as h:mm).
        const QVariantMap byJob = c.service.runReport(str("time_by_job"), {{str("from"), str("2026-03-01")}, {str("to"), str("2026-03-31")}});
        QVERIFY(ok(byJob));
        QCOMPARE(byJob.value(str("columns")).toList().at(1).toMap().value(str("type")).toString(), str("duration"));
        const QVariantMap total = find(byJob.value(str("rows")).toList(), str("key"), str("total"));
        QCOMPARE(total.value(str("cells")).toList().at(1).toMap().value(str("text")).toString(), str("3:45"));
        for (const char *id : {"time_by_job", "time_by_name", "unbilled_time_by_job"}) {
            QVERIFY2(ok(c.service.exportReport(str(id), {}, url(c.dir, str(id) + str(".csv")), str("csv"))), id);
        }
    }

    void theCheckQueueAndAlignment()
    {
        QTemporaryDir dir;
        BooksHost host;
        BooksService *books = host.books();
        books->setToday(kToday);
        QVERIFY(ok(books->createCompany(url(dir, str("checks.qbooks")), {{str("name"), str("Check Co")}}, str("general"))));
        const auto account = [books](const char *number) {
            return find(books->accounts(), str("number"), str(number)).value(str("id")).toLongLong();
        };
        const qint64 checking = account("1000");
        const qint64 vendor = idOf(books->saveParty({{str("kind"), str("vendor")}, {str("name"), str("Pixel Print Works")}}));
        const QVariantMap line{{str("description"), str("Proofs")}, {str("accountId"), account("6400")}, {str("amount"), str("120.00")}};
        const QVariantMap check = books->saveDocument({{str("kind"), str("check")}, {str("partyId"), vendor},
                                                      {str("paymentAccountId"), checking}, {str("toPrint"), true},
                                                      {str("number"), str("77")}, {str("lines"), QVariantList{line}}});
        QVERIFY2(ok(check), qPrintable(check.value(str("error")).toString()));
        QVERIFY(books->document(idOf(check)).value(str("toPrint")).toBool());
        QCOMPARE(books->document(idOf(check)).value(str("number")).toString(), QString());
        const qint64 bill = idOf(books->saveDocument({{str("kind"), str("bill")}, {str("partyId"), vendor},
                                                      {str("date"), str("2026-03-01")}, {str("lines"), QVariantList{line}}}));
        const QVariantMap paid = books->payBills({{str("paymentAccountId"), checking}, {str("toPrint"), true},
                                                  {str("bills"), QVariantList{QVariantMap{{str("billId"), bill}, {str("amount"), str("120.00")}}}}});
        QVERIFY(ok(paid));
        QCOMPARE(books->checkQueue(checking).size(), 2);
        QCOMPARE(books->nextCheckNumber(checking), 1001);

        const QVariantList ids{idOf(check), paid.value(str("ids")).toList().first()};
        const QVariantMap run = books->assignCheckNumbers(checking, ids, str("1001"));
        QVERIFY2(ok(run), qPrintable(run.value(str("error")).toString()));
        QCOMPARE(run.value(str("checks")).toList().last().toMap().value(str("number")).toString(), str("1002"));
        QVERIFY(books->checkQueue(checking).isEmpty());
        host.setPrintSource(str("checks"), ids);
        QCOMPARE(host.printSourceKind(), str("checks"));
        const QString pdf = dir.filePath(str("run.pdf"));
        QVERIFY(host.exportPdf(pdf));
        QVERIFY(QFileInfo(pdf).size() > 1000);
        // The bill payment jammed: back in the queue, 1002 recorded void.
        QVERIFY(ok(books->confirmCheckPrint({ids.last()})));
        QCOMPARE(books->checkQueue(checking).size(), 1);
        QCOMPARE(find(books->documents({{str("kind"), str("check")}, {str("status"), str("void")}}), str("number"), str("1002"))
                     .value(str("statusLabel")).toString(), str("Void"));
        QCOMPARE(books->nextCheckNumber(checking), 1003);
        QVERIFY(!ok(books->assignCheckNumbers(checking, {ids.last()}, str("1002")))); // a used number
        QVERIFY(ok(books->assignCheckNumbers(checking, {ids.last()}, str("1003"))));
        QVERIFY(ok(books->takeBackChecks(checking, {ids.last()}))); // that run never printed
        QCOMPARE(books->checkQueue(checking).size(), 1);
        QCOMPARE(books->nextCheckNumber(checking), 1003);

        // Alignment: per printer, kept as typed, refused beyond an inch.
        const QVariantMap aligned = books->saveCheckAlignment({{str("printer"), str("Office Laser")}, {str("x"), str("0.25")},
                                                               {str("y"), str("0.5")}, {str("unit"), str("in")}});
        QVERIFY2(ok(aligned), qPrintable(aligned.value(str("error")).toString()));
        QCOMPARE(aligned.value(str("description")).toString(), str("0.25 in right, 0.5 in down"));
        QCOMPARE(books->checkAlignment(str("Office Laser")).value(str("x")).toString(), str("0.25"));
        QVERIFY(!find(books->checkPrinters(), str("name"), str("Office Laser")).isEmpty());
        QVERIFY(!ok(books->saveCheckAlignment({{str("printer"), str("Office Laser")}, {str("x"), str("2")}, {str("unit"), str("in")}})));
        QVERIFY(!ok(books->saveCheckAlignment({{str("x"), str("0.1")}})));
        const QString page = dir.filePath(str("alignment.pdf"));
        QVERIFY(ok(books->exportAlignmentTestPdf(str("Office Laser"), QUrl::fromLocalFile(page))));
        QVERIFY(QFileInfo(page).size() > 1000);
        host.setPrintSource(str("alignment"), str("Office Laser"));
        QCOMPARE(host.printSourceKind(), str("alignment"));
        QVERIFY(host.createPrintJob() != nullptr);
        QVERIFY(host.exportPdf(dir.filePath(str("alignment-host.pdf"))));
    }
};

BOOKS_APP_TEST_MAIN(QindaBooksM68Test)

#include "tst_qindabooks_m68.moc"
