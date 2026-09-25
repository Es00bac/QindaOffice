// SPDX-License-Identifier: GPL-3.0-or-later
// BooksHost: the QindaBooks commands (ActionEntries) and menus.
//
// AGENT-CONTRACT (rule 5): every id below is the command's activation
// identity for the panel export, the ribbon and the keyboard — never rename
// or reuse one; add new ids for new commands. An id appears in the menu
// tree exactly once (the desktop validator rejects duplicates).
#include "books_host.h"

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
    {"booksImportStatementAction", QT_TRANSLATE_NOOP("BooksHost", "&Import Statement…"), "", "import"},
    {"booksReconcileAction", QT_TRANSLATE_NOOP("BooksHost", "&Reconcile…"), "", "scale"},
    {"booksBankRulesAction", QT_TRANSLATE_NOOP("BooksHost", "Bank R&ules…"), "", "workflow"},
    {"booksRunReportAction", QT_TRANSLATE_NOOP("BooksHost", "&Run Report…"), "", "chart-column"},
    {"booksExportReportPdfAction", QT_TRANSLATE_NOOP("BooksHost", "Export Report as &PDF…"), "", "file-output"},
    {"booksExportReportCsvAction", QT_TRANSLATE_NOOP("BooksHost", "Export Report as &CSV…"), "", "file-spreadsheet"},
    {"booksExportIifAction", QT_TRANSLATE_NOOP("BooksHost", "Export for QuickBooks (&IIF)…"), "", "file-down"},
    {"booksExportJournalCsvAction", QT_TRANSLATE_NOOP("BooksHost", "Export &Journal (CSV)…"), "", "sheet"},
    {"booksImportListsAction", QT_TRANSLATE_NOOP("BooksHost", "Import &Lists (CSV)…"), "", "file-up"},
    {"booksBackupAction", QT_TRANSLATE_NOOP("BooksHost", "&Backup…"), "", "archive"},
    {"booksCompanySettingsAction", QT_TRANSLATE_NOOP("BooksHost", "Company Se&ttings…"), "", "building"},
    {"booksCloseBooksAction", QT_TRANSLATE_NOOP("BooksHost", "Close the &Books…"), "", "lock"},
    {"booksVoidAction", QT_TRANSLATE_NOOP("BooksHost", "&Void Document…"), "", "circle-x"},
    {"booksEmailDocumentAction", QT_TRANSLATE_NOOP("BooksHost", "E-&mail Document…"), "", "mail"},
    {"booksPrintDocumentAction", QT_TRANSLATE_NOOP("BooksHost", "Print &Document…"), "", "printer"},
};

