// SPDX-License-Identifier: GPL-3.0-or-later
// BooksHost (M47): every command id exists with an icon and emits its
// request, the workspace radio group stays consistent, no two commands
// share a shortcut, commands wait for a company, a new window asks for the
// setup wizard, Save As switches the window to the copy (and the service
// follows), the print source drives Print/Preview/Export PDF, and the
// real BooksWindow.qml instantiates over the host.

#include "books_app_test_support.h"

#include "standard_actions.h"

#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QSignalSpy>

using namespace BooksAppTest;
using QindaOffice::Quick::ActionEntry;

namespace {

const QStringList kCommandIds{
    QStringLiteral("booksNewInvoiceAction"),       QStringLiteral("booksNewEstimateAction"),
    QStringLiteral("booksNewSalesReceiptAction"),  QStringLiteral("booksReceivePaymentAction"),
    QStringLiteral("booksNewCreditMemoAction"),    QStringLiteral("booksNewBillAction"),
    QStringLiteral("booksPayBillsAction"),         QStringLiteral("booksNewExpenseAction"),
    QStringLiteral("booksNewCheckAction"),         QStringLiteral("booksNewVendorCreditAction"),
    QStringLiteral("booksNewCustomerAction"),      QStringLiteral("booksNewVendorAction"),
    QStringLiteral("booksNewItemAction"),          QStringLiteral("booksNewAccountAction"),
    QStringLiteral("booksJournalEntryAction"),     QStringLiteral("booksTransferAction"),
    QStringLiteral("booksDepositAction"),          QStringLiteral("booksImportStatementAction"),
    QStringLiteral("booksReconcileAction"),        QStringLiteral("booksBankRulesAction"),
    QStringLiteral("booksRunReportAction"),        QStringLiteral("booksExportReportPdfAction"),
    QStringLiteral("booksExportReportCsvAction"),  QStringLiteral("booksExportIifAction"),
    QStringLiteral("booksExportJournalCsvAction"), QStringLiteral("booksImportListsAction"),
    QStringLiteral("booksBackupAction"),           QStringLiteral("booksCompanySettingsAction"),
    QStringLiteral("booksCloseBooksAction"),       QStringLiteral("booksVoidAction"),
    QStringLiteral("booksEmailDocumentAction"),    QStringLiteral("booksPrintDocumentAction"),
    QStringLiteral("booksViewDashboardAction"),    QStringLiteral("booksViewSalesAction"),
    QStringLiteral("booksViewExpensesAction"),     QStringLiteral("booksViewBankingAction"),
    QStringLiteral("booksViewAccountingAction"),   QStringLiteral("booksViewReportsAction"),
};

// A host with a company at `dir/name`.
QVariantMap withCompany(BooksHost &host, const QTemporaryDir &dir, const QString &name = QStringLiteral("co.qbooks"))
{
    host.books()->setToday(kToday);
    return host.books()->createCompany(url(dir, name), {{QStringLiteral("name"), QStringLiteral("Host Test Co")}},
                                       QStringLiteral("general"));
}

QStringList checkedViews(BooksHost &host)
{
    QStringList out;
    for (ActionEntry *e : host.actions()->entries()) {
        if (e->radioGroup() == QLatin1String("booksView") && e->isChecked()) {
            out << e->id();
        }
    }
    return out;
}

} // namespace

class QindaBooksHostTest final : public QObject {
    Q_OBJECT

private slots:
    void everyCommandExistsWithAnIcon()
    {
        BooksHost host;
        for (const QString &id : kCommandIds) {
            ActionEntry *e = host.action(id);
            QVERIFY2(e != nullptr, qPrintable(id));
            QVERIFY2(!e->iconName().isEmpty(), qPrintable(id));
            QVERIFY2(!e->plainText().isEmpty(), qPrintable(id));
        }
        QCOMPARE(host.action(QStringLiteral("booksNewInvoiceAction"))->shortcut(), QKeySequence(QStringLiteral("Ctrl+I")));
        QCOMPARE(host.action(QStringLiteral("booksViewReportsAction"))->shortcut(), QKeySequence(QStringLiteral("Ctrl+6")));
        QCOMPARE(host.action(QStringLiteral("fileNewAction"))->plainText(), QStringLiteral("New Company…"));
    }

