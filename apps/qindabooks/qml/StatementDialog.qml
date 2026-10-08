// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs as Dialogs
import QindaTK as Tk
import QindaOffice.Books.App

// Sales ▸ Customer Statements… (M63, D-071): one customer's activity over a
// period — balance forward, every invoice, payment and credit with the
// running balance, the amount due and its aging — on screen, then printed,
// previewed, saved as PDF or handed to QindaMail with the PDF attached.
// Paper, preview and PDF are the same books_io pages: Print and Preview
// name the statement as BooksHost's print source and use the standard
// commands.
Tk.Dialog {
    id: dialog

    property BooksService books: null
    property BooksHost host: null
    property var customers: []
    property var shown: ({})          // BooksService.statement(); {} before a customer is chosen
    property string from: ""
    property string to: ""
    property string problem: ""
    readonly property int customerId: customers.length > 0 && customerBox.currentIndex > 0
                                      ? customers[customerBox.currentIndex].id : 0
    readonly property var presets: [{ id: "thisMonth", text: qsTr("This month") }, { id: "lastMonth", text: qsTr("Last month") },
                                    { id: "thisQuarter", text: qsTr("This quarter") }, { id: "yearToDate", text: qsTr("Year to date") },
                                    { id: "lastYear", text: qsTr("Last fiscal year") }, { id: "last12Months", text: qsTr("Last 12 months") }]

    objectName: "statementDialog"
    title: qsTr("Customer statement")
    subtitle: shown.ok === true ? qsTr("%1 · %2 to %3").arg(shown.customerName).arg(shown.from).arg(shown.to) : ""
    dialogWidth: Tk.Theme.size.dialogWidth * 2
    primaryText: qsTr("E-mail…")
    primaryEnabled: shown.ok === true
    secondaryText: qsTr("Close")
    tertiaryText: shown.ok === true ? qsTr("Save PDF…") : ""

    // customerId 0 = let the user pick.
    function openFor(customerId) {
        customers = [{ id: 0, name: qsTr("Choose a customer") }].concat(books.parties("customer", false))
        customerBox.currentIndex = Math.max(0, customers.map(function(c) { return c.id }).indexOf(customerId || 0))
        const range = books.rangePreset("yearToDate")
        from = range.from; to = range.to
        problem = ""
        refresh()
        open()
    }
    function choose(customerId) {
        customerBox.currentIndex = Math.max(0, customers.map(function(c) { return c.id }).indexOf(customerId))
        refresh()
    }
    function refresh() {
        shown = customerId > 0 ? books.statement(customerId, from, to) : ({})
        problem = shown.ok === false ? String(shown.error || "") : ""
    }
    function applyPreset(id) { const r = books.rangePreset(id); from = r.from; to = r.to; refresh() }
    function source() { host.setPrintSource("statement", customerId, { from: shown.from, to: shown.to }) }
    function preview() { source(); close(); host.actions.trigger("filePrintPreviewAction") }
    function printIt() { source(); close(); host.actions.trigger("filePrintAction") }
    // The file and mail steps; the tests call them directly.
    function savePdf(url) {
        const r = books.exportStatementPdf(customerId, shown.from, shown.to, url)
        problem = r.ok ? "" : String(r.error || "")
        open()
        return r
    }
    function email() {
        const r = books.emailStatement(customerId, shown.from, shown.to)
        problem = r.ok ? "" : String(r.error || "")
        return r
    }
    onAccepted: { if (!email().ok) open() }
    onTertiary: pdfDialog.open()

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Notice { objectName: "statementProblem"; visible: dialog.problem.length > 0; variant: "danger"; title: qsTr("Not done"); text: dialog.problem }
        Tk.Flex {
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.ComboBox {
                id: customerBox
                objectName: "statementCustomer"
                small: true
                implicitWidth: Tk.Theme.size.fieldWidth * 1.6
                textRole: "name"
                model: dialog.customers
                onActivated: dialog.refresh()
            }
            Tk.ComboBox {
                objectName: "statementPreset"
                small: true
                textRole: "text"
                model: dialog.presets
                currentIndex: 3
                onActivated: function(i) { dialog.applyPreset(dialog.presets[i].id) }
            }
            Tk.TextField { objectName: "statementFrom"; small: true; mono: true; implicitWidth: Tk.Theme.size.fieldWidth; text: dialog.from; onEditingFinished: { dialog.from = text; dialog.refresh() } }
            Tk.Caption { text: qsTr("to") }
            Tk.TextField { objectName: "statementTo"; small: true; mono: true; implicitWidth: Tk.Theme.size.fieldWidth; text: dialog.to; onEditingFinished: { dialog.to = text; dialog.refresh() } }
            Tk.Spacer { }
            Tk.Button { objectName: "statementPreview"; small: true; variant: "ghost"; iconName: "eye"; text: qsTr("Preview"); enabled: dialog.shown.ok === true; onClicked: dialog.preview() }
            Tk.Button { objectName: "statementPrint"; small: true; variant: "ghost"; iconName: "printer"; text: qsTr("Print…"); enabled: dialog.shown.ok === true; onClicked: dialog.printIt() }
        }
        Tk.EmptyState {
            visible: dialog.customerId === 0
            iconName: "scroll-text"
            title: qsTr("Choose a customer")
            text: qsTr("A statement lists what the customer was invoiced and paid in the period, and what they owe now.")
        }
        Tk.Flex {
            visible: dialog.shown.ok === true
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.sm
            Tk.Flex {
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.xl
                // AGENT-NOTE: a KeyValue's key takes no width of its own
                // (flex basis 0); in a row each one needs a width.
                Tk.KeyValue { objectName: "statementOpening"; implicitWidth: Tk.Theme.size.tableColumn * 2.4; key: qsTr("Balance forward"); value: String(dialog.shown.openingText || "") }
                Tk.KeyValue { objectName: "statementClosing"; implicitWidth: Tk.Theme.size.tableColumn * 2.4; key: qsTr("Amount due"); value: String(dialog.shown.closingText || ""); accent: true }
            }
            Tk.DataTable {
                objectName: "statementLines"
                implicitHeight: Tk.Theme.size.panelMinHeight * 2.4
                model: dialog.shown.lines || []
                alternatingRows: true
                emptyText: qsTr("No activity in this period")
                columns: [
                    Tk.TableColumn { key: "date"; title: qsTr("Date"); width: Tk.Theme.size.tableColumn * 1.2; mono: true },
                    Tk.TableColumn { key: "description"; title: qsTr("Activity"); flex: 1 },
                    Tk.TableColumn { key: "amountText"; title: qsTr("Amount"); width: Tk.Theme.size.tableColumn * 1.4; align: Qt.AlignRight; mono: true },
                    Tk.TableColumn { key: "balanceText"; title: qsTr("Balance"); width: Tk.Theme.size.tableColumn * 1.4; align: Qt.AlignRight; mono: true }
                ]
            }
            Tk.Flex {
                objectName: "statementAging"
                direction: Tk.Flex.Row
                wrap: Tk.Flex.Wrap
                gap: Tk.Theme.space.lg
                Repeater {
                    model: dialog.shown.aging || []
                    delegate: Tk.KeyValue {
                        required property var modelData
                        implicitWidth: Tk.Theme.size.tableColumn * 1.9
                        key: String(modelData.label)
                        value: String(modelData.amountText)
                    }
                }
            }
            Tk.Caption {
                visible: dialog.shown.ok === true && String(dialog.shown.email || "").length === 0
                text: qsTr("%1 has no e-mail address; QindaMail opens the message for you to address.").arg(dialog.shown.customerName || "")
            }
        }
    }
    Dialogs.FileDialog {
        id: pdfDialog
        title: qsTr("Save the statement as PDF")
        fileMode: Dialogs.FileDialog.SaveFile
        defaultSuffix: "pdf"
        nameFilters: [qsTr("PDF (*.pdf)")]
        onAccepted: dialog.savePdf(selectedFile)
        onRejected: dialog.open()
    }
}