// The workspace radio group, in Ctrl+1…6 order (BooksHost::viewKeys()).
constexpr Command kViews[] = {
    {"booksViewDashboardAction", QT_TRANSLATE_NOOP("BooksHost", "&Dashboard"), "Ctrl+1", "layout-dashboard"},
    {"booksViewSalesAction", QT_TRANSLATE_NOOP("BooksHost", "&Sales"), "Ctrl+2", "trending-up"},
    {"booksViewExpensesAction", QT_TRANSLATE_NOOP("BooksHost", "&Expenses"), "Ctrl+3", "trending-down"},
    {"booksViewBankingAction", QT_TRANSLATE_NOOP("BooksHost", "&Banking"), "Ctrl+4", "landmark"},
    {"booksViewAccountingAction", QT_TRANSLATE_NOOP("BooksHost", "&Accounting"), "Ctrl+5", "book-text"},
    {"booksViewReportsAction", QT_TRANSLATE_NOOP("BooksHost", "&Reports"), "Ctrl+6", "chart-pie"},
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
    on("booksReceivePaymentAction", [this] { emit paymentDialogRequested(QStringLiteral("receive")); });
    on("booksPayBillsAction", [this] { emit paymentDialogRequested(QStringLiteral("payBills")); });
    on("booksNewCustomerAction", [this] { emit partyDialogRequested(QStringLiteral("customer"), 0); });
    on("booksNewVendorAction", [this] { emit partyDialogRequested(QStringLiteral("vendor"), 0); });
    on("booksNewItemAction", [this] { emit itemDialogRequested(0); });
    on("booksNewAccountAction", [this] { emit accountDialogRequested(0); });
    on("booksJournalEntryAction", [this] { emit journalEntryRequested(); });
    on("booksTransferAction", [this] { emit transferRequested(); });
    on("booksDepositAction", [this] { emit depositRequested(); });
    on("booksImportStatementAction", [this] { emit importStatementRequested(); });
    on("booksReconcileAction", [this] { emit reconcileRequested(); });
    on("booksBankRulesAction", [this] { emit bankRulesRequested(); });
    on("booksRunReportAction", [this] {
        setCurrentView(QStringLiteral("reports"));
        emit reportRequested(QString());
    });
    on("booksExportReportPdfAction", [this] { emit exportRequested(QStringLiteral("reportPdf")); });
    on("booksExportReportCsvAction", [this] { emit exportRequested(QStringLiteral("reportCsv")); });
    on("booksExportIifAction", [this] { emit exportRequested(QStringLiteral("iif")); });
    on("booksExportJournalCsvAction", [this] { emit exportRequested(QStringLiteral("journalCsv")); });
    on("booksBackupAction", [this] { emit exportRequested(QStringLiteral("backup")); });
    on("booksImportListsAction", [this] { emit importListsRequested(); });
    on("booksCompanySettingsAction", [this] { emit companySettingsRequested(); });
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
                                       {a("booksImportListsAction"), MenuNode::separator(), a("booksExportIifAction"),
                                        a("booksExportJournalCsvAction")}));
    tree->insertInto(QStringLiteral("fileMenu"), close, MenuNode::separator());
    tree->insertInto(QStringLiteral("fileMenu"), close, a("booksCompanySettingsAction"));
    tree->insertInto(QStringLiteral("fileMenu"), close, a("booksCloseBooksAction"));
    tree->insertInto(QStringLiteral("fileMenu"), close, MenuNode::separator());

    const QString before = QStringLiteral("aiMenu");
    tree->insertMenuBefore(before, MenuNode::submenu(QStringLiteral("booksSalesMenu"), tr("Sa&les"),
                                                     {a("booksNewInvoiceAction"), a("booksNewEstimateAction"),
                                                      a("booksNewSalesReceiptAction"), a("booksNewCreditMemoAction"),
                                                      MenuNode::separator(), a("booksReceivePaymentAction"),
                                                      MenuNode::separator(), a("booksNewCustomerAction"),
                                                      a("booksNewItemAction"), MenuNode::separator(),
                                                      a("booksEmailDocumentAction"), a("booksPrintDocumentAction")}));
    tree->insertMenuBefore(before, MenuNode::submenu(QStringLiteral("booksExpensesMenu"), tr("E&xpenses"),
                                                     {a("booksNewBillAction"), a("booksPayBillsAction"),
                                                      MenuNode::separator(), a("booksNewExpenseAction"),
                                                      a("booksNewCheckAction"), a("booksNewVendorCreditAction"),
                                                      MenuNode::separator(), a("booksNewVendorAction")}));
    tree->insertMenuBefore(before, MenuNode::submenu(QStringLiteral("booksBankingMenu"), tr("&Banking"),
                                                     {a("booksImportStatementAction"), a("booksBankRulesAction"),
                                                      MenuNode::separator(), a("booksDepositAction"),
                                                      a("booksTransferAction"), MenuNode::separator(),
                                                      a("booksReconcileAction")}));
    tree->insertMenuBefore(before, MenuNode::submenu(QStringLiteral("booksAccountingMenu"), tr("Accou&nting"),
                                                     {a("booksNewAccountAction"), a("booksJournalEntryAction"),
                                                      MenuNode::separator(), a("booksVoidAction")}));
    tree->insertMenuBefore(before, MenuNode::submenu(QStringLiteral("booksReportsMenu"), tr("&Reports"),
                                                     {a("booksRunReportAction"), MenuNode::separator(),
                                                      a("booksExportReportPdfAction"),
                                                      a("booksExportReportCsvAction")}));
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
    const bool reportShown = company && m_printKind == QLatin1String("report");
    actions()->setEnabled({id("booksExportReportPdfAction"), id("booksExportReportCsvAction")}, reportShown);
    // Print / Preview render the current source (see the header).
    const bool printable = company && !m_printKind.isEmpty();
    actions()->setEnabled({id(ActionId::kFilePrint), id(ActionId::kFilePrintPreview)}, printable);
}

} // namespace QindaOffice::Apps::Books