    void noTwoCommandsShareAShortcut()
    {
        BooksHost host;
        QHash<QString, QString> owner;
        for (ActionEntry *e : host.actions()->entries()) {
            const QString key = e->shortcut().toString(QKeySequence::PortableText);
            if (key.isEmpty()) {
                continue;
            }
            QVERIFY2(!owner.contains(key), qPrintable(key + QStringLiteral(": ") + owner.value(key) + QStringLiteral(" / ") + e->id()));
            owner.insert(key, e->id());
        }
    }

    void commandsWaitForACompanyAndSetupIsRequested()
    {
        QTemporaryDir dir;
        BooksHost host;
        QSignalSpy setup(&host, &BooksHost::companySetupRequested);
        for (const QString &id : kCommandIds) {
            QVERIFY2(!host.action(id)->isEnabled(), qPrintable(id));
        }
        QVERIFY(host.action(QStringLiteral("fileOpenAction"))->isEnabled());
        QVERIFY(setup.wait(1000));
        QCOMPARE(setup.count(), 1);
        QVERIFY(ok(withCompany(host, dir)));
        QVERIFY(host.action(QStringLiteral("booksNewInvoiceAction"))->isEnabled());
        QVERIFY(host.action(QStringLiteral("booksViewSalesAction"))->isEnabled());
        // Selection- and source-bound commands wait for theirs.
        QVERIFY(!host.action(QStringLiteral("booksVoidAction"))->isEnabled());
        QVERIFY(!host.action(QStringLiteral("booksExportReportPdfAction"))->isEnabled());
        QVERIFY(!host.action(QStringLiteral("filePrintAction"))->isEnabled());
        host.setSelectedDocumentId(1);
        QVERIFY(host.action(QStringLiteral("booksVoidAction"))->isEnabled());
    }

    void workspaceRadioGroupIsConsistent()
    {
        QTemporaryDir dir;
        BooksHost host;
        QVERIFY(ok(withCompany(host, dir)));
        QCOMPARE(host.currentView(), QStringLiteral("dashboard"));
        QCOMPARE(checkedViews(host), QStringList{QStringLiteral("booksViewDashboardAction")});
        QSignalSpy viewChanged(&host, &BooksHost::currentViewChanged);
        QVERIFY(host.actions()->trigger(QStringLiteral("booksViewReportsAction")));
        QCOMPARE(host.currentView(), QStringLiteral("reports"));
        QCOMPARE(checkedViews(host), QStringList{QStringLiteral("booksViewReportsAction")});
        host.setCurrentView(QStringLiteral("banking"));
        QCOMPARE(checkedViews(host), QStringList{QStringLiteral("booksViewBankingAction")});
        host.setCurrentView(QStringLiteral("nowhere"));
        QCOMPARE(host.currentView(), QStringLiteral("banking"));
        QCOMPARE(viewChanged.count(), 2);
        QCOMPARE(BooksHost::viewKeys().size(), 6);
    }

