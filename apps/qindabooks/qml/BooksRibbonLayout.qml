// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaOffice.Shell

// QindaBooks' ribbon (D-041, D-047): File (company, backups, import and
// export) · Home (the everyday transactions, statements and recurring
// invoices) · Purchases (orders, the check queue and alignment; M68) · Time
// (activities, the weekly timesheet, employees; M68) · Payroll (setup,
// owner pay and estimated taxes, payroll, tax forms; M71) · Banking ·
// Accounting (journal, budgets and sales tax, the period) · Reports · View.
// Every string is an ActionEntry id BooksHost registers.
QtObject {
    readonly property var tabs: [
        (function() {
            const tab = StandardRibbon.fileTab([
                { id: "company", label: qsTr("Company"), items: [
                    { action: "booksCompanySettingsAction", size: "large", iconName: "building" },
                    { action: "booksCompanyLogoAction", iconName: "image" },
                    { action: "booksCloseBooksAction", iconName: "lock" },
                    { action: "booksBackupAction", iconName: "archive" }] },
                { id: "transfer", label: qsTr("Import & export"), items: [
                    { action: "booksImportListsAction", iconName: "file-input" },
                    { action: "booksExportListsAction", iconName: "file-down" },
                    { action: "booksImportIifAction", iconName: "import" },
                    { action: "booksExportIifAction", iconName: "file-output" },
                    { action: "booksExportJournalCsvAction", iconName: "file-spreadsheet" }] }], [], [])
            tab.groups[0].items[2] = { action: "fileSaveAction", iconName: "save" }
            return tab
        })(),
        { id: "home", label: qsTr("Home"), keyTip: "H", groups: [
            StandardRibbon.historyGroup(),
            { id: "customers", label: qsTr("Customers"), items: [
                { action: "booksNewInvoiceAction", size: "large", split: true,
                  menu: ["booksNewInvoiceAction", "booksNewEstimateAction", "booksNewSalesReceiptAction", "booksNewCreditMemoAction"] },
                { action: "booksReceivePaymentAction", size: "large" },
                "booksNewEstimateAction", "booksNewSalesReceiptAction", "booksNewCreditMemoAction", "booksNewCustomerAction",
                "booksStatementsAction", "booksRecurringAction"] },
            { id: "vendors", label: qsTr("Vendors"), items: [
                { action: "booksNewExpenseAction", size: "large", split: true,
                  menu: ["booksNewExpenseAction", "booksNewCheckAction", "booksNewBillAction", "booksNewVendorCreditAction",
                         "booksNewPurchaseOrderAction"] },
                { action: "booksPayBillsAction", size: "large" },
                "booksNewBillAction", "booksNewCheckAction", "booksNewVendorCreditAction", "booksNewVendorAction"] },
            { id: "lists", label: qsTr("Lists"), items: ["booksNewItemAction", "booksNewAccountAction"] },
            { id: "document", label: qsTr("Document"), items: [
                "booksPrintDocumentAction", "booksPrintCheckAction", "booksEmailDocumentAction", "booksVoidAction"] }
        ] },
        { id: "purchases", label: qsTr("Purchases"), keyTip: "P", groups: [
            { id: "orders", label: qsTr("Purchase orders"), items: [
                { action: "booksNewPurchaseOrderAction", size: "large" }, "booksCreateBillFromOrderAction"] },
            { id: "checks", label: qsTr("Checks"), items: [
                { action: "booksPrintChecksAction", size: "large" }, "booksCheckAlignmentAction"] }
        ] },
        { id: "time", label: qsTr("Time"), keyTip: "T", groups: [
            { id: "enter", label: qsTr("Enter time"), items: [
                { action: "booksTimeActivityAction", size: "large" }, { action: "booksWeeklyTimesheetAction", size: "large" }] },
            { id: "people", label: qsTr("People"), items: ["booksNewEmployeeAction"] }
        ] },
        { id: "payroll", label: qsTr("Payroll"), keyTip: "Y", groups: [
            { id: "payrollSetup", label: qsTr("Setup"), items: [
                { action: "booksPayrollSetupAction", size: "large", label: qsTr("Payroll Setup") }] },
            { id: "ownerPay", label: qsTr("Owner pay"), items: [
                { action: "booksPayOwnerAction", size: "large" }, "booksEstimatedTaxesAction", "booksDistributionAction"] },
            { id: "payrollRun", label: qsTr("Payroll"), items: [
                { action: "booksRunPayrollAction", size: "large" }, "booksPayrollEmployeesAction", "booksPayLiabilitiesAction"] },
            { id: "payrollForms", label: qsTr("Tax forms"), items: [
                { action: "booksPayrollFormsAction", size: "large" }, "booksExportEfw2Action"] }
        ] },
        { id: "banking", label: qsTr("Banking"), keyTip: "B", groups: [
            { id: "connections", label: qsTr("Connections"), items: [
                { action: "booksSyncBankAction", size: "large" }, "booksConnectionsAction"] },
            { id: "statements", label: qsTr("Statements"), items: [
                { action: "booksImportStatementAction", size: "large" }, "booksBankRulesAction"] },
            { id: "reconcile", label: qsTr("Reconcile"), items: [{ action: "booksReconcileAction", size: "large" }] },
            { id: "moveMoney", label: qsTr("Move money"), items: [
                { action: "booksTransferAction", size: "large" }, "booksDepositAction"] }
        ] },
        { id: "accounting", label: qsTr("Accounting"), keyTip: "C", groups: [
            { id: "journal", label: qsTr("Journal"), items: [
                { action: "booksJournalEntryAction", size: "large" }, "booksNewAccountAction"] },
            { id: "planning", label: qsTr("Budget & tax"), items: [
                { action: "booksBudgetsAction", size: "large" }, "booksSalesTaxPaymentAction"] },
            { id: "period", label: qsTr("Period"), items: [{ action: "booksCloseBooksAction", size: "large" }] }
        ] },
        { id: "reports", label: qsTr("Reports"), keyTip: "R", groups: [
            { id: "run", label: qsTr("Report"), items: [{ action: "booksRunReportAction", size: "large" }] },
            { id: "export", label: qsTr("Export"), items: [
                "booksExportReportPdfAction", "booksExportReportCsvAction", "booksExportReportXlsxAction", "filePrintAction"] }
        ] },
        StandardRibbon.viewTab([
            { id: "workspace", label: qsTr("Workspace"), items: [
                { action: "booksViewDashboardAction", size: "large" }, "booksViewSalesAction", "booksViewExpensesAction",
                "booksViewBankingAction", "booksViewAccountingAction", "booksViewReportsAction", "booksViewTimeAction",
                "booksViewPayrollAction"] },
            StandardRibbon.assistantGroup([])
        ])
    ]
}
