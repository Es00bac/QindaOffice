// SPDX-License-Identifier: GPL-3.0-or-later
// M63 (D-071) through the real window: each new command opens its dialog,
// the dialog drives BooksService and shows the answer. QQO_GRAB_DIR saves a
// PNG of every dialog for review.

#include "books_window_scene.h"

#include <QImage>

using namespace BooksAppTest;
using QindaOffice::Test::saveGrab;

namespace {

QString str(const char *text)
{
    return QString::fromUtf8(text);
}

QUrl write(const QTemporaryDir &dir, const QString &name, const QByteArray &bytes)
{
    const QString path = dir.filePath(name);
    return writeFile(path, bytes) ? QUrl::fromLocalFile(path) : QUrl();
}

} // namespace

class QindaBooksDialogsTest final : public QObject {
    Q_OBJECT

private slots:
    // File ▸ Import and Export ▸ Import Lists: the user picks the list, the
    // rows land in THAT list, and the answer (counts, refused rows) is shown.
    void listsDialogImportsTheChosenListAndSaysWhatHappened()
    {
        WindowScene scene;
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QObject *dialog = scene.openThrough(str("booksImportListsAction"), str("listsDialog"));
        QVERIFY(dialog != nullptr);
        QCOMPARE(dialog->property("mode").toString(), str("import"));
        WindowScene::call(dialog, "openFor", str("import"), str("vendors"));
        QCOMPARE(dialog->property("kind").toString(), str("vendors"));
        const QUrl vendors = write(scene.dir, str("vendors.csv"),
                                   "Vendor,Email,1099\nCopperleaf Paper,orders@copperleaf.test,no\n,,\n"
                                   "Pioneer Print Shop,,\n");
        WindowScene::call(dialog, "importFile", vendors);
        QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(!find(scene.host.books()->parties(str("vendor")), str("name"), str("Copperleaf Paper")).isEmpty());
        QVERIFY(find(scene.host.books()->parties(str("customer")), str("name"), str("Copperleaf Paper")).isEmpty());
        const QVariantMap result = dialog->property("answer").toMap();
        QCOMPARE(result.value(str("added")).toInt(), 1);
        QCOMPARE(result.value(str("skipped")).toInt(), 1);
        QObject *notice = scene.object(str("listsResult"));
        QVERIFY(notice != nullptr);
        QTRY_VERIFY(notice->property("visible").toBool());
        QCOMPARE(notice->property("variant").toString(), str("success"));
        QTest::qWait(400); // the segment highlight animates
        saveGrab(scene.window, str("qindabooks_lists_import"));

        // A refused row is shown, with its row number, in the dialog.
        WindowScene::call(dialog, "openFor", str("import"), str("accounts"));
        WindowScene::call(dialog, "importFile",
                          write(scene.dir, str("chart.csv"), "Name,Type\nStudio Lights,Fixed asset\nOdd,Sideways\n"));
        QTRY_COMPARE(notice->property("variant").toString(), str("warning"));
        const QVariantList errors = dialog->property("errors").toList();
        QCOMPARE(errors.size(), 1);
        QVERIFY2(errors.first().toString().startsWith(str("row 3:")), qPrintable(errors.first().toString()));
        QTest::qWait(400);
        saveGrab(scene.window, str("qindabooks_lists_refused"));

        // Export: the other half, through its own command.
        QVERIFY(scene.host.actions()->trigger(str("booksExportListsAction")));
        QTRY_COMPARE(dialog->property("mode").toString(), str("export"));
        const QString out = scene.dir.filePath(str("customers.csv"));
        WindowScene::call(dialog, "openFor", str("export"), str("customers"));
        WindowScene::call(dialog, "exportFile", QUrl::fromLocalFile(out));
        QTRY_VERIFY(QFileInfo::exists(out));
        QCOMPARE(notice->property("variant").toString(), str("success"));
    }

