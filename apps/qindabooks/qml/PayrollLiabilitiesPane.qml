// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// Payroll ▸ Liabilities (M71, D-081): what the paychecks owe, by agency,
// period and due date (open ones, or all of them), and the payments
// recorded this year with a way to void one. Paying is Pay Payroll
// Liabilities (PayLiabilitiesDialog); the balances by account are the
// Payroll Liability Balances report.
Tk.Flex {
    id: pane

    property BooksHost host: null
    property PayrollService payroll: null
    property int year: 0
    property bool openOnly: true
    property var dues: []
    property var payments: []
    property int pick: -1

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.sm

    function refresh() {
        if (payroll === null) { dues = []; payments = []; return }
        dues = payroll.liabilities("", "", openOnly)
        payments = payroll.liabilityPayments(year)
        pick = -1
    }
    onOpenOnlyChanged: refresh()

    Tk.Flex {
        direction: Tk.Flex.Row
        wrap: Tk.Flex.Wrap
        align: Tk.Flex.Center
        gap: Tk.Theme.space.sm
        Tk.Segmented {
            objectName: "liabilitiesFilter"
            small: true
            model: [qsTr("Open"), qsTr("All")]
            currentIndex: pane.openOnly ? 0 : 1
            onActivated: function(index) { pane.openOnly = index === 0 }
        }
        Tk.Spacer { }
        Tk.Button { objectName: "liabilitiesPay"; small: true; variant: "accent"; iconName: "landmark"; text: qsTr("Pay liabilities…")
                    onClicked: pane.host.actions.trigger("booksPayLiabilitiesAction") }
    }
    Tk.DataTable {
        objectName: "payrollLiabilities"
        Tk.Flex.grow: 1
        Tk.Flex.basis: 0
        model: pane.dues
        alternatingRows: true
        emptyText: pane.openOnly ? qsTr("Nothing owed.") : qsTr("No payroll yet.")
        columns: [
            Tk.TableColumn { key: "label"; title: qsTr("Liability"); flex: 2.4; minWidth: Tk.Theme.size.tableColumn * 2 },
            Tk.TableColumn { key: "period"; title: qsTr("Period"); flex: 1; minWidth: Tk.Theme.size.tableColumn },
            Tk.TableColumn { key: "dueText"; title: qsTr("Due"); width: Tk.Theme.size.tableColumn * 1.2; mono: true },
            Tk.TableColumn { key: "accruedText"; title: qsTr("Owed"); flex: 1; minWidth: Tk.Theme.size.tableColumn; align: Qt.AlignRight; mono: true },
            Tk.TableColumn { key: "paidText"; title: qsTr("Paid"); flex: 1; minWidth: Tk.Theme.size.tableColumn; align: Qt.AlignRight; mono: true },
            Tk.TableColumn { key: "balanceText"; title: qsTr("Balance"); flex: 1; minWidth: Tk.Theme.size.tableColumn; align: Qt.AlignRight; mono: true },
            Tk.TableColumn { key: "overdue"; title: qsTr("Status"); width: Tk.Theme.size.tableColumn
                             formatter: function(value, row) { return row.overdue ? qsTr("Overdue") : (Number(row.balance) === 0 ? qsTr("Paid") : "") } }
        ]
    }
    Tk.Flex {
        direction: Tk.Flex.Row
        align: Tk.Flex.Center
        gap: Tk.Theme.space.sm
        Tk.SectionHeader { title: qsTr("Payments recorded in %1").arg(pane.year); count: String(pane.payments.length); Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
        Tk.Button {
            objectName: "liabilityPaymentVoid"
            small: true; variant: "ghost"; iconName: "circle-x"; text: qsTr("Void…")
            enabled: pane.pick >= 0 && pane.payments[pane.pick] !== undefined && !pane.payments[pane.pick].voided
            onClicked: { reasonPrompt.text = ""; reasonPrompt.open() }
        }
    }
    Tk.DataTable {
        objectName: "payrollLiabilityPayments"
        implicitHeight: Tk.Theme.size.row * (Math.max(1, Math.min(6, pane.payments.length)) + 1.5)
        model: pane.payments
        currentIndex: pane.pick
        onActivated: function(index) { pane.pick = index }
        emptyText: qsTr("No payments recorded yet.")
        columns: [
            Tk.TableColumn { key: "date"; title: qsTr("Paid"); width: Tk.Theme.size.tableColumn * 1.2; mono: true },
            Tk.TableColumn { key: "label"; title: qsTr("Liability"); flex: 2; minWidth: Tk.Theme.size.tableColumn * 2 },
            Tk.TableColumn { key: "period"; title: qsTr("Period"); flex: 1.2; minWidth: Tk.Theme.size.tableColumn * 2; mono: true },
            Tk.TableColumn { key: "amountText"; title: qsTr("Amount"); flex: 1; minWidth: Tk.Theme.size.tableColumn; align: Qt.AlignRight; mono: true },
            Tk.TableColumn { key: "reference"; title: qsTr("Confirmation"); flex: 1; mono: true; muted: true },
            Tk.TableColumn { key: "voided"; title: qsTr("Status"); width: Tk.Theme.size.tableColumn
                             formatter: function(value, row) { return row.voided ? qsTr("Void") : "" } }
        ]
    }
    Tk.PromptDialog {
        id: reasonPrompt
        objectName: "liabilityVoidPrompt"
        acceptEmpty: true
        title: qsTr("Void payment")
        label: qsTr("Why is it void? The liability is owed again.")
        primaryText: qsTr("Void")
        onAccepted: pane.payroll.voidLiabilityPayment(pane.payments[pane.pick].id, text.trim().length > 0 ? text : qsTr("Voided"))
    }
}
