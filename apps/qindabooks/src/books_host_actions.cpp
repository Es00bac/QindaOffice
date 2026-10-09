// SPDX-License-Identifier: GPL-3.0-or-later
// BooksHost: the QindaBooks commands (ActionEntries) and menus.
//
// AGENT-CONTRACT (rule 5): every id below is the command's activation
// identity for the panel export, the ribbon and the keyboard — never rename
// or reuse one; add new ids for new commands. An id appears in the menu
// tree exactly once (the desktop validator rejects duplicates).
#include "books_host.h"

#include "check_print.h"
#include "standard_actions.h"

#include <QKeySequence>

#include <iterator>

namespace QindaOffice::Apps::Books {

using Quick::ActionEntry;
using Quick::MenuNode;
namespace ActionId = Quick::ActionId;

namespace {

struct Command {
    const char *id;
    const char *text;
    const char *shortcut;
    const char *icon;
};

// Commands that open an editor/dialog; the handlers are wired below.
constexpr Command kCommands[] = {
    {"booksNewInvoiceAction", QT_TRANSLATE_NOOP("BooksHost", "&Invoice…"), "Ctrl+I", "receipt-text"},
    {"booksNewEstimateAction", QT_TRANSLATE_NOOP("BooksHost", "&Estimate…"), "", "file-text"},
    {"booksNewSalesReceiptAction", QT_TRANSLATE_NOOP("BooksHost", "Sales &Receipt…"), "", "receipt"},
    {"booksReceivePaymentAction", QT_TRANSLATE_NOOP("BooksHost", "Receive &Payment…"), "", "hand-coins"},
    {"booksNewCreditMemoAction", QT_TRANSLATE_NOOP("BooksHost", "&Credit Memo…"), "", "file-minus"},
    {"booksNewBillAction", QT_TRANSLATE_NOOP("BooksHost", "&Bill…"), "", "file-input"},
    {"booksPayBillsAction", QT_TRANSLATE_NOOP("BooksHost", "&Pay Bills…"), "", "banknote"},
    {"booksNewExpenseAction", QT_TRANSLATE_NOOP("BooksHost", "E&xpense…"), "", "wallet"},
    {"booksNewCheckAction", QT_TRANSLATE_NOOP("BooksHost", "C&heck…"), "", "square-pen"},
    {"booksNewVendorCreditAction", QT_TRANSLATE_NOOP("BooksHost", "Vendor &Credit…"), "", "circle-dollar-sign"},
    {"booksNewCustomerAction", QT_TRANSLATE_NOOP("BooksHost", "New C&ustomer…"), "", "user-plus"},
    {"booksNewVendorAction", QT_TRANSLATE_NOOP("BooksHost", "New &Vendor…"), "", "store"},
    {"booksNewItemAction", QT_TRANSLATE_NOOP("BooksHost", "New Product or &Service…"), "", "package"},
    {"booksNewAccountAction", QT_TRANSLATE_NOOP("BooksHost", "New &Account…"), "", "list-plus"},
    {"booksJournalEntryAction", QT_TRANSLATE_NOOP("BooksHost", "&Journal Entry…"), "", "book-open"},
    {"booksTransferAction", QT_TRANSLATE_NOOP("BooksHost", "&Transfer…"), "", "arrow-left-right"},
    {"booksDepositAction", QT_TRANSLATE_NOOP("BooksHost", "&Deposit…"), "", "piggy-bank"},
    // M48 (D-048): Mercury and Stripe connections.
    {"booksSyncBankAction", QT_TRANSLATE_NOOP("BooksHost", "S&ync Mercury && Stripe"), "F5", "refresh-cw"},
    {"booksConnectionsAction", QT_TRANSLATE_NOOP("BooksHost", "Co&nnections…"), "", "link"},
    {"booksImportStatementAction", QT_TRANSLATE_NOOP("BooksHost", "&Import Statement…"), "", "import"},
    {"booksReconcileAction", QT_TRANSLATE_NOOP("BooksHost", "&Reconcile…"), "", "scale"},
    {"booksBankRulesAction", QT_TRANSLATE_NOOP("BooksHost", "Bank R&ules…"), "", "workflow"},
    {"booksRunReportAction", QT_TRANSLATE_NOOP("BooksHost", "&Run Report…"), "", "chart-column"},
    {"booksExportReportPdfAction", QT_TRANSLATE_NOOP("BooksHost", "Export Report as &PDF…"), "", "file-output"},
    {"booksExportReportCsvAction", QT_TRANSLATE_NOOP("BooksHost", "Export Report as &CSV…"), "", "file-spreadsheet"},
    // M63 (D-071): every report to Excel with numeric, currency-formatted cells.
    {"booksExportReportXlsxAction", QT_TRANSLATE_NOOP("BooksHost", "Export Report as E&xcel…"), "", "sheet"},
    {"booksExportIifAction", QT_TRANSLATE_NOOP("BooksHost", "Export for QuickBooks (&IIF)…"), "", "file-down"},
    {"booksExportJournalCsvAction", QT_TRANSLATE_NOOP("BooksHost", "Export &Journal (CSV)…"), "", "sheet"},
    {"booksImportListsAction", QT_TRANSLATE_NOOP("BooksHost", "Import &Lists (CSV)…"), "", "file-up"},
    // M63 (D-071): the other half of the list round trip.
    {"booksExportListsAction", QT_TRANSLATE_NOOP("BooksHost", "Export Li&sts (CSV)…"), "", "file-down"},
    {"booksImportIifAction", QT_TRANSLATE_NOOP("BooksHost", "Import from &QuickBooks (IIF)…"), "", "import"},
    {"booksBackupAction", QT_TRANSLATE_NOOP("BooksHost", "&Backup…"), "", "archive"},
    {"booksCompanySettingsAction", QT_TRANSLATE_NOOP("BooksHost", "Company Se&ttings…"), "", "building"},
    {"booksCloseBooksAction", QT_TRANSLATE_NOOP("BooksHost", "Close the &Books…"), "", "lock"},
    {"booksVoidAction", QT_TRANSLATE_NOOP("BooksHost", "&Void Document…"), "", "circle-x"},
    {"booksEmailDocumentAction", QT_TRANSLATE_NOOP("BooksHost", "E-&mail Document…"), "", "mail"},
    {"booksPrintDocumentAction", QT_TRANSLATE_NOOP("BooksHost", "Print &Document…"), "", "printer"},
    // M63 (D-071): what the engine always had, now reachable.
    {"booksCompanyLogoAction", QT_TRANSLATE_NOOP("BooksHost", "Company &Logo…"), "", "image"},
    {"booksStatementsAction", QT_TRANSLATE_NOOP("BooksHost", "Customer S&tatements…"), "", "scroll-text"},
    {"booksRecurringAction", QT_TRANSLATE_NOOP("BooksHost", "Recurring In&voices…"), "", "repeat"},
    {"booksBudgetsAction", QT_TRANSLATE_NOOP("BooksHost", "&Budgets…"), "", "target"},
    {"booksSalesTaxPaymentAction", QT_TRANSLATE_NOOP("BooksHost", "Pay Sales &Tax…"), "", "percent"},
    // M63 (D-071 item 7): the selected check or bill payment onto voucher stock.
    {"booksPrintCheckAction", QT_TRANSLATE_NOOP("BooksHost", "Print Chec&k…"), "", "printer"},
    // M68 (D-077): purchase orders, the check print queue, time.
    {"booksNewPurchaseOrderAction", QT_TRANSLATE_NOOP("BooksHost", "Purchase &Order…"), "", "clipboard-list"},
    {"booksCreateBillFromOrderAction", QT_TRANSLATE_NOOP("BooksHost", "Create Bill from Or&der"), "", "clipboard-check"},
    {"booksPrintChecksAction", QT_TRANSLATE_NOOP("BooksHost", "Checks to Prin&t…"), "", "list-checks"},
    {"booksCheckAlignmentAction", QT_TRANSLATE_NOOP("BooksHost", "Check Printer &Alignment…"), "", "crosshair"},
    {"booksTimeActivityAction", QT_TRANSLATE_NOOP("BooksHost", "&Time Activity…"), "Ctrl+Shift+T", "clock"},
    {"booksWeeklyTimesheetAction", QT_TRANSLATE_NOOP("BooksHost", "&Weekly Timesheet…"), "", "calendar-days"},
    {"booksNewEmployeeAction", QT_TRANSLATE_NOOP("BooksHost", "New &Employee…"), "", "user-round"},
    // M71 (D-081): owner pay, estimated taxes and payroll.
    {"booksPayrollSetupAction", QT_TRANSLATE_NOOP("BooksHost", "Set Up Owner Pay && &Payroll…"), "", "settings-2"},
    {"booksPayOwnerAction", QT_TRANSLATE_NOOP("BooksHost", "Pay &Owner…"), "", "hand-coins"},
    {"booksEstimatedTaxesAction", QT_TRANSLATE_NOOP("BooksHost", "Estimated &Taxes"), "", "calculator"},
    {"booksDistributionAction", QT_TRANSLATE_NOOP("BooksHost", "Shareholder &Distribution…"), "", "banknote"},
    // D-081 amendment: a C corporation's dividends, and the company's own
    // payroll tax items for any state or locality without built-in tables.
    {"booksDividendAction", QT_TRANSLATE_NOOP("BooksHost", "Pay Di&vidend…"), "", "coins"},
    {"booksPayrollItemsAction", QT_TRANSLATE_NOOP("BooksHost", "Payroll Tax &Items…"), "", "list-plus"},
    {"booksPayrollEmployeesAction", QT_TRANSLATE_NOOP("BooksHost", "&Employee Payroll…"), "", "users"},
    {"booksRunPayrollAction", QT_TRANSLATE_NOOP("BooksHost", "&Run Payroll…"), "", "receipt"},
    {"booksPayLiabilitiesAction", QT_TRANSLATE_NOOP("BooksHost", "Pay Payroll &Liabilities…"), "", "landmark"},
    {"booksPayrollFormsAction", QT_TRANSLATE_NOOP("BooksHost", "Payroll Tax &Forms…"), "", "file-check"},
    {"booksExportEfw2Action", QT_TRANSLATE_NOOP("BooksHost", "Export &W-2 File (EFW2)…"), "", "file-down"},
};

// The workspace radio group, in Ctrl+1…7 order (BooksHost::viewKeys()).
constexpr Command kViews[] = {
    {"booksViewDashboardAction", QT_TRANSLATE_NOOP("BooksHost", "&Dashboard"), "Ctrl+1", "layout-dashboard"},
    {"booksViewSalesAction", QT_TRANSLATE_NOOP("BooksHost", "&Sales"), "Ctrl+2", "trending-up"},
    {"booksViewExpensesAction", QT_TRANSLATE_NOOP("BooksHost", "&Expenses"), "Ctrl+3", "trending-down"},
    {"booksViewBankingAction", QT_TRANSLATE_NOOP("BooksHost", "&Banking"), "Ctrl+4", "landmark"},
    {"booksViewAccountingAction", QT_TRANSLATE_NOOP("BooksHost", "&Accounting"), "Ctrl+5", "book-text"},
    {"booksViewReportsAction", QT_TRANSLATE_NOOP("BooksHost", "&Reports"), "Ctrl+6", "chart-pie"},
    {"booksViewTimeAction", QT_TRANSLATE_NOOP("BooksHost", "&Time"), "Ctrl+7", "timer"}, // M68
    {"booksViewPayrollAction", QT_TRANSLATE_NOOP("BooksHost", "&Payroll"), "Ctrl+8", "wallet-cards"}, // M71
};

QString id(const char *raw)
{
    return QString::fromLatin1(raw);
}

MenuNode a(const char *raw)
{
    return MenuNode::action(QString::fromLatin1(raw));
}

} // namespace

void BooksHost::createActions()
{
    for (const Command &c : kCommands) {
        actions()->add(id(c.id), tr(c.text), QKeySequence(QString::fromLatin1(c.shortcut)), id(c.icon));
    }
    const QStringList views = viewKeys();
    for (int i = 0; i < int(std::size(kViews)); ++i) {
        const Command &c = kViews[i];
        ActionEntry *entry = actions()->add(id(c.id), tr(c.text), QKeySequence(QString::fromLatin1(c.shortcut)),
                                            id(c.icon));
        entry->setCheckable(true);
        entry->setRadioGroup(QStringLiteral("booksView"));
        entry->setChecked(views.at(i) == m_view);
        const QString key = views.at(i);
        connect(entry, &ActionEntry::toggled, this, [this, key](bool checked) {
            if (checked) {
                setCurrentView(key);
            }
        });
    }
    // New = a new window for a new company (it shows the setup wizard).
    action(id(ActionId::kFileNew))->setText(tr("&New Company…"));

    const auto on = [this](const char *raw, auto &&slot) {
        connect(action(id(raw)), &ActionEntry::triggered, this, std::forward<decltype(slot)>(slot));
    };
    const auto editor = [this, on](const char *raw, const char *kind) {
        on(raw, [this, kind] { emit documentEditorRequested(QString::fromLatin1(kind), 0); });
    };
    editor("booksNewInvoiceAction", "invoice");
    editor("booksNewEstimateAction", "estimate");
    editor("booksNewSalesReceiptAction", "salesReceipt");
    editor("booksNewCreditMemoAction", "creditMemo");
    editor("booksNewBillAction", "bill");
    editor("booksNewExpenseAction", "expense");
    editor("booksNewCheckAction", "check");
    editor("booksNewVendorCreditAction", "vendorCredit");
    editor("booksNewPurchaseOrderAction", "purchaseOrder");
    on("booksCreateBillFromOrderAction", [this] { emit billFromOrderRequested(m_selectedDocumentId); });
    on("booksPrintChecksAction", [this] { emit printChecksRequested(); });
    on("booksCheckAlignmentAction", [this] { emit checkAlignmentRequested(); });
    on("booksTimeActivityAction", [this] { emit timeActivityRequested(0); });
    on("booksWeeklyTimesheetAction", [this] { emit timesheetRequested(); });
    on("booksNewEmployeeAction", [this] { emit employeeDialogRequested(0); });
    // M71 (D-081): every payroll command only requests; the QML answers.
    on("booksPayrollSetupAction", [this] { emit payrollSetupRequested(); });
    on("booksPayOwnerAction", [this] { emit payOwnerRequested(QStringLiteral("draw")); });
    on("booksDistributionAction", [this] { emit payOwnerRequested(QStringLiteral("distribution")); });
    on("booksDividendAction", [this] { emit payOwnerRequested(QStringLiteral("dividend")); });
    on("booksPayrollItemsAction", [this] { emit payrollItemsRequested(); });
    on("booksEstimatedTaxesAction", [this] {
        setCurrentView(QStringLiteral("payroll"));
        emit estimatedTaxesRequested();
    });
    on("booksPayrollEmployeesAction", [this] {
        setCurrentView(QStringLiteral("payroll"));
        emit payrollEmployeesRequested();
    });
    on("booksRunPayrollAction", [this] { emit runPayrollRequested(); });
    on("booksPayLiabilitiesAction", [this] { emit payLiabilitiesRequested(); });
    on("booksPayrollFormsAction", [this] {
        setCurrentView(QStringLiteral("reports"));
        emit payrollFormsRequested();
    });
    on("booksExportEfw2Action", [this] { emit efw2ExportRequested(); });
    on("booksReceivePaymentAction", [this] { emit paymentDialogRequested(QStringLiteral("receive")); });
    on("booksPayBillsAction", [this] { emit paymentDialogRequested(QStringLiteral("payBills")); });
    on("booksNewCustomerAction", [this] { emit partyDialogRequested(QStringLiteral("customer"), 0); });
    on("booksNewVendorAction", [this] { emit partyDialogRequested(QStringLiteral("vendor"), 0); });
    on("booksNewItemAction", [this] { emit itemDialogRequested(0); });
    on("booksNewAccountAction", [this] { emit accountDialogRequested(0); });
    on("booksJournalEntryAction", [this] { emit journalEntryRequested(); });
    on("booksTransferAction", [this] { emit transferRequested(); });
    on("booksDepositAction", [this] { emit depositRequested(); });
    on("booksSyncBankAction", [this] { static_cast<void>(m_service->syncConnections()); });
    on("booksConnectionsAction", [this] { emit connectionsRequested(); });
    on("booksImportStatementAction", [this] { emit importStatementRequested(); });
    on("booksReconcileAction", [this] { emit reconcileRequested(); });
    on("booksBankRulesAction", [this] { emit bankRulesRequested(); });
    on("booksRunReportAction", [this] {
        setCurrentView(QStringLiteral("reports"));
        emit reportRequested(QString());
    });
    on("booksExportReportPdfAction", [this] { emit exportRequested(QStringLiteral("reportPdf")); });
    on("booksExportReportCsvAction", [this] { emit exportRequested(QStringLiteral("reportCsv")); });
    on("booksExportReportXlsxAction", [this] { emit exportRequested(QStringLiteral("reportXlsx")); });
    on("booksExportIifAction", [this] { emit exportRequested(QStringLiteral("iif")); });
    on("booksExportJournalCsvAction", [this] { emit exportRequested(QStringLiteral("journalCsv")); });
    on("booksBackupAction", [this] { emit exportRequested(QStringLiteral("backup")); });
    on("booksImportListsAction", [this] { emit importListsRequested(); });
    on("booksExportListsAction", [this] { emit exportListsRequested(); });
    on("booksImportIifAction", [this] { emit importIifRequested(); });
    on("booksCompanySettingsAction", [this] { emit companySettingsRequested(); });
    on("booksCompanyLogoAction", [this] { emit companyLogoRequested(); });
    on("booksStatementsAction", [this] { emit statementsRequested(); });
    on("booksRecurringAction", [this] { emit recurringRequested(); });
    on("booksBudgetsAction", [this] { emit budgetsRequested(); });
    on("booksSalesTaxPaymentAction", [this] { emit salesTaxPaymentRequested(); });
    on("booksCloseBooksAction", [this] { emit closeBooksRequested(); });
    on("booksVoidAction", [this] { emit voidRequested(m_selectedDocumentId); });
    on("booksEmailDocumentAction", [this] {
        const QVariantMap result = m_service->emailDocument(m_selectedDocumentId);
        if (!result.value(QStringLiteral("ok")).toBool()) {
            emit messageRequested(tr("E-mail"), result.value(QStringLiteral("error")).toString(),
                                  QStringLiteral("warning"));
        }
    });
    on("booksPrintDocumentAction", [this] {
        setPrintSource(QStringLiteral("document"), m_selectedDocumentId);
        emit printRequested();
    });
    on("booksPrintCheckAction", [this] {
        QString why;
        if (!m_service->hasCompany()
            || !::QindaOffice::Books::isPrintableCheck(*m_service->file(), m_selectedDocumentId, &why)) {
            emit messageRequested(tr("Print Check"), why, QStringLiteral("warning"));
            return;
        }
        setPrintSource(QStringLiteral("check"), m_selectedDocumentId);
        emit printRequested();
    });
}

void BooksHost::insertMenus()
{
    Quick::MenuTree *tree = menuTree();
    const QString close = id(ActionId::kFileCloseWindow);
    // File: the company-level entries, before Close Window (the anchor
    // standard_actions.h names; appending would land them under Quit).
    tree->insertInto(QStringLiteral("fileMenu"), close, a("booksBackupAction"));
    tree->insertInto(QStringLiteral("fileMenu"), close,
                     MenuNode::submenu(QStringLiteral("booksImportExportMenu"), tr("&Import and Export"),
                                       {a("booksImportListsAction"), a("booksExportListsAction"), MenuNode::separator(),
                                        a("booksImportIifAction"), a("booksExportIifAction"),
                                        a("booksExportJournalCsvAction")}));
    tree->insertInto(QStringLiteral("fileMenu"), close, MenuNode::separator());
    tree->insertInto(QStringLiteral("fileMenu"), close, a("booksCompanySettingsAction"));
    tree->insertInto(QStringLiteral("fileMenu"), close, a("booksCompanyLogoAction"));
    tree->insertInto(QStringLiteral("fileMenu"), close, a("booksCloseBooksAction"));
    tree->insertInto(QStringLiteral("fileMenu"), close, MenuNode::separator());

    const QString before = QStringLiteral("aiMenu");
    tree->insertMenuBefore(before, MenuNode::submenu(QStringLiteral("booksSalesMenu"), tr("Sa&les"),
                                                     {a("booksNewInvoiceAction"), a("booksNewEstimateAction"),
                                                      a("booksNewSalesReceiptAction"), a("booksNewCreditMemoAction"),
                                                      MenuNode::separator(), a("booksReceivePaymentAction"),
                                                      a("booksStatementsAction"), a("booksRecurringAction"),
                                                      MenuNode::separator(), a("booksNewCustomerAction"),
                                                      a("booksNewItemAction"), MenuNode::separator(),
                                                      a("booksEmailDocumentAction"), a("booksPrintDocumentAction")}));
    tree->insertMenuBefore(before, MenuNode::submenu(QStringLiteral("booksExpensesMenu"), tr("E&xpenses"),
                                                     {a("booksNewBillAction"), a("booksPayBillsAction"),
                                                      MenuNode::separator(), a("booksNewPurchaseOrderAction"),
                                                      a("booksCreateBillFromOrderAction"), MenuNode::separator(),
                                                      a("booksNewExpenseAction"), a("booksNewCheckAction"),
                                                      a("booksNewVendorCreditAction"), MenuNode::separator(),
                                                      a("booksNewVendorAction"), MenuNode::separator(),
                                                      a("booksPrintCheckAction"), a("booksPrintChecksAction"),
                                                      a("booksCheckAlignmentAction")}));
    // M68: time — QuickBooks keeps it under Employees.
    tree->insertMenuBefore(before, MenuNode::submenu(QStringLiteral("booksTimeMenu"), tr("&Time"),
                                                     {a("booksTimeActivityAction"), a("booksWeeklyTimesheetAction"),
                                                      MenuNode::separator(), a("booksNewEmployeeAction")}));
    // M71 (D-081): owner pay and payroll — QuickBooks' Employees ▸ Payroll.
    tree->insertMenuBefore(before, MenuNode::submenu(QStringLiteral("booksPayrollMenu"), tr("Pa&yroll"),
                                                     {a("booksPayrollSetupAction"), a("booksPayrollItemsAction"),
                                                      MenuNode::separator(), a("booksPayOwnerAction"),
                                                      a("booksEstimatedTaxesAction"), a("booksDistributionAction"),
                                                      a("booksDividendAction"), MenuNode::separator(),
                                                      a("booksPayrollEmployeesAction"), a("booksRunPayrollAction"),
                                                      a("booksPayLiabilitiesAction"), MenuNode::separator(),
                                                      a("booksPayrollFormsAction"), a("booksExportEfw2Action")}));
    tree->insertMenuBefore(before, MenuNode::submenu(QStringLiteral("booksBankingMenu"), tr("&Banking"),
                                                     {a("booksSyncBankAction"), a("booksConnectionsAction"),
                                                      MenuNode::separator(), a("booksImportStatementAction"),
                                                      a("booksBankRulesAction"),
                                                      MenuNode::separator(), a("booksDepositAction"),
                                                      a("booksTransferAction"), MenuNode::separator(),
                                                      a("booksReconcileAction")}));
    tree->insertMenuBefore(before, MenuNode::submenu(QStringLiteral("booksAccountingMenu"), tr("Accou&nting"),
                                                     {a("booksNewAccountAction"), a("booksJournalEntryAction"),
                                                      MenuNode::separator(), a("booksBudgetsAction"),
                                                      a("booksSalesTaxPaymentAction"), MenuNode::separator(),
                                                      a("booksVoidAction")}));
    tree->insertMenuBefore(before, MenuNode::submenu(QStringLiteral("booksReportsMenu"), tr("&Reports"),
                                                     {a("booksRunReportAction"), MenuNode::separator(),
                                                      a("booksExportReportPdfAction"),
                                                      a("booksExportReportCsvAction"),
                                                      a("booksExportReportXlsxAction")}));
    // View: the workspaces first, then the standard chrome toggles.
    const QString firstView = id(ActionId::kViewZoomIn);
    for (const Command &c : kViews) {
        tree->insertInto(QStringLiteral("viewMenu"), firstView, a(c.id));
    }
    tree->insertInto(QStringLiteral("viewMenu"), firstView, MenuNode::separator());
}

void BooksHost::syncEnablement()
{
    const bool company = m_service != nullptr && m_service->hasCompany();
    for (const Command &c : kCommands) {
        actions()->setEnabled(id(c.id), company);
    }
    for (const Command &c : kViews) {
        actions()->setEnabled(id(c.id), company);
    }
    const bool selected = company && m_selectedDocumentId != 0;
    actions()->setEnabled({id("booksVoidAction"), id("booksEmailDocumentAction"), id("booksPrintDocumentAction")},
                          selected);
    actions()->setEnabled(id("booksPrintCheckAction"),
                          selected && ::QindaOffice::Books::isPrintableCheck(*m_service->file(), m_selectedDocumentId));
    // M68: Create Bill from Order needs a selected order with something to receive.
    bool receivable = false;
    if (selected) {
        const QVariantMap doc = m_service->document(m_selectedDocumentId);
        const QString status = doc.value(QStringLiteral("status")).toString();
        receivable = doc.value(QStringLiteral("kind")).toString() == QLatin1String("purchaseOrder")
            && (status == QLatin1String("open") || status == QLatin1String("partiallyReceived"));
    }
    actions()->setEnabled(id("booksCreateBillFromOrderAction"), receivable);
    const bool reportShown = company && m_printKind == QLatin1String("report");
    actions()->setEnabled({id("booksExportReportPdfAction"), id("booksExportReportCsvAction"),
                           id("booksExportReportXlsxAction")},
                          reportShown);
    // Print / Preview render the current source (see the header).
    const bool printable = company && !m_printKind.isEmpty();
    actions()->setEnabled({id(ActionId::kFilePrint), id(ActionId::kFilePrintPreview)}, printable);
}

} // namespace QindaOffice::Apps::Books