    // File ▸ Company Logo: choose, preview, remove.
    void logoDialogStoresAndPreviewsThePicture()
    {
        WindowScene scene;
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QObject *dialog = scene.openThrough(str("booksCompanyLogoAction"), str("logoDialog"));
        QVERIFY(dialog != nullptr);
        QVERIFY(dialog->property("logo").toString().isEmpty());
        QImage picture(160, 64, QImage::Format_RGB32);
        picture.fill(QColor(0x1f, 0x5f, 0xbf));
        const QString png = scene.dir.filePath(str("lantern.png"));
        QVERIFY(picture.save(png));
        WindowScene::call(dialog, "useFile", QUrl::fromLocalFile(png));
        QTRY_VERIFY(dialog->property("logo").toString().startsWith(str("data:image/png")));
        QObject *image = scene.object(str("logoImage"));
        QVERIFY(image != nullptr);
        QTRY_COMPARE(image->property("status").toInt(), 1); // Image.Ready
        QTest::qWait(200);
        saveGrab(scene.window, str("qindabooks_logo"));
        WindowScene::call(dialog, "removeLogo");
        QVERIFY(!scene.host.books()->settings().value(str("hasLogo")).toBool());
    }

    // Sales ▸ Customer Statements: pick the customer, see the activity,
    // preview through the standard command, mail through QindaMail.
    void statementDialogPreviewsAndMails()
    {
        WindowScene scene;
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QObject *dialog = scene.openThrough(str("booksStatementsAction"), str("statementDialog"));
        QVERIFY(dialog != nullptr);
        QVERIFY(!dialog->property("shown").toMap().value(str("ok")).toBool());
        WindowScene::call(dialog, "choose", scene.harbor);
        const QVariantMap shown = dialog->property("shown").toMap();
        QVERIFY(shown.value(str("ok")).toBool());
        QCOMPARE(shown.value(str("closing")).toLongLong(), 120000);
        QTRY_VERIFY(scene.item(str("statementLines")) != nullptr);
        QTRY_COMPARE(scene.item(str("statementLines"))->property("count").toInt(), 1);
        QTest::qWait(300);
        saveGrab(scene.window, str("qindabooks_statement"));

        QStringList arguments;
        scene.host.books()->setLauncher([&](const QString &, const QStringList &a) {
            arguments = a;
            return true;
        });
        QVERIFY(WindowScene::call(dialog, "email").toMap().value(str("ok")).toBool());
        QCOMPARE(arguments.value(0), str("--attach"));
        QVERIFY(arguments.value(2).startsWith(str("mailto:ap@harbor.test")));

        WindowScene::call(dialog, "preview");
        QCOMPARE(scene.host.printSourceKind(), str("statement"));
        QTRY_VERIFY(!dialog->property("opened").toBool());
        QVERIFY(scene.host.action(str("filePrintPreviewAction"))->isEnabled());
    }

    // Sales ▸ Recurring Invoices: a schedule made in the editor shows in the
    // list, pauses, resumes and is deleted.
    void recurringDialogsCreatePauseAndDelete()
    {
        WindowScene scene;
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QObject *list = scene.openThrough(str("booksRecurringAction"), str("recurringDialog"));
        QVERIFY(list != nullptr);
        QCOMPARE(list->property("rows").toList().size(), 0);
        QObject *editor = scene.object(str("recurringEditor"));
        QVERIFY(editor != nullptr);
        WindowScene::call(editor, "openFor", 0);
        QTRY_VERIFY(editor->property("opened").toBool());
        scene.object(str("recurringName"))->setProperty("text", str("Monthly care plan"));
        scene.object(str("recurringCustomer"))->setProperty("currentIndex", 2); // Riverbend Books
        WindowScene::call(editor, "edit", str("lines"),
                          QVariantList{QVariantMap{{str("description"), str("Care plan")},
                                                   {str("accountId"), scene.account(str("4100"))},
                                                   {str("quantity"), str("1")},
                                                   {str("rate"), str("180.00")}}});
        QTRY_COMPARE(scene.object(str("recurringTotal"))->property("value").toString(), str("$180.00"));
        QTest::qWait(300);
        saveGrab(scene.window, str("qindabooks_recurring_editor"));
        const QVariantMap saved = WindowScene::call(editor, "save").toMap();
        QVERIFY2(saved.value(str("ok")).toBool(), qPrintable(saved.value(str("error")).toString()));
        QTRY_COMPARE(list->property("rows").toList().size(), 1);
        const QVariantMap row = list->property("rows").toList().first().toMap();
        QCOMPARE(row.value(str("customerName")).toString(), str("Riverbend Books"));
        QCOMPARE(row.value(str("nextDate")).toString(), str("2026-03-31"));
        QMetaObject::invokeMethod(editor, "close");
        QTest::qWait(300);
        saveGrab(scene.window, str("qindabooks_recurring_list"));
        const qint64 id = row.value(str("id")).toLongLong();
        WindowScene::call(list, "setActive", id, false);
        QTRY_VERIFY(!list->property("rows").toList().first().toMap().value(str("active")).toBool());
        WindowScene::call(list, "setActive", id, true);
        WindowScene::call(list, "remove", id);
        QTRY_COMPARE(list->property("rows").toList().size(), 0);
    }

