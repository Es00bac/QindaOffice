// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Books.App

// Receive against purchase orders (M68, D-077): the vendor's open order
// lines with what is still to come; tick a line to receive all of it, or
// type part of it. OK hands the bill editor its new lines — each one
// remembers its order line, so the order's received quantities follow the
// bill (and come back if the bill is voided).
Tk.Dialog {
    id: dialog

    property BooksService books: null
    property string vendorName: ""
    property var rows: []          // openOrderLines(vendor)
    property var quantities: ({})  // lineId -> text
    signal received(var lines)

    objectName: "receiveOrderDialog"
    title: qsTr("Receive against purchase orders")
    subtitle: vendorName
    dialogWidth: Tk.Theme.size.dialogWidth * 2
    primaryText: qsTr("Add to bill")
    secondaryText: qsTr("Cancel")
    primaryEnabled: Object.keys(quantities).some(function(k) { return String(dialog.quantities[k]).length > 0 })

    // `skip`: order lines the bill already receives.
    function openFor(vendorId, name, skip) {
        vendorName = name || ""
        rows = books.openOrderLines(vendorId).filter(function(r) { return (skip || []).indexOf(r.lineId) < 0 })
        quantities = ({})
        error.visible = false
        open()
    }
    function setQuantity(lineId, text) { const next = Object.assign({}, quantities); next[lineId] = text; quantities = next }
    function receiveAll() {
        const next = ({})
        rows.forEach(function(r) { next[r.lineId] = String(r.remaining) })
        quantities = next
    }
    function receive() {
        const receipts = Object.keys(quantities).filter(function(k) { return String(dialog.quantities[k]).length > 0 })
            .map(function(k) { return { lineId: Number(k), quantity: dialog.quantities[k] } })
        const result = books.receiveOrderLines(receipts)
        if (!result.ok) { error.text = String(result.error || ""); error.visible = true; return false }
        received(result.lines)
        return true
    }
    onAccepted: if (!receive()) open()

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { id: error; visible: false; variant: "danger"; title: qsTr("Not received") }
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Caption { Tk.Flex.grow: 1; text: dialog.rows.length === 0 ? qsTr("Nothing is waiting to be received from this vendor.")
                                                : dialog.rows.length === 1 ? qsTr("1 order line still to receive")
                                                                           : qsTr("%1 order lines still to receive").arg(dialog.rows.length) }
            Tk.Button { objectName: "receiveAll"; small: true; variant: "outline"; iconName: "check-check"; text: qsTr("Receive everything"); visible: dialog.rows.length > 0; onClicked: dialog.receiveAll() }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Item { implicitWidth: Tk.Theme.size.control }
            Tk.Overline { title: qsTr("P.O."); Tk.Flex.basis: Tk.Theme.size.tableColumn; Tk.Flex.shrink: 0 }
            Tk.Overline { title: qsTr("Item or description"); Tk.Flex.grow: 1 }
            Tk.Overline { title: qsTr("Ordered"); Tk.Flex.basis: Tk.Theme.size.tableColumn * 0.7; Tk.Flex.shrink: 0; horizontalAlignment: Text.AlignRight }
            Tk.Overline { title: qsTr("To come"); Tk.Flex.basis: Tk.Theme.size.tableColumn * 0.7; Tk.Flex.shrink: 0; horizontalAlignment: Text.AlignRight }
            Tk.Overline { title: qsTr("Rate"); Tk.Flex.basis: Tk.Theme.size.tableColumn; Tk.Flex.shrink: 0; horizontalAlignment: Text.AlignRight }
            Tk.Overline { title: qsTr("Receive"); Tk.Flex.basis: Tk.Theme.size.tableColumn; Tk.Flex.shrink: 0; horizontalAlignment: Text.AlignRight }
        }
        Tk.Divider { }
        Repeater {
            model: dialog.rows
            delegate: Tk.Flex {
                id: row
                required property var modelData
                objectName: "receiveLine_" + modelData.lineId
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                Tk.CheckBox {
                    implicitWidth: Tk.Theme.size.control
                    checked: String(dialog.quantities[row.modelData.lineId] || "").length > 0
                    tooltip: qsTr("Receive all that is left")
                    onToggled: {
                        dialog.setQuantity(row.modelData.lineId, checked ? String(row.modelData.remaining) : "")
                        checked = Qt.binding(function() { return String(dialog.quantities[row.modelData.lineId] || "").length > 0 })
                    }
                }
                Tk.Mono { text: String(row.modelData.orderNumber || ""); Tk.Flex.basis: Tk.Theme.size.tableColumn; Tk.Flex.shrink: 0 }
                Tk.Label {
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    elide: Text.ElideRight
                    text: (row.modelData.itemName ? row.modelData.itemName + " — " : "") + String(row.modelData.description || "")
                          + (row.modelData.customerName ? "  ·  " + row.modelData.customerName : "")
                }
                Tk.Mono { text: String(row.modelData.ordered); Tk.Flex.basis: Tk.Theme.size.tableColumn * 0.7; Tk.Flex.shrink: 0; horizontalAlignment: Text.AlignRight }
                Tk.Mono { text: String(row.modelData.remaining); Tk.Flex.basis: Tk.Theme.size.tableColumn * 0.7; Tk.Flex.shrink: 0; horizontalAlignment: Text.AlignRight }
                Tk.Mono { text: String(row.modelData.rateText || "—"); Tk.Flex.basis: Tk.Theme.size.tableColumn; Tk.Flex.shrink: 0; horizontalAlignment: Text.AlignRight }
                Tk.TextField {
                    objectName: "receiveQuantity"
                    small: true; mono: true
                    implicitWidth: Tk.Theme.size.tableColumn
                    horizontalAlignment: Text.AlignRight
                    placeholderText: "0"
                    text: String(dialog.quantities[row.modelData.lineId] || "")
                    onEditingFinished: dialog.setQuantity(row.modelData.lineId, text.trim())
                }
            }
        }
    }
}
