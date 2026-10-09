// SPDX-License-Identifier: GPL-3.0-or-later
// BooksHost (M47): every command id exists with an icon and emits its
// request, the workspace radio group stays consistent, no two commands
// share a shortcut, commands wait for a company, a new window asks for the
// setup wizard, Save As switches the window to the copy (and the service
// follows), the print source drives Print/Preview/Export PDF, Print Check
// prints the selected check (M63), and the real BooksWindow.qml
// instantiates over the host.

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
    QStringLiteral("booksSyncBankAction"),         QStringLiteral("booksConnectionsAction"),
    // M63 (D-071)
    QStringLiteral("booksExportListsAction"),      QStringLiteral("booksCompanyLogoAction"),
    QStringLiteral("booksStatementsAction"),       QStringLiteral("booksRecurringAction"),
    QStringLiteral("booksBudgetsAction"),          QStringLiteral("booksSalesTaxPaymentAction"),
    QStringLiteral("booksImportIifAction"),        QStringLiteral("booksExportReportXlsxAction"),
    QStringLiteral("booksPrintCheckAction"),
    // M68 (D-077)
    QStringLiteral("booksNewPurchaseOrderAction"),  QStringLiteral("booksCreateBillFromOrderAction"),
    QStringLiteral("booksPrintChecksAction"),       QStringLiteral("booksCheckAlignmentAction"),
    QStringLiteral("booksTimeActivityAction"),      QStringLiteral("booksWeeklyTimesheetAction"),
    QStringLiteral("booksNewEmployeeAction"),       QStringLiteral("booksViewTimeAction"),
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
        // M48: the Lucide names QindaTK ships (qtk-preview --list-icons).
        QCOMPARE(host.action(QStringLiteral("booksSyncBankAction"))->shortcut(), QKeySequence(QStringLiteral("F5")));
        QCOMPARE(host.action(QStringLiteral("booksSyncBankAction"))->iconName(), QStringLiteral("refresh-cw"));
        QCOMPARE(host.action(QStringLiteral("booksSyncBankAction"))->plainText(), QStringLiteral("Sync Mercury & Stripe"));
        QCOMPARE(host.action(QStringLiteral("booksConnectionsAction"))->iconName(), QStringLiteral("link"));
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
        viewChanged.clear();
        QCOMPARE(BooksHost::viewKeys().size(), 8); // M68: Time; M71: Payroll
        host.setCurrentView(QStringLiteral("time"));
        QCOMPARE(checkedViews(host), QStringList{QStringLiteral("booksViewTimeAction")});
        QCOMPARE(host.action(QStringLiteral("booksViewTimeAction"))->shortcut(), QKeySequence(QStringLiteral("Ctrl+7")));
        host.setCurrentView(QStringLiteral("payroll"));
        QCOMPARE(checkedViews(host), QStringList{QStringLiteral("booksViewPayrollAction")});
        QCOMPARE(host.action(QStringLiteral("booksViewPayrollAction"))->shortcut(), QKeySequence(QStringLiteral("Ctrl+8")));
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
            {QStringLiteral("booksNewVendorCreditAction"), QStringLiteral("vendorCredit")},
            {QStringLiteral("booksNewPurchaseOrderAction"), QStringLiteral("purchaseOrder")}};
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
            {QStringLiteral("booksConnectionsAction"), SIGNAL(connectionsRequested())},
            {QStringLiteral("booksReconcileAction"), SIGNAL(reconcileRequested())},
            {QStringLiteral("booksBankRulesAction"), SIGNAL(bankRulesRequested())},
            {QStringLiteral("booksRunReportAction"), SIGNAL(reportRequested(QString))},
            {QStringLiteral("booksImportListsAction"), SIGNAL(importListsRequested())},
            {QStringLiteral("booksCompanySettingsAction"), SIGNAL(companySettingsRequested())},
            {QStringLiteral("booksCloseBooksAction"), SIGNAL(closeBooksRequested())},
            // M68 (D-077)
            {QStringLiteral("booksPrintChecksAction"), SIGNAL(printChecksRequested())},
            {QStringLiteral("booksCheckAlignmentAction"), SIGNAL(checkAlignmentRequested())},
            {QStringLiteral("booksTimeActivityAction"), SIGNAL(timeActivityRequested(qint64))},
            {QStringLiteral("booksWeeklyTimesheetAction"), SIGNAL(timesheetRequested())},
            {QStringLiteral("booksNewEmployeeAction"), SIGNAL(employeeDialogRequested(qint64))},
            // M71 (D-081); Payroll Tax Forms last, as it opens Reports.
            {QStringLiteral("booksPayrollSetupAction"), SIGNAL(payrollSetupRequested())},
            {QStringLiteral("booksPayOwnerAction"), SIGNAL(payOwnerRequested(QString))},
            {QStringLiteral("booksDistributionAction"), SIGNAL(payOwnerRequested(QString))},
            {QStringLiteral("booksEstimatedTaxesAction"), SIGNAL(estimatedTaxesRequested())},
            {QStringLiteral("booksPayrollEmployeesAction"), SIGNAL(payrollEmployeesRequested())},
            {QStringLiteral("booksRunPayrollAction"), SIGNAL(runPayrollRequested())},
            {QStringLiteral("booksPayLiabilitiesAction"), SIGNAL(payLiabilitiesRequested())},
            {QStringLiteral("booksExportEfw2Action"), SIGNAL(efw2ExportRequested())},
            {QStringLiteral("booksPayrollFormsAction"), SIGNAL(payrollFormsRequested())}};
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
        // M71: Pay Owner and Shareholder Distribution share a dialog; the
        // planner and the employee list open the Payroll workspace.
        QSignalSpy owner(&host, &BooksHost::payOwnerRequested);
        host.actions()->trigger(QStringLiteral("booksPayOwnerAction"));
        host.actions()->trigger(QStringLiteral("booksDistributionAction"));
        QCOMPARE(owner.at(0).at(0).toString(), QStringLiteral("draw"));
        QCOMPARE(owner.at(1).at(0).toString(), QStringLiteral("distribution"));
        host.actions()->trigger(QStringLiteral("booksEstimatedTaxesAction"));
        QCOMPARE(host.currentView(), QStringLiteral("payroll"));
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

    // M63 (D-071 item 7): Print Check follows the selection, prints the
    // check onto voucher stock, and a check chosen for Print is the check.
    void printCheckPrintsTheSelectedCheck()
    {
        QTemporaryDir dir;
        BooksHost host;
        QVERIFY(ok(withCompany(host, dir)));
        BooksService *books = host.books();
        const qint64 vendor = idOf(books->saveParty({{QStringLiteral("kind"), QStringLiteral("vendor")},
                                                     {QStringLiteral("name"), QStringLiteral("Pixel Print Works")},
                                                     {QStringLiteral("billingAddress"), QStringLiteral("77 SE Water Ave")}}));
        const auto paid = [&](const QString &kind) {
            return idOf(books->saveDocument(
                {{QStringLiteral("kind"), kind},
                 {QStringLiteral("partyId"), vendor},
                 {QStringLiteral("paymentAccountId"), find(books->accounts(), QStringLiteral("number"), QStringLiteral("1000")).value(QStringLiteral("id"))},
                 {QStringLiteral("number"), QStringLiteral("1001")},
                 {QStringLiteral("notes"), QStringLiteral("Proof paper")},
                 {QStringLiteral("lines"), QVariantList{QVariantMap{
                      {QStringLiteral("description"), QStringLiteral("Paper")},
                      {QStringLiteral("accountId"), find(books->accounts(), QStringLiteral("number"), QStringLiteral("6400")).value(QStringLiteral("id"))},
                      {QStringLiteral("amount"), QStringLiteral("120.00")}}}}}));
        };
        const qint64 check = paid(QStringLiteral("check"));
        const qint64 expense = paid(QStringLiteral("expense"));
        QVERIFY(check != 0 && expense != 0);
        ActionEntry *printCheck = host.action(QStringLiteral("booksPrintCheckAction"));
        QVERIFY(!printCheck->isEnabled());
        host.setSelectedDocumentId(expense); // a debit-card expense is no check
        QVERIFY(!printCheck->isEnabled());
        host.setSelectedDocumentId(check);
        QVERIFY(printCheck->isEnabled());
        QSignalSpy print(&host, &BooksHost::printRequested);
        QVERIFY(host.actions()->trigger(QStringLiteral("booksPrintCheckAction")));
        QCOMPARE(print.count(), 1);
        QCOMPARE(host.printSourceKind(), QStringLiteral("check"));
        QVERIFY(host.createPrintJob() != nullptr);
        const QString pdf = dir.filePath(QStringLiteral("check.pdf"));
        QVERIFY(host.exportPdf(pdf));
        QVERIFY(QFileInfo(pdf).size() > 1000);
        // Save PDF in the editor writes the check page too.
        QVERIFY(ok(books->exportDocumentPdf(check, QUrl::fromLocalFile(dir.filePath(QStringLiteral("saved.pdf"))))));
        QVERIFY(!ok(books->exportDocumentPdf(expense, QUrl::fromLocalFile(dir.filePath(QStringLiteral("expense.pdf"))))));
        host.setPrintSource(QStringLiteral("document"), check); // File ▸ Print on a selected check
        QCOMPARE(host.printSourceKind(), QStringLiteral("check"));
        host.setPrintSource(QStringLiteral("check"), expense);
        QCOMPARE(host.printSourceKind(), QString());
        // Voiding the selected check takes the command away.
        QVERIFY(ok(books->voidDocument(check, QStringLiteral("spoiled"))));
        QVERIFY(!printCheck->isEnabled());
    }

    // M68 (D-077): Create Bill from Order follows the selection — an order
    // with something left to receive — and asks for that order's bill.
    void createBillFollowsTheSelectedOrder()
    {
        QTemporaryDir dir;
        BooksHost host;
        QVERIFY(ok(withCompany(host, dir)));
        BooksService *books = host.books();
        const qint64 vendor = idOf(books->saveParty({{QStringLiteral("kind"), QStringLiteral("vendor")},
                                                     {QStringLiteral("name"), QStringLiteral("Copperleaf Paper")}}));
        const QVariantMap line{{QStringLiteral("description"), QStringLiteral("Paper")},
                               {QStringLiteral("accountId"), find(books->accounts(), QStringLiteral("number"), QStringLiteral("6400")).value(QStringLiteral("id"))},
                               {QStringLiteral("amount"), QStringLiteral("80.00")}};
        const qint64 po = idOf(books->saveDocument({{QStringLiteral("kind"), QStringLiteral("purchaseOrder")},
                                                    {QStringLiteral("partyId"), vendor},
                                                    {QStringLiteral("lines"), QVariantList{line}}}));
        const qint64 bill = idOf(books->saveDocument({{QStringLiteral("kind"), QStringLiteral("bill")},
                                                      {QStringLiteral("partyId"), vendor},
                                                      {QStringLiteral("lines"), QVariantList{line}}}));
        QVERIFY(po != 0 && bill != 0);
        ActionEntry *create = host.action(QStringLiteral("booksCreateBillFromOrderAction"));
        QVERIFY(!create->isEnabled());
        host.setSelectedDocumentId(bill);
        QVERIFY(!create->isEnabled());
        host.setSelectedDocumentId(po);
        QVERIFY(create->isEnabled());
        QSignalSpy requested(&host, &BooksHost::billFromOrderRequested);
        QVERIFY(host.actions()->trigger(QStringLiteral("booksCreateBillFromOrderAction")));
        QCOMPARE(requested.last().at(0).toLongLong(), po);
        // A purchase order prints as its form through the document source.
        host.setPrintSource(QStringLiteral("document"), po);
        QCOMPARE(host.printSourceKind(), QStringLiteral("document"));
        QVERIFY(host.exportPdf(dir.filePath(QStringLiteral("po.pdf"))));
        // Closed by hand, there is nothing to receive.
        QVERIFY(ok(books->setOrderClosed(po, true)));
        QVERIFY(!create->isEnabled());
        // A "checks" source refuses anything that is not a check.
        host.setPrintSource(QStringLiteral("checks"), QVariantList{po});
        QCOMPARE(host.printSourceKind(), QString());
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
        QCOMPARE(window->width(), 1320); // BooksWindow.qml sets its size once (ADR-0117)
        delete window; // before the host it binds to
    }
};

BOOKS_APP_TEST_MAIN(QindaBooksHostTest)

#include "tst_qindabooks_host.moc"