    // Accounting ▸ Budgets: a year's amount splits into months and saves.
    void budgetDialogSpreadsAndSaves()
    {
        WindowScene scene;
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QObject *dialog = scene.openThrough(str("booksBudgetsAction"), str("budgetDialog"));
        QVERIFY(dialog != nullptr);
        QCOMPARE(dialog->property("year").toInt(), 2026);
        const QVariantList rows = dialog->property("rows").toList();
        int rentRow = -1;
        for (int i = 0; i < rows.size(); ++i) {
            if (rows.at(i).toMap().value(str("accountId")).toLongLong() == scene.account(str("6500"))) {
                rentRow = i;
            }
        }
        QVERIFY(rentRow >= 0);
        WindowScene::call(dialog, "setAnnual", rentRow, str("24000"));
        QTRY_VERIFY(dialog->property("dirty").toBool());
        const QVariantMap edited = dialog->property("rows").toList().at(rentRow).toMap();
        QCOMPARE(edited.value(str("months")).toList().first().toString(), str("2000.00"));
        dialog->setProperty("expanded", rentRow);
        QTest::qWait(300);
        saveGrab(scene.window, str("qindabooks_budget"));
        QVERIFY(WindowScene::call(dialog, "save").toMap().value(str("ok")).toBool());
        const QVariantMap stored = find(scene.host.books()->budget(2026).value(str("rows")).toList(), str("accountId"),
                                        scene.account(str("6500")));
        QCOMPARE(stored.value(str("annual")).toLongLong(), 2400000);
    }

    // Accounting ▸ Pay Sales Tax: the owed amount is offered and paid.
    void salesTaxDialogRecordsAPayment()
    {
        WindowScene scene;
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        BooksService *books = scene.host.books();
        QVERIFY(ok(books->saveTaxRate({{str("name"), str("Lane County")}, {str("percent"), str("8")},
                                       {str("agencyName"), str("Oregon Revenue")}})));
        const qint64 rate = books->taxRates().first().toMap().value(str("id")).toLongLong();
        QVERIFY(ok(books->saveDocument({{str("kind"), str("invoice")}, {str("partyId"), scene.riverbend},
                                        {str("date"), str("2026-03-10")}, {str("taxRateId"), rate},
                                        {str("lines"), QVariantList{QVariantMap{{str("description"), str("Print run")},
                                                                                {str("accountId"), scene.account(str("4000"))},
                                                                                {str("amount"), str("500.00")},
                                                                                {str("taxable"), true}}}}})));
        QObject *dialog = scene.openThrough(str("booksSalesTaxPaymentAction"), str("salesTaxPaymentDialog"));
        QVERIFY(dialog != nullptr);
        QCOMPARE(dialog->property("agencies").toList().size(), 1);
        QCOMPARE(scene.object(str("salesTaxAmount"))->property("text").toString(), str("40.00"));
        QTest::qWait(300);
        saveGrab(scene.window, str("qindabooks_sales_tax"));
        const QVariantMap paid = WindowScene::call(dialog, "record").toMap();
        QVERIFY2(paid.value(str("ok")).toBool(), qPrintable(paid.value(str("error")).toString()));
        QCOMPARE(books->salesTaxOwed(str("2026-03-31")).first().toMap().value(str("owed")).toLongLong(), 0);
    }

