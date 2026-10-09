// SPDX-License-Identifier: GPL-3.0-or-later
// M68 (D-077) through the real window at 1280 and 520 px: the purchase
// order editor and receiving it on a bill, the weekly timesheet and the
// invoice's billable-time picker, Print Checks (queue and confirmation),
// the check alignment dialog, the Time workspace and the employee record.
// Each command opens its dialog; the dialog drives BooksService. With
// QQO_GRAB_DIR every screen is saved for review.

#include "books_window_scene.h"

using namespace BooksAppTest;
using QindaOffice::Test::saveGrab;

namespace {

QString str(const char *text)
{
    return QString::fromUtf8(text);
}

QSize sizeFor(int width)
{
    return width == 1280 ? QSize(1280, 800) : QSize(520, 700);
}

// Popup::close()/open() return nothing; WindowScene::call wants a value.
void closePopup(QObject *popup)
{
    QMetaObject::invokeMethod(popup, "close");
}

void openPopup(QObject *popup)
{
    QMetaObject::invokeMethod(popup, "open");
}

QVariantList linesOf(QObject *editor)
{
    return editor->property("doc").toMap().value(str("lines")).toList();
}

} // namespace

class QindaBooksM68DialogsTest final : public QObject {
    Q_OBJECT

private slots:
    void purchaseOrderAndReceiving_data()
    {
        QTest::addColumn<int>("width");
        QTest::newRow("1280") << 1280;
        QTest::newRow("520") << 520;
    }
    void purchaseOrderAndReceiving()
    {
        QFETCH(int, width);
        WindowScene scene(sizeFor(width));
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        BooksService *books = scene.host.books();
        const qint64 stock = idOf(books->saveItem({{str("name"), str("Cover stock")}, {str("type"), str("product")},
                                                   {str("cost"), str("25.00")}, {str("expenseAccountId"), scene.account(str("6500"))}}));
        QObject *editor = scene.openThrough(str("booksNewPurchaseOrderAction"), str("purchaseOrderEditor"));
        QVERIFY(editor != nullptr);
        WindowScene::call(editor, "edit", str("partyId"), scene.printer);
        WindowScene::call(editor, "edit", str("dueDate"), str("2026-04-10"));
        WindowScene::call(editor, "edit", str("lines"),
                          QVariantList{QVariantMap{{str("itemId"), stock}, {str("quantity"), str("10")}, {str("customerId"), scene.harbor},
                                                   {str("description"), str("Cover stock, 100 lb")}, {str("rate"), str("25.00")}},
                                       QVariantMap{{str("accountId"), scene.account(str("6500"))}, {str("description"), str("Freight")},
                                                   {str("amount"), str("20.00")}}});
        QTRY_COMPARE(editor->property("totals").toMap().value(str("totalText")).toString(), str("$270.00"));
        QTest::qWait(200);
        saveGrab(scene.window, str("qindabooks_po_editor_%1").arg(width));
        // Narrow, the lines keep their columns and scroll sideways.
        QQuickItem *linesScroll = scene.item(str("orderLinesScroll"));
        QVERIFY(linesScroll != nullptr);
        QVERIFY2(linesScroll->property("scrollableX").toBool() == (width == 520),
                 qPrintable(str("viewport %1, content %2, overflowX %3")
                                .arg(linesScroll->property("viewportWidth").toDouble())
                                .arg(linesScroll->property("contentWidth").toDouble())
                                .arg(linesScroll->property("overflowX").toInt())));
        QVERIFY(WindowScene::call(editor, "save").toBool());
        const QVariantList orders = books->documents({{str("kind"), str("orders")}});
        QCOMPARE(orders.size(), 1);
        const qint64 po = orders.first().toMap().value(str("id")).toLongLong();
        closePopup(editor);

        // A bill for the printer offers the order; receive 6 of the 10.
        QObject *bill = scene.openThrough(str("booksNewBillAction"), str("documentEditor"));
        QVERIFY(bill != nullptr);
        WindowScene::call(bill, "edit", str("partyId"), scene.printer);
        QTRY_COMPARE(bill->property("waiting").toInt(), 2);
        QTRY_VERIFY(scene.item(str("documentWaiting")) != nullptr && scene.item(str("documentWaiting"))->isVisible());
        QObject *receive = scene.object(str("receiveOrderDialog"));
        QVERIFY(receive != nullptr);
        WindowScene::call(receive, "openFor", scene.printer, str("Pioneer Print Shop"), QVariantList{});
        QTRY_VERIFY(receive->property("opened").toBool());
        const QVariantList rows = receive->property("rows").toList();
        QCOMPARE(rows.size(), 2);
        WindowScene::call(receive, "setQuantity", rows.first().toMap().value(str("lineId")), str("6"));
        QTest::qWait(250);
        saveGrab(scene.window, str("qindabooks_receive_%1").arg(width));
        QVERIFY(WindowScene::call(receive, "receive").toBool());
        closePopup(receive);
        QTRY_COMPARE(linesOf(bill).size(), 1);
        QCOMPARE(linesOf(bill).first().toMap().value(str("quantity")).toString(), str("6"));
        QTest::qWait(200);
        saveGrab(scene.window, str("qindabooks_bill_from_order_%1").arg(width));
        QVERIFY(WindowScene::call(bill, "save", str("open")).toBool());
        QCOMPARE(books->document(po).value(str("status")).toString(), str("partiallyReceived"));
        closePopup(bill);

        // The Purchase orders tab lists it; Create Bill follows the selection.
        scene.host.setCurrentView(str("expenses"));
        scene.host.setSelectedDocumentId(po);
        QVERIFY(scene.host.action(str("booksCreateBillFromOrderAction"))->isEnabled());
        QSignalSpy createBill(&scene.host, &BooksHost::billFromOrderRequested);
        QVERIFY(scene.host.actions()->trigger(str("booksCreateBillFromOrderAction")));
        QCOMPARE(createBill.count(), 1);
        QTRY_VERIFY(bill->property("opened").toBool());
        QCOMPARE(linesOf(bill).size(), 2); // the 4 left and the freight
        closePopup(bill);
    }