    void commandsEmitTheirRequests()
    {
        QTemporaryDir dir;
        BooksHost host;
        QVERIFY(ok(withCompany(host, dir)));
        QSignalSpy editor(&host, &BooksHost::documentEditorRequested);
        const QList<QPair<QString, QString>> editors{
            {QStringLiteral("booksNewInvoiceAction"), QStringLiteral("invoice")},
            {QStringLiteral("booksNewEstimateAction"), QStringLiteral("estimate")},
            {QStringLiteral("booksNewSalesReceiptAction"), QStringLiteral("salesReceipt")},
            {QStringLiteral("booksNewCreditMemoAction"), QStringLiteral("creditMemo")},
            {QStringLiteral("booksNewBillAction"), QStringLiteral("bill")},
            {QStringLiteral("booksNewExpenseAction"), QStringLiteral("expense")},
            {QStringLiteral("booksNewCheckAction"), QStringLiteral("check")},
            {QStringLiteral("booksNewVendorCreditAction"), QStringLiteral("vendorCredit")}};
        for (const auto &[id, kind] : editors) {
            QVERIFY(host.actions()->trigger(id));
            QCOMPARE(editor.last().at(0).toString(), kind);
            QCOMPARE(editor.last().at(1).toLongLong(), 0);
        }
        QSignalSpy payment(&host, &BooksHost::paymentDialogRequested);
        host.actions()->trigger(QStringLiteral("booksReceivePaymentAction"));
        host.actions()->trigger(QStringLiteral("booksPayBillsAction"));
        QCOMPARE(payment.at(0).at(0).toString(), QStringLiteral("receive"));
        QCOMPARE(payment.at(1).at(0).toString(), QStringLiteral("payBills"));
        QSignalSpy party(&host, &BooksHost::partyDialogRequested);
        host.actions()->trigger(QStringLiteral("booksNewCustomerAction"));
        host.actions()->trigger(QStringLiteral("booksNewVendorAction"));
        QCOMPARE(party.at(0).at(0).toString(), QStringLiteral("customer"));
        QCOMPARE(party.at(1).at(0).toString(), QStringLiteral("vendor"));
        QSignalSpy exports(&host, &BooksHost::exportRequested);
        host.actions()->trigger(QStringLiteral("booksExportIifAction"));
        host.actions()->trigger(QStringLiteral("booksExportJournalCsvAction"));
        host.actions()->trigger(QStringLiteral("booksBackupAction"));
        QCOMPARE(exports.at(0).at(0).toString(), QStringLiteral("iif"));
        QCOMPARE(exports.at(1).at(0).toString(), QStringLiteral("journalCsv"));
        QCOMPARE(exports.at(2).at(0).toString(), QStringLiteral("backup"));
        const QList<QPair<QString, const char *>> plain{
            {QStringLiteral("booksNewItemAction"), SIGNAL(itemDialogRequested(qint64))},
            {QStringLiteral("booksNewAccountAction"), SIGNAL(accountDialogRequested(qint64))},
            {QStringLiteral("booksJournalEntryAction"), SIGNAL(journalEntryRequested())},
            {QStringLiteral("booksTransferAction"), SIGNAL(transferRequested())},
            {QStringLiteral("booksDepositAction"), SIGNAL(depositRequested())},
            {QStringLiteral("booksImportStatementAction"), SIGNAL(importStatementRequested())},
            {QStringLiteral("booksReconcileAction"), SIGNAL(reconcileRequested())},
            {QStringLiteral("booksBankRulesAction"), SIGNAL(bankRulesRequested())},
            {QStringLiteral("booksRunReportAction"), SIGNAL(reportRequested(QString))},
            {QStringLiteral("booksImportListsAction"), SIGNAL(importListsRequested())},
            {QStringLiteral("booksCompanySettingsAction"), SIGNAL(companySettingsRequested())},
            {QStringLiteral("booksCloseBooksAction"), SIGNAL(closeBooksRequested())}};
        for (const auto &[id, signal] : plain) {
            QSignalSpy spy(&host, signal);
            QVERIFY(host.actions()->trigger(id));
            QVERIFY2(spy.count() == 1, qPrintable(id));
        }
        QCOMPARE(host.currentView(), QStringLiteral("reports")); // Run Report switches the workspace
        QSignalSpy voided(&host, &BooksHost::voidRequested);
        host.setSelectedDocumentId(42);
        host.actions()->trigger(QStringLiteral("booksVoidAction"));
        QCOMPARE(voided.last().at(0).toLongLong(), 42);
    }

    void saveAsSwitchesTheWindowToTheCopy()
    {
        QTemporaryDir dir;
        BooksHost host;
        QVERIFY(ok(withCompany(host, dir, QStringLiteral("original.qbooks"))));
        BooksService *books = host.books();
        QVERIFY(ok(books->saveParty({{QStringLiteral("kind"), QStringLiteral("customer")}, {QStringLiteral("name"), QStringLiteral("Before")}})));
        QVERIFY(!host.isDirty());
        QSignalSpy companyChanged(books, &BooksService::companyChanged);
        QSignalSpy saved(&host, &BooksHost::saveFinished);
        host.saveAs();
        host.saveAsPathChosen(url(dir, QStringLiteral("copy")), QStringLiteral("QindaBooks company (*.qbooks)"));
        QCOMPARE(saved.count(), 1);
        QVERIFY(saved.first().first().toBool());
        const QString copy = dir.filePath(QStringLiteral("copy.qbooks"));
        QCOMPARE(host.document()->canonicalPath(), QFileInfo(copy).absoluteFilePath());
        QCOMPARE(books->file()->path(), QFileInfo(copy).absoluteFilePath());
        QCOMPARE(companyChanged.count(), 1);
        QVERIFY(host.windowTitle().contains(QStringLiteral("copy")));
        // The service works on the copy now; the original is untouched.
        QVERIFY(ok(books->saveParty({{QStringLiteral("kind"), QStringLiteral("customer")}, {QStringLiteral("name"), QStringLiteral("After")}})));
        QCOMPARE(books->parties(QStringLiteral("customer")).size(), 2);
        BooksDocument original;
        QVERIFY(original.load(dir.filePath(QStringLiteral("original.qbooks"))));
        BooksService view(&original);
        QCOMPARE(view.parties(QStringLiteral("customer")).size(), 1);
        // Save on an open company is a no-op that succeeds.
        host.save();
        QCOMPARE(saved.count(), 2);
        QVERIFY(saved.last().first().toBool());
    }

