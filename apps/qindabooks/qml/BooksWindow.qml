// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Dialogs as Dialogs
import QindaTK as Tk
import QindaOffice.Shell
import QindaOffice.Books.App

// The QindaBooks window (D-047): one company file per window. A rail picks
// the workspace — Dashboard, Sales, Expenses, Banking, Accounting, Reports,
// Time (M68) — and the window answers the host's requests with the editors
// and dialogs. A brand-new window with no company opens the setup wizard.
//
// AGENT-NOTE: every amount arrives from BooksService as minor units plus
// formatted text; this QML never does arithmetic on money.
OfficeWindow {
    id: window

    readonly property BooksHost booksHost: host as BooksHost
    readonly property BooksService books: booksHost !== null ? booksHost.books : null
    readonly property string view: booksHost !== null ? booksHost.currentView : "dashboard"

    width: 1320
    height: 840

    function trigger(id) { window.host.actions.trigger(id) }
    function fileLocal(url) { return decodeURIComponent(String(url).replace(/^file:\/\//, "")) }

    readonly property BooksRibbonLayout ribbonLayout: BooksRibbonLayout { }
    ribbonTabs: ribbonLayout.tabs

    // AGENT-GUARD: the window seats exactly one Item; a Flex keeps its own
    // implicit size, so the row fills a plain Item.
    Item {
      objectName: "booksContent"
      Tk.Flex {
        objectName: "booksRoot"
        anchors.fill: parent
        direction: Tk.Flex.Row
        gap: 0
        Tk.NavigationRail {
            objectName: "booksRail"
            Tk.Flex.shrink: 0
            expanded: true
            expandedWidth: Tk.Theme.size.panelMinWidth * 0.85
            currentId: window.view
            tooltip: qsTr("Workspaces")
            groups: [{ id: "books", label: window.books !== null ? window.books.companyName : "", entries: [
                { id: "dashboard", label: qsTr("Dashboard"), destination: "booksViewDashboardAction", iconName: "layout-dashboard" },
                { id: "sales", label: qsTr("Sales"), destination: "booksViewSalesAction", iconName: "receipt" },
                { id: "expenses", label: qsTr("Expenses"), destination: "booksViewExpensesAction", iconName: "wallet" },
                { id: "banking", label: qsTr("Banking"), destination: "booksViewBankingAction", iconName: "landmark" },
                { id: "accounting", label: qsTr("Accounting"), destination: "booksViewAccountingAction", iconName: "book-text" },
                { id: "reports", label: qsTr("Reports"), destination: "booksViewReportsAction", iconName: "chart-line" },
                { id: "time", label: qsTr("Time"), destination: "booksViewTimeAction", iconName: "timer" }] }]
            onDestinationRequested: function(id, destination) { window.trigger(destination) }
        }
        Tk.Divider { vertical: true }
        Item {
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Toast { id: toast; objectName: "booksToast"; z: 10; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: Tk.Theme.space.lg }
            DashboardView { anchors.fill: parent; visible: window.view === "dashboard"; books: window.books; host: window.booksHost }
            SalesView { anchors.fill: parent; visible: window.view === "sales"; books: window.books; host: window.booksHost }
            ExpensesView { anchors.fill: parent; visible: window.view === "expenses"; books: window.books; host: window.booksHost }
            BankingView { id: banking; anchors.fill: parent; visible: window.view === "banking"; books: window.books; host: window.booksHost }
            AccountsView { anchors.fill: parent; visible: window.view === "accounting"; books: window.books; host: window.booksHost }
            ReportsView { id: reports; anchors.fill: parent; visible: window.view === "reports"; books: window.books; host: window.booksHost }
            TimeView { anchors.fill: parent; visible: window.view === "time"; books: window.books; host: window.booksHost }
            Tk.EmptyState {
                anchors.centerIn: parent
                visible: window.books === null || !window.books.hasCompany
                iconName: "building"
                title: qsTr("No company open")
                text: qsTr("Create a company file for your business, or open an existing one.")
                Tk.Button { variant: "accent"; text: qsTr("Set up a company…"); onClicked: setup.open() }
                Tk.Button { variant: "outline"; text: qsTr("Open…"); onClicked: window.trigger("fileOpenAction") }
            }
        }
      }
    }

    // ---- editors and dialogs ----
    CompanySetupDialog { id: setup; books: window.books }
    DocumentEditor { id: editor; books: window.books; host: window.booksHost }
    PaymentDialog { id: payment; books: window.books }
    PartyDialog { id: partyDialog; books: window.books }
    ItemDialog { id: itemDialog; books: window.books }
    AccountDialog { id: accountDialog; books: window.books }
    JournalEntryDialog { id: journalDialog; books: window.books }
    ConnectionsDialog { id: connectionsDialog; books: window.books }
    Tk.PromptDialog {
        id: closeBooks
        objectName: "closeBooksDialog"
        title: qsTr("Close the books")
        label: qsTr("Lock every transaction on or before this date (yyyy-mm-dd). Changing a locked period needs an explicit reason.")
        primaryText: qsTr("Lock")
        onAccepted: window.books.setLockDate(text, qsTr("Period closed"))
    }
    Dialogs.FileDialog {
        id: exportDialog
        property string what: ""
        title: qsTr("Export")
        fileMode: Dialogs.FileDialog.SaveFile
        nameFilters: what === "reportPdf" ? [qsTr("PDF (*.pdf)")] : what === "reportXlsx" ? [qsTr("Excel workbook (*.xlsx)")]
                     : what === "iif" ? [qsTr("QuickBooks IIF (*.iif)")] : [qsTr("CSV (*.csv)")]
        onAccepted: {
            if (what === "reportPdf" || what === "reportCsv" || what === "reportXlsx")
                reports.exportTo(selectedFile, what === "reportPdf" ? "pdf" : what === "reportXlsx" ? "xlsx" : "csv")
            else if (what === "iif") window.books.exportIif(selectedFile)
            else if (what === "journalCsv") window.books.exportJournalCsv(selectedFile)
        }
    }
    ListsDialog { id: listsDialog; books: window.books }
    // M63 (D-071): the engine's statements, schedules, budgets, sales-tax
    // payments and logo, reachable at last.
    LogoDialog { id: logoDialog; books: window.books }
    StatementDialog { id: statementDialog; books: window.books; host: window.booksHost }
    RecurringDialog { id: recurringDialog; books: window.books }
    BudgetDialog { id: budgetDialog; books: window.books; host: window.booksHost }
    SalesTaxPaymentDialog { id: salesTaxDialog; books: window.books }
    IifImportDialog { id: iifDialog; books: window.books }
    // M68 (D-077): purchase orders, the check queue and alignment, time.
    PurchaseOrderEditor { id: orderEditor; books: window.books; host: window.booksHost; onBillRequested: function(id) { window.billFromOrder(id) } }
    PrintChecksDialog { id: printChecks; books: window.books; host: window.booksHost }
    CheckAlignmentDialog { id: alignment; books: window.books; host: window.booksHost }
    TimeActivityDialog { id: timeActivity; books: window.books }
    TimesheetDialog { id: timesheet; books: window.books }
    EmployeeDialog { id: employeeDialog; books: window.books }
    function openDocument(kind, id) {
        const isOrder = kind === "purchaseOrder" || (Number(id) > 0 && String(window.books.document(id).kind) === "purchaseOrder")
        if (isOrder) orderEditor.openFor(id)
        else editor.openFor(kind, id)
    }
    function billFromOrder(id) {
        const r = window.books.billFromOrder(id)
        if (r.ok) { editor.openDraft(r.draft); return }
        toast.title = qsTr("Create bill"); toast.message = String(r.error || ""); toast.variant = "warning"; toast.show()
    }

    Connections {
        target: window.booksHost
        function onCompanySetupRequested() { setup.open() }
        function onCompanySettingsRequested() { window.trigger("settingsPreferencesAction") }
        function onDocumentEditorRequested(kind, id) { window.openDocument(kind, id) }
        function onPaymentDialogRequested(kind) { payment.openFor(kind) }
        function onPartyDialogRequested(kind, id) { partyDialog.openFor(kind, id) }
        function onItemDialogRequested(id) { itemDialog.openFor(id) }
        function onAccountDialogRequested(id) { accountDialog.openFor(id) }
        function onJournalEntryRequested() { journalDialog.openFor("journal") }
        function onTransferRequested() { journalDialog.openFor("transfer") }
        function onDepositRequested() { journalDialog.openFor("deposit") }
        function onImportStatementRequested() { window.trigger("booksViewBankingAction"); banking.importStatement() }
        function onReconcileRequested() { window.trigger("booksViewBankingAction"); banking.startReconcile() }
        function onCloseBooksRequested() { closeBooks.text = window.books.lockDate; closeBooks.open() }
        function onReportRequested(reportId) { window.trigger("booksViewReportsAction"); if (reportId.length > 0) reports.reportId = reportId }
        function onBankRulesRequested() { window.trigger("booksViewBankingAction"); banking.tab = 2 }
        function onVoidRequested(id) { window.openDocument("", id) }
        function onBillFromOrderRequested(id) { window.billFromOrder(id) }
        function onPrintChecksRequested() { printChecks.openDialog() }
        function onCheckAlignmentRequested() { alignment.openDialog() }
        function onTimeActivityRequested(id) { timeActivity.openFor(id) }
        function onTimesheetRequested() { timesheet.openDialog("", window.books.today) }
        function onEmployeeDialogRequested(id) { employeeDialog.openFor(id) }
        function onExportRequested(what) {
            if (what === "backup") { const r = window.books.backupNow(); return }
            exportDialog.what = what
            exportDialog.open()
        }
        function onImportListsRequested() { listsDialog.openFor("import") }
        function onExportListsRequested() { listsDialog.openFor("export") }
        function onImportIifRequested() { iifDialog.openDialog() }
        function onCompanyLogoRequested() { logoDialog.openDialog() }
        function onStatementsRequested() { statementDialog.openFor(0) }
        function onRecurringRequested() { recurringDialog.openDialog() }
        function onBudgetsRequested() { budgetDialog.openFor(window.books.currentFiscalYear()) }
        function onSalesTaxPaymentRequested() { salesTaxDialog.openDialog() }
        function onConnectionsRequested() { connectionsDialog.openDialog() }
    }
    Connections {
        target: window.books
        function onNotify(title, text, variant) { toast.title = title; toast.message = text; toast.variant = variant; toast.show() }
        function onConnectionSyncFinished(requestId, result) {
            toast.title = result.ok ? qsTr("Synced") : qsTr("Sync finished with problems")
            toast.message = result.ok ? qsTr("%1 new lines, %2 invoices paid, %3 payouts matched").arg(result.imported || 0).arg(result.applied || 0).arg(result.matched || 0)
                                      : (result.errors || []).join(" · ")
            toast.variant = result.ok ? "success" : "warning"
            toast.show()
        }
    }
    Component.onCompleted: {
        window.booksHost.preferencesPageUrl = Qt.resolvedUrl("BooksPreferencesPage.qml")
        // (BooksHost.companySetupRequested opens the wizard for a new window.)
    }
}