    void timesheetAndBillableTime_data() { purchaseOrderAndReceiving_data(); }
    void timesheetAndBillableTime()
    {
        QFETCH(int, width);
        WindowScene scene(sizeFor(width));
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        BooksService *books = scene.host.books();
        const QString maya = str("employee:%1").arg(idOf(books->saveEmployee({{str("name"), str("Maya Okafor")}})));
        const qint64 design = idOf(books->saveItem({{str("name"), str("Design")}, {str("type"), str("service")},
                                                    {str("price"), str("120.00")}, {str("incomeAccountId"), scene.account(str("4100"))}}));
        QVERIFY(ok(books->saveTimeEntry({{str("worker"), maya}, {str("customerId"), scene.harbor}, {str("itemId"), design},
                                         {str("date"), str("2026-03-02")}, {str("duration"), str("1:30")},
                                         {str("billable"), true}, {str("notes"), str("Logo sketches")}})));
        QObject *sheet = scene.openThrough(str("booksWeeklyTimesheetAction"), str("timesheetDialog"));
        QVERIFY(sheet != nullptr);
        WindowScene::call(sheet, "openDialog", maya, str("2026-03-04"));
        QTRY_COMPARE(sheet->property("sheet").toMap().value(str("weekStart")).toString(), str("2026-03-02"));
        WindowScene::call(sheet, "changeCell", 0, 1, str("0:45"));
        QVERIFY(WindowScene::call(sheet, "save").toBool());
        QTRY_COMPARE(sheet->property("sheet").toMap().value(str("totalText")).toString(), str("2:15"));
        QTest::qWait(250);
        saveGrab(scene.window, str("qindabooks_timesheet_%1").arg(width));
        QQuickItem *gridScroll = scene.item(str("timesheetScroll"));
        QVERIFY(gridScroll != nullptr);
        QCOMPARE(gridScroll->property("scrollableX").toBool(), width == 520); // narrow, the week scrolls sideways
        closePopup(sheet);

        // The invoice editor offers the customer's billable time.
        QObject *editor = scene.openThrough(str("booksNewInvoiceAction"), str("documentEditor"));
        QVERIFY(editor != nullptr);
        WindowScene::call(editor, "edit", str("partyId"), scene.harbor);
        QTRY_COMPARE(editor->property("waiting").toInt(), 2);
        QObject *picker = scene.object(str("billableTimeDialog"));
        QVERIFY(picker != nullptr);
        WindowScene::call(picker, "openFor", scene.harbor, str("Harbor & Pine Coffee"), QVariantList{});
        QTRY_VERIFY(picker->property("opened").toBool());
        WindowScene::call(picker, "pickAll");
        QTest::qWait(250);
        saveGrab(scene.window, str("qindabooks_billable_time_%1").arg(width));
        QVERIFY(WindowScene::call(picker, "addLines").toBool());
        closePopup(picker);
        QTRY_COMPARE(linesOf(editor).size(), 2);
        QTRY_COMPARE(editor->property("totals").toMap().value(str("totalText")).toString(), str("$270.00")); // 1.5 + 0.75 h × 120.00
        QVERIFY(WindowScene::call(editor, "save", str("open")).toBool());
        QCOMPARE(books->timeEntries({{str("status"), str("billed")}}).size(), 2);
        closePopup(editor);

        // The Time workspace lists it; a time activity opens from it.
        scene.host.setCurrentView(str("time"));
        QTRY_VERIFY(scene.item(str("timeEntries")) != nullptr && scene.item(str("timeEntries"))->isVisible());
        QTest::qWait(250);
        saveGrab(scene.window, str("qindabooks_time_%1").arg(width));
        QObject *activity = scene.openThrough(str("booksTimeActivityAction"), str("timeActivityDialog"));
        QVERIFY(activity != nullptr);
        QTest::qWait(200);
        saveGrab(scene.window, str("qindabooks_time_activity_%1").arg(width));
        closePopup(activity);
        QObject *employee = scene.openThrough(str("booksNewEmployeeAction"), str("employeeDialog"));
        QVERIFY(employee != nullptr);
        QTest::qWait(200);
        saveGrab(scene.window, str("qindabooks_employee_%1").arg(width));
        closePopup(employee);
    }

