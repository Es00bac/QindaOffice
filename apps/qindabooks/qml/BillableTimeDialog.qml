// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// Billable time onto an invoice (M68, D-077), QuickBooks' "Choose Billable
// Time": the customer's unbilled billable time — date, who, service, notes,
// duration, rate, amount. The chosen entries become invoice lines (the
// service item, hours as the quantity, the rate, the notes) that remember
// their time entry: saved, the time is billed; voided or removed from the
// invoice, it is unbilled again.
Tk.Dialog {
    id: dialog

    property BooksService books: null
    property int customerId: 0
    property string customerName: ""
    property var rows: []
    property var picked: ({})   // entry id -> true
    signal chosen(var lines)

    objectName: "billableTimeDialog"
    title: qsTr("Billable time")
    subtitle: customerName
    dialogWidth: Tk.Theme.size.dialogWidth * 2
    primaryText: qsTr("Add to invoice")
    secondaryText: qsTr("Cancel")
    primaryEnabled: Object.keys(picked).length > 0

    // `skip`: time the invoice already bills.
    function openFor(id, name, skip) {
        customerId = id
        customerName = name || ""
        rows = books.unbilledTime(id).filter(function(r) { return (skip || []).indexOf(r.id) < 0 })
        picked = ({})
        error.visible = false
        open()
    }
    function toggle(id, on) {
        const next = Object.assign({}, picked)
        if (on) next[id] = true
        else delete next[id]
        picked = next
    }
    function pickAll() { const next = ({}); rows.forEach(function(r) { next[r.id] = true }); picked = next }
    function addLines() {
        const result = books.timeInvoiceLines(customerId, Object.keys(picked).map(Number))
        if (!result.ok) { error.text = String(result.error || ""); error.visible = true; return false }
        chosen(result.lines)
        return true
    }
    onAccepted: if (!addLines()) open()

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { id: error; visible: false; variant: "danger"; title: qsTr("Not added") }
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Caption { Tk.Flex.grow: 1; text: dialog.rows.length === 0 ? qsTr("No unbilled billable time for this customer.")
                                                : dialog.rows.length === 1 ? qsTr("1 time entry not billed yet")
                                                                           : qsTr("%1 time entries not billed yet").arg(dialog.rows.length) }
            Tk.Button { objectName: "billableSelectAll"; small: true; variant: "outline"; iconName: "check-check"; text: qsTr("Select all"); visible: dialog.rows.length > 0; onClicked: dialog.pickAll() }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Item { implicitWidth: Tk.Theme.size.control }
            Tk.Overline { title: qsTr("Date"); Tk.Flex.basis: Tk.Theme.size.tableColumn * 1.1; Tk.Flex.shrink: 0 }
            Tk.Overline { title: qsTr("Name"); Tk.Flex.basis: Tk.Theme.size.tableColumn * 1.4; Tk.Flex.shrink: 0 }
            Tk.Overline { title: qsTr("Service and notes"); Tk.Flex.grow: 1 }
            Tk.Overline { title: qsTr("Hours"); Tk.Flex.basis: Tk.Theme.size.tableColumn * 0.8; Tk.Flex.shrink: 0; horizontalAlignment: Text.AlignRight }
            Tk.Overline { title: qsTr("Rate"); Tk.Flex.basis: Tk.Theme.size.tableColumn; Tk.Flex.shrink: 0; horizontalAlignment: Text.AlignRight }
            Tk.Overline { title: qsTr("Amount"); Tk.Flex.basis: Tk.Theme.size.tableColumn; Tk.Flex.shrink: 0; horizontalAlignment: Text.AlignRight }
        }
        Tk.Divider { }
        Repeater {
            model: dialog.rows
            delegate: Tk.Flex {
                id: row
                required property var modelData
                objectName: "billableTime_" + modelData.id
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                Tk.CheckBox {
                    implicitWidth: Tk.Theme.size.control
                    checked: dialog.picked[row.modelData.id] === true
                    onToggled: {
                        dialog.toggle(row.modelData.id, checked)
                        checked = Qt.binding(function() { return dialog.picked[row.modelData.id] === true })
                    }
                }
                Tk.Mono { text: String(row.modelData.date); Tk.Flex.basis: Tk.Theme.size.tableColumn * 1.1; Tk.Flex.shrink: 0 }
                Tk.Label { text: String(row.modelData.workerName); elide: Text.ElideRight; Tk.Flex.basis: Tk.Theme.size.tableColumn * 1.4; Tk.Flex.shrink: 0 }
                Tk.Label {
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    elide: Text.ElideRight
                    text: String(row.modelData.itemName) + (row.modelData.notes ? " — " + row.modelData.notes : "")
                }
                Tk.Mono { text: qsTr("%1 h").arg(row.modelData.hours); Tk.Flex.basis: Tk.Theme.size.tableColumn * 0.8; Tk.Flex.shrink: 0; horizontalAlignment: Text.AlignRight }
                Tk.Mono { text: String(row.modelData.rateText); Tk.Flex.basis: Tk.Theme.size.tableColumn; Tk.Flex.shrink: 0; horizontalAlignment: Text.AlignRight }
                Tk.Mono { text: String(row.modelData.amountText); Tk.Flex.basis: Tk.Theme.size.tableColumn; Tk.Flex.shrink: 0; horizontalAlignment: Text.AlignRight }
            }
        }
    }
}