    // File ▸ Import from QuickBooks: preview, map the unknown account, import.
    void iifDialogPreviewsMapsAndImports()
    {
        WindowScene scene;
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QObject *dialog = scene.openThrough(str("booksImportIifAction"), str("iifImportDialog"));
        QVERIFY(dialog != nullptr);
        const QUrl url = write(scene.dir, str("books.iif"),
                               "!ACCNT\tNAME\tACCNTTYPE\r\nACCNT\tStudio Rent\tEXP\r\n"
                               "!VEND\tNAME\r\nVEND\tLakeside Properties\r\n"
                               "!TRNS\tTRNSTYPE\tDATE\tACCNT\tNAME\tAMOUNT\r\n!SPL\tTRNSTYPE\tDATE\tACCNT\tNAME\tAMOUNT\r\n"
                               "!ENDTRNS\r\nTRNS\tBILL\t03/01/2026\tAccounts Payable\tLakeside Properties\t-1500.00\r\n"
                               "SPL\tBILL\t03/01/2026\tStudio Rent\tLakeside Properties\t1500.00\r\nENDTRNS\r\n"
                               "TRNS\tCHECK\t03/02/2026\tOperating Checking\tLakeside Properties\t-1500.00\r\n"
                               "SPL\tCHECK\t03/02/2026\tStudio Rent\tLakeside Properties\t1500.00\r\nENDTRNS\r\n");
        WindowScene::call(dialog, "choose", url);
        QTRY_VERIFY(dialog->property("preview").toMap().value(str("ok")).toBool());
        QCOMPARE(dialog->property("refused").toList().size(), 1);
        QCOMPARE(dialog->property("unmapped").toList().size(), 1);
        QTest::qWait(300);
        saveGrab(scene.window, str("qindabooks_iif_preview"));
        WindowScene::call(dialog, "map", str("Operating Checking"), scene.account(str("1000")));
        QTRY_COMPARE(dialog->property("refused").toList().size(), 0);
        QCOMPARE(dialog->property("preview").toMap().value(str("posted")).toInt(), 2);
        QVERIFY(dialog->property("primaryEnabled").toBool());
        const QVariantMap answer = WindowScene::call(dialog, "importNow").toMap();
        QVERIFY2(answer.value(str("committed")).toBool(), qPrintable(answer.value(str("error")).toString()));
        QTRY_VERIFY(dialog->property("done").toBool());
        QTest::qWait(300);
        saveGrab(scene.window, str("qindabooks_iif_done"));
        QVERIFY(!find(scene.host.books()->parties(str("vendor")), str("name"), str("Lakeside Properties")).isEmpty());
    }

    void iifEmployeeTimePreview_data()
    {
        QTest::addColumn<int>("width");
        QTest::newRow("wide") << 1280;
        QTest::newRow("narrow") << 520;
    }

    void iifEmployeeTimePreview()
    {
        QFETCH(int, width);
        WindowScene scene(QSize(width, 840));
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QObject *dialog = scene.openThrough(str("booksImportIifAction"), str("iifImportDialog"));
        QVERIFY(dialog != nullptr);
        const QUrl url = write(scene.dir, str("time.iif"),
            "!ACCNT\tNAME\tACCNTTYPE\nACCNT\tConsulting Income\tINC\n"
            "!CUST\tNAME\nCUST\tHarbor Clinic\n!EMP\tNAME\nEMP\tAda Lee\n"
            "!INVITEM\tNAME\tINVITEMTYPE\tACCNT\tPRICE\n"
            "INVITEM\tConsulting\tSERV\tConsulting Income\t90.00\n"
            "!TIMEACT\tDATE\tJOB\tEMP\tITEM\tDURATION\tBILLINGSTATUS\n"
            "TIMEACT\t10/04/2026\tHarbor Clinic\tAda Lee\tConsulting\t1:30\t1\n");
        WindowScene::call(dialog, "choose", url);
        QTRY_VERIFY(dialog->property("preview").toMap().value(str("ok")).toBool());
        const QVariantMap preview = dialog->property("preview").toMap();
        QCOMPARE(find(preview.value(str("lists")).toList(), str("key"), str("employees")).value(str("added")).toInt(), 1);
        QCOMPARE(find(preview.value(str("lists")).toList(), str("key"), str("timeActivities")).value(str("added")).toInt(), 1);
        QCOMPARE(preview.value(str("timeAdded")).toInt(), 1);
        QVERIFY(dialog->property("primaryEnabled").toBool());
        QTest::qWait(250);
        saveGrab(scene.window, str("qindabooks_iif_time_preview_%1").arg(width));
        const QVariantMap answer = WindowScene::call(dialog, "importNow").toMap();
        QVERIFY2(answer.value(str("committed")).toBool(), qPrintable(answer.value(str("error")).toString()));
        QCOMPARE(answer.value(str("timeAdded")).toInt(), 1);
        QCOMPARE(scene.host.books()->timeEntries({}).size(), 1);
    }
};

BOOKS_APP_TEST_MAIN(QindaBooksDialogsTest)

#include "tst_qindabooks_dialogs.moc"