    void printChecksAndAlignment_data() { purchaseOrderAndReceiving_data(); }
    void printChecksAndAlignment()
    {
        QFETCH(int, width);
        WindowScene scene(sizeFor(width));
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        BooksService *books = scene.host.books();
        const qint64 checking = scene.account(str("1000"));
        for (const char *amount : {"120.00", "80.00"}) {
            QVERIFY(ok(books->saveDocument({{str("kind"), str("check")}, {str("partyId"), scene.printer},
                                            {str("paymentAccountId"), checking}, {str("toPrint"), true},
                                            {str("lines"), QVariantList{QVariantMap{{str("description"), str("Proofs")},
                                                                                    {str("accountId"), scene.account(str("6500"))},
                                                                                    {str("amount"), str(amount)}}}}})));
        }
        QObject *dialog = scene.openThrough(str("booksPrintChecksAction"), str("printChecksDialog"));
        QVERIFY(dialog != nullptr);
        QCOMPARE(dialog->property("rows").toList().size(), 2);
        QCOMPARE(dialog->property("firstNumber").toString(), str("1001"));
        QTest::qWait(250);
        saveGrab(scene.window, str("qindabooks_print_checks_%1").arg(width));
        QVERIFY(WindowScene::call(dialog, "printSelected").toBool());
        QCOMPARE(dialog->property("stage").toString(), str("confirm"));
        QCOMPARE(scene.host.printSourceKind(), str("checks"));
        openPopup(dialog);
        QTRY_VERIFY(dialog->property("opened").toBool());
        const QVariantList printed = dialog->property("printed").toList();
        QCOMPARE(printed.size(), 2);
        QVariantMap reprint;
        reprint.insert(printed.last().toMap().value(str("id")).toString(), true);
        dialog->setProperty("reprint", reprint);
        QTest::qWait(250);
        saveGrab(scene.window, str("qindabooks_print_checks_confirm_%1").arg(width));
        QVERIFY(!WindowScene::call(dialog, "finish").toBool()); // one waits to be reprinted
        QCOMPARE(dialog->property("stage").toString(), str("queue"));
        QCOMPARE(dialog->property("firstNumber").toString(), str("1003"));
        closePopup(dialog);

        // The two new ribbon tabs, for the review grabs (the gate in
        // tst_qindabooks_ui checks every item fits at 1320 and 520 px).
        QQuickItem *ribbon = scene.item(str("officeRibbon"));
        QVERIFY(ribbon != nullptr);
        for (const char *tab : {"purchases", "time"}) {
            QVariant shown;
            QMetaObject::invokeMethod(ribbon, "activateTab", Q_RETURN_ARG(QVariant, shown), Q_ARG(QVariant, str(tab)));
            QVERIFY2(shown.toBool(), tab);
            QTest::qWait(150);
            saveGrab(scene.window, str("qindabooks_ribbon_%1_%2").arg(str(tab)).arg(width));
        }

        QObject *alignment = scene.openThrough(str("booksCheckAlignmentAction"), str("checkAlignmentDialog"));
        QVERIFY(alignment != nullptr);
        alignment->setProperty("printer", str("Office Laser"));
        scene.item(str("alignmentX"))->setProperty("text", str("0.25"));
        scene.item(str("alignmentY"))->setProperty("text", str("-0.1"));
        QVERIFY(WindowScene::call(alignment, "save").toBool());
        QCOMPARE(books->checkAlignment(str("Office Laser")).value(str("description")).toString(), str("0.25 in right, 0.1 in up"));
        QTest::qWait(250);
        saveGrab(scene.window, str("qindabooks_check_alignment_%1").arg(width));
        closePopup(alignment);
    }
};

BOOKS_APP_TEST_MAIN(QindaBooksM68DialogsTest)

#include "tst_qindabooks_m68_dialogs.moc"
