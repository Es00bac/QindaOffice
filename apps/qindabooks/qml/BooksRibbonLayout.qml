// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaOffice.Shell

// QindaBooks' ribbon (D-041, D-047): File (company, backups, import and
// export) · Home (the everyday transactions) · Banking · Accounting ·
// Reports · View. Every string is an ActionEntry id BooksHost registers.
QtObject {
    readonly property var tabs: [
        (function() {
            const tab = StandardRibbon.fileTab([
                { id: "company", label: qsTr("Company"), items: [
                    { action: "booksCompanySettingsAction", size: "large", iconName: "building" },
                    { action: "booksCloseBooksAction", iconName: "lock" },
                    { action: "booksBackupAction", iconName: "archive" }] },
                { id: "transfer", label: qsTr("Import & export"), items: [
                    { action: "booksImportListsAction", iconName: "file-input" },
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
                "booksNewEstimateAction", "booksNewSalesReceiptAction", "booksNewCreditMemoAction", "booksNewCustomerAction"] },
            { id: "vendors", label: qsTr("Vendors"), items: [
                { action: "booksNewExpenseAction", size: "large", split: true,
                  menu: ["booksNewExpenseAction", "booksNewCheckAction", "booksNewBillAction", "booksNewVendorCreditAction"] },
                { action: "booksPayBillsAction", size: "large" },
                "booksNewBillAction", "booksNewCheckAction", "booksNewVendorCreditAction", "booksNewVendorAction"] },
            { id: "lists", label: qsTr("Lists"), items: ["booksNewItemAction", "booksNewAccountAction"] },
            { id: "document", label: qsTr("Document"), items: [
                "booksPrintDocumentAction", "booksEmailDocumentAction", "booksVoidAction"] }
        ] },
        { id: "banking", label: qsTr("Banking"), keyTip: "B", groups: [
            { id: "statements", label: qsTr("Statements"), items: [
                { action: "booksImportStatementAction", size: "large" }, "booksBankRulesAction"] },
            { id: "reconcile", label: qsTr("Reconcile"), items: [{ action: "booksReconcileAction", size: "large" }] },
            { id: "moveMoney", label: qsTr("Move money"), items: [
                { action: "booksTransferAction", size: "large" }, "booksDepositAction"] }
        ] },
        { id: "accounting", label: qsTr("Accounting"), keyTip: "C", groups: [
            { id: "journal", label: qsTr("Journal"), items: [
                { action: "booksJournalEntryAction", size: "large" }, "booksNewAccountAction"] },
            { id: "period", label: qsTr("Period"), items: [{ action: "booksCloseBooksAction", size: "large" }] }
        ] },
        { id: "reports", label: qsTr("Reports"), keyTip: "R", groups: [
            { id: "run", label: qsTr("Report"), items: [{ action: "booksRunReportAction", size: "large" }] },
            { id: "export", label: qsTr("Export"), items: [
                "booksExportReportPdfAction", "booksExportReportCsvAction", "filePrintAction"] }
        ] },
        StandardRibbon.viewTab([
            { id: "workspace", label: qsTr("Workspace"), items: [
                { action: "booksViewDashboardAction", size: "large" }, "booksViewSalesAction", "booksViewExpensesAction",
                "booksViewBankingAction", "booksViewAccountingAction", "booksViewReportsAction"] },
            StandardRibbon.assistantGroup([])
        ])
    ]
}
