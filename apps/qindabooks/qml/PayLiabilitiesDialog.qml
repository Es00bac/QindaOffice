// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// Payroll ▸ Pay Payroll Liabilities… (M71, D-081): what the paychecks owe
// each agency, grouped as each is paid — one federal deposit for income
// tax, social security and Medicare (by the company's monthly or
// semiweekly schedule), FUTA by quarter, Colorado withholding by its
// filing frequency, UI and FAMLI by quarter, a city's head tax, and the
// deductions owed to plans — with the due date. Recording a payment the
// owner made (EFTPS, Revenue Online, MyUI Employer+, My FAMLI+ Employer)
// clears the liability accounts; a difference of up to a dollar of
// rounding posts to Payroll Taxes. QindaBooks never pays an agency.
Tk.Dialog {
    id: dialog

    property BooksService books: null
    property PayrollService payroll: null
    property var dues: []
    property var accounts: []
    property int picked: 0
    property string problem: ""
    // Two columns of fields, one when the window is narrow.
    readonly property string fieldColumns: width < Tk.Theme.size.dialogWidth * 1.5 ? "1fr" : "1fr 1fr"
    readonly property var due: picked >= 0 && picked < dues.length ? dues[picked] : null

    objectName: "payLiabilitiesDialog"
    title: qsTr("Pay payroll liabilities")
    subtitle: qsTr("Taxes withheld and owed, by agency and due date")
    dialogWidth: Tk.Theme.size.dialogWidth * 2.2
    primaryText: qsTr("Record payment")
    secondaryText: qsTr("Close")
    primaryEnabled: due !== null && accounts.length > 0

    function openDialog() {
        accounts = books.bankAccounts()
        const setup = payroll.setup()
        let bank = 0
        for (let i = 0; i < accounts.length; ++i) if (Number(accounts[i].id) === Number(setup.payrollBankAccountId || 0)) bank = i
        bankBox.currentIndex = bank
        reload()
        problem = ""
        open()
    }
    function reload() {
        dues = payroll.liabilities("", "", true)
        pick(dues.length > 0 ? 0 : -1)
    }
    function pick(index) {
        picked = index
        amountField.text = due !== null ? String(due.balancePlain) : ""
        dateField.text = books.today
        referenceField.text = ""
    }
    // The record step; the tests pick a row, fill the fields and call it.
    function record() {
        if (due === null) return { ok: false, error: qsTr("Nothing is owed.") }
        const r = payroll.payLiability({ tax: due.tax, start: due.start, end: due.end,
                                         bankAccountId: accounts.length > 0 ? accounts[bankBox.currentIndex].id : 0,
                                         date: dateField.text, amount: amountField.text, reference: referenceField.text })
        problem = r.ok ? "" : String(r.error || "")
        if (r.ok) reload()
        return r
    }
    onAccepted: { record(); if (dues.length > 0 || problem.length > 0) open() }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Notice { objectName: "payLiabilitiesProblem"; visible: dialog.problem.length > 0; variant: "danger"; title: qsTr("Not recorded"); text: dialog.problem }
        Tk.Caption {
            wrapMode: Text.Wrap
            text: qsTr("Pay each one at the agency first — EFTPS for federal deposits, Revenue Online for Colorado withholding, MyUI Employer+ for unemployment, My FAMLI+ Employer for FAMLI, the city for its head tax — then record it here with the confirmation number.")
        }
        Tk.EmptyState {
            visible: dialog.dues.length === 0
            iconName: "circle-check"
            title: qsTr("Nothing owed")
            text: qsTr("Every payroll liability is paid.")
        }
        Tk.DataTable {
            objectName: "payrollDues"
            visible: dialog.dues.length > 0
            implicitHeight: Tk.Theme.size.row * (Math.min(8, dialog.dues.length) + 1.5)
            model: dialog.dues
            currentIndex: dialog.picked
            onActivated: function(index) { dialog.pick(index) }
            columns: [
                Tk.TableColumn { key: "label"; title: qsTr("Liability"); flex: 2.4; minWidth: Tk.Theme.size.tableColumn * 2 },
                Tk.TableColumn { key: "period"; title: qsTr("Period"); flex: 1; minWidth: Tk.Theme.size.tableColumn },
                Tk.TableColumn { key: "dueText"; title: qsTr("Due"); width: Tk.Theme.size.tableColumn * 1.2; mono: true },
                Tk.TableColumn { key: "accruedText"; title: qsTr("Owed"); flex: 1; minWidth: Tk.Theme.size.tableColumn; align: Qt.AlignRight; mono: true },
                Tk.TableColumn { key: "paidText"; title: qsTr("Paid"); flex: 1; minWidth: Tk.Theme.size.tableColumn; align: Qt.AlignRight; mono: true },
                Tk.TableColumn {
                    key: "balanceText"; title: qsTr("Balance"); flex: 1.2; minWidth: Tk.Theme.size.tableColumn * 1.2; align: Qt.AlignRight
                    delegate: Item {
                        Tk.Badge {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.right: parent.right
                            text: parent.parent && parent.parent.row ? String(parent.parent.row.balanceText || "") : ""
                            variant: parent.parent && parent.parent.row && parent.parent.row.overdue ? "danger" : "default"
                        }
                    }
                }
            ]
        }
        Tk.Caption {
            objectName: "payrollDueNotes"
            visible: dialog.due !== null && String(dialog.due.notes || "").length > 0
            wrapMode: Text.Wrap
            text: dialog.due !== null ? String(dialog.due.notes || "") : ""
        }
        Tk.Grid {
            visible: dialog.due !== null
            columns: dialog.fieldColumns
            columnGap: Tk.Theme.space.lg
            rowGap: Tk.Theme.space.sm
            Tk.PropertyRow { label: qsTr("Paying"); Tk.Label { text: dialog.due !== null ? dialog.due.label + " — " + dialog.due.period : ""; elide: Text.ElideRight } }
            Tk.PropertyRow { label: qsTr("Pay at"); Tk.Label { text: dialog.due !== null ? String(dialog.due.agency) : ""; elide: Text.ElideRight } }
            Tk.PropertyRow {
                label: qsTr("Amount")
                Tk.TextField { id: amountField; objectName: "payLiabilityAmount"; small: true; mono: true; horizontalAlignment: Text.AlignRight }
            }
            Tk.PropertyRow { label: qsTr("Date paid"); Tk.TextField { id: dateField; objectName: "payLiabilityDate"; small: true; mono: true } }
            Tk.PropertyRow {
                label: qsTr("From")
                Tk.ComboBox { id: bankBox; objectName: "payLiabilityBank"; small: true; textRole: "name"; model: dialog.accounts }
            }
            Tk.PropertyRow {
                label: qsTr("Confirmation")
                Tk.TextField { id: referenceField; objectName: "payLiabilityReference"; small: true; mono: true; placeholderText: qsTr("EFTPS or agency number") }
            }
        }
    }
}