    void printSourceDrivesPrintAndExport()
    {
        QTemporaryDir dir;
        BooksHost host;
        QVERIFY(ok(withCompany(host, dir)));
        BooksService *books = host.books();
        QCOMPARE(host.createPrintJob(), nullptr);
        host.setPrintSource(QStringLiteral("report"), QStringLiteral("profit_and_loss"),
                            {{QStringLiteral("columns"), QStringLiteral("months")},
                             {QStringLiteral("from"), QStringLiteral("2025-04-01")},
                             {QStringLiteral("to"), QStringLiteral("2026-03-31")}});
        QCOMPARE(host.printSourceKind(), QStringLiteral("report"));
        QVERIFY(host.printLandscape()); // twelve month columns do not fit portrait
        QVERIFY(host.action(QStringLiteral("filePrintAction"))->isEnabled());
        QVERIFY(host.action(QStringLiteral("filePrintPreviewAction"))->isEnabled());
        QVERIFY(host.action(QStringLiteral("booksExportReportPdfAction"))->isEnabled());
        const QString reportPdf = dir.filePath(QStringLiteral("report.pdf"));
        QVERIFY(host.exportPdf(reportPdf));
        QVERIFY(QFileInfo(reportPdf).size() > 1000);

        const qint64 acme = idOf(books->saveParty({{QStringLiteral("kind"), QStringLiteral("customer")},
                                                   {QStringLiteral("name"), QStringLiteral("Acme")}}));
        const qint64 inv = idOf(books->saveDocument(
            {{QStringLiteral("kind"), QStringLiteral("invoice")},
             {QStringLiteral("partyId"), acme},
             {QStringLiteral("lines"), QVariantList{QVariantMap{
                  {QStringLiteral("description"), QStringLiteral("Design")},
                  {QStringLiteral("accountId"), find(books->accounts(), QStringLiteral("number"), QStringLiteral("4100")).value(QStringLiteral("id"))},
                  {QStringLiteral("amount"), QStringLiteral("750.00")}}}}}));
        QVERIFY(inv != 0);
        host.setPrintSource(QStringLiteral("document"), inv);
        QVERIFY(!host.printLandscape());
        QVERIFY(!host.action(QStringLiteral("booksExportReportPdfAction"))->isEnabled());
        const QString formPdf = dir.filePath(QStringLiteral("form.pdf"));
        QVERIFY(host.exportPdf(formPdf));
        QVERIFY(QFileInfo(formPdf).size() > 1000);
        // Print Document = source → the selected document, then print.
        QSignalSpy print(&host, &BooksHost::printRequested);
        host.clearPrintSource();
        QVERIFY(!host.action(QStringLiteral("filePrintAction"))->isEnabled());
        host.setSelectedDocumentId(inv);
        QVERIFY(host.actions()->trigger(QStringLiteral("booksPrintDocumentAction")));
        QCOMPARE(print.count(), 1);
        QCOMPARE(host.printSourceKind(), QStringLiteral("document"));
        QVERIFY(host.createPrintJob() != nullptr);
    }

    void theWindowInstantiatesOverTheHost()
    {
        QQmlApplicationEngine engine;
        engine.addImportPath(QStringLiteral(QQO_QINDATK_QML_ROOT));
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/QindaOffice/Books/App/BooksWindow.qml")));
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        BooksHost host;
        QObject *object = component.createWithInitialProperties({{QStringLiteral("host"), QVariant::fromValue(&host)}});
        auto *window = qobject_cast<QQuickWindow *>(object);
        QVERIFY2(window != nullptr, qPrintable(component.errorString()));
        host.attachWindow(window);
        QCOMPARE(window->width(), 1280);
    }
};

BOOKS_APP_TEST_MAIN(QindaBooksHostTest)

#include "tst_qindabooks_host.moc"
