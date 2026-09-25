// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// Receive a customer payment against open invoices, or pay several bills
// at once (D-047). The amount is split oldest-first by default; each line's
// applied amount stays editable text the service validates exactly.
Tk.Dialog {
    id: dialog

    property BooksService books: null
    property string kind: "receive"          // receive | payBills
    property var parties: []
    property var accounts: []
    property var openRows: []                // open invoices / unpaid bills
    property var applied: ({})               // id -> amount text

    objectName: "paymentDialog"
    title: kind === "receive" ? qsTr("Receive payment") : qsTr("Pay bills")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.8
    primaryText: kind === "receive" ? qsTr("Record payment") : qsTr("Pay selected bills")
    secondaryText: qsTr("Cancel")
    primaryEnabled: Object.keys(applied).some(function(k) { return String(dialog.applied[k]).length > 0 })

    function openFor(k) {
        kind = k
        parties = kind === "receive" ? [{ id: 0, name: qsTr("Choose a customer") }].concat(books.parties("customer", false)) : []
        accounts = books.paymentAccounts()
        applied = ({})
        error.visible = false
        amountField.text = ""
        reload(0)
        open()
    }
    function reload(partyId) {
        openRows = kind === "receive" ? (partyId > 0 ? books.openInvoices(partyId) : []) : books.unpaidBills(0)
        applied = ({})
    }
    function setApplied(id, value) { const next = Object.assign({}, applied); next[id] = value; applied = next }
    function payInFull(row, on) { setApplied(row.id, on ? String(row.balancePlain || "") : "") }
    onAccepted: {
        const rows = Object.keys(applied).filter(function(k) { return String(dialog.applied[k]).length > 0 })
        let result
        if (kind === "receive") {
            result = books.receivePayment({ customerId: dialog.parties[partyBox.currentIndex].id, date: dateField.text,
                amount: amountField.text, depositAccountId: dialog.accounts[accountBox.currentIndex].id,
                method: methodField.text, reference: referenceField.text,
                applications: rows.map(function(k) { return { invoiceId: Number(k), amount: dialog.applied[k] } }) })
        } else {
            result = books.payBills({ date: dateField.text, paymentAccountId: dialog.accounts[accountBox.currentIndex].id,
                reference: referenceField.text, bills: rows.map(function(k) { return { billId: Number(k), amount: dialog.applied[k] } }) })
        }
        if (!result.ok) { error.text = String(result.error || ""); error.visible = true; open() }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Notice { id: error; visible: false; variant: "danger"; title: qsTr("Not recorded") }
        Tk.Grid {
            columns: "1fr 1fr"
            columnGap: Tk.Theme.space.lg
            rowGap: Tk.Theme.space.sm
            Tk.PropertyRow {
                visible: dialog.kind === "receive"
                label: qsTr("Customer")
                Tk.ComboBox { id: partyBox; objectName: "paymentCustomer"; small: true; textRole: "name"; model: dialog.parties
                              onActivated: function(i) { dialog.reload(dialog.parties[i].id) } }
            }
            Tk.PropertyRow { label: qsTr("Date"); Tk.TextField { id: dateField; objectName: "paymentDate"; small: true; mono: true; text: dialog.books !== null ? dialog.books.today : "" } }
            Tk.PropertyRow {
                label: dialog.kind === "receive" ? qsTr("Deposit to") : qsTr("Pay from")
                Tk.ComboBox { id: accountBox; objectName: "paymentAccount"; small: true; textRole: "name"; model: dialog.accounts }
            }
            Tk.PropertyRow { visible: dialog.kind === "receive"; label: qsTr("Amount received"); Tk.TextField { id: amountField; objectName: "paymentAmount"; small: true; mono: true; placeholderText: qsTr("Total of the lines below") } }
            Tk.PropertyRow { visible: dialog.kind === "receive"; label: qsTr("Method"); Tk.TextField { id: methodField; small: true; placeholderText: qsTr("Check, card, transfer…") } }
            Tk.PropertyRow { label: qsTr("Reference"); Tk.TextField { id: referenceField; small: true; placeholderText: qsTr("Check or transaction number") } }
        }
        Tk.Overline { title: dialog.kind === "receive" ? qsTr("Open invoices") : qsTr("Unpaid bills") }
        Tk.Caption { visible: dialog.openRows.length === 0; text: dialog.kind === "receive" ? qsTr("Pick a customer to see what they owe.") : qsTr("No unpaid bills.") }
        Repeater {
            model: dialog.openRows
            delegate: Tk.Flex {
                id: row
                required property var modelData
                objectName: "paymentLine_" + modelData.id
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                Tk.CheckBox { checked: String(dialog.applied[row.modelData.id] || "").length > 0; onToggled: dialog.payInFull(row.modelData, checked) }
                Tk.Mono { text: String(row.modelData.number || ""); Tk.Flex.basis: Tk.Theme.size.tableColumn; Tk.Flex.shrink: 0}
                Tk.Label { text: String(row.modelData.partyName || ""); visible: dialog.kind === "payBills"; Tk.Flex.basis: Tk.Theme.size.tableColumn * 1.6; Tk.Flex.shrink: 0; elide: Text.ElideRight }
                Tk.Caption { text: qsTr("Due %1").arg(row.modelData.dueDate || "—"); Tk.Flex.grow: 1; color: row.modelData.status === "overdue" ? Tk.Theme.color.danger : Tk.Theme.color.textMuted }
                Tk.Mono { text: String(row.modelData.balanceText || "") }
                Tk.TextField { small: true; mono: true; implicitWidth: Tk.Theme.size.tableColumn * 1.2; horizontalAlignment: Text.AlignRight; placeholderText: "0.00"
                               text: String(dialog.applied[row.modelData.id] || ""); onEditingFinished: dialog.setApplied(row.modelData.id, text) }
            }
        }
    }
}
