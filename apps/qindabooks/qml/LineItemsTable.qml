// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// The lines of a document: product/service (sales) or category account
// (bills and expenses), description, quantity, rate, the computed amount
// and — on sales — taxable. Quantities and rates stay TEXT here; the
// service parses them exactly (Decimal) and returns each line's amount in
// `amounts`, so this view never multiplies money.
Tk.Flex {
    id: table

    property var lines: []
    property string mode: "item"          // item | account
    property var items: []                // [{id, name, description, price (text), taxable}]
    property var accounts: []             // [{id, name}]
    property bool showTax: true
    property bool readOnly: false
    property var amounts: []              // amountText per line, from computeTotals
    signal edited(var lines)

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.xs

    function blank() { return { itemId: 0, accountId: 0, description: "", quantity: "1", rate: "", taxable: mode === "item" } }
    function update(index, field, value) {
        const next = lines.slice()
        next[index] = Object.assign({}, next[index], { [field]: value })
        if (field === "itemId") {
            const item = items.find(function(i) { return i.id === value })
            if (item) next[index] = Object.assign(next[index], { description: item.description || item.name, rate: String(item.price || ""), taxable: !!item.taxable })
        }
        edited(next)
    }
    function remove(index) { const next = lines.slice(); next.splice(index, 1); edited(next.length ? next : [blank()]) }
    function add() { edited(lines.concat([blank()])) }

    // Header
    Tk.Flex {
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.sm
        Tk.Overline { title: table.mode === "item" ? qsTr("Product or service") : qsTr("Category"); Tk.Flex.flex: 2 }
        Tk.Overline { title: qsTr("Description"); Tk.Flex.flex: 3 }
        Tk.Overline { title: qsTr("Qty"); Tk.Flex.basis: Tk.Theme.size.controlLg * 2.2; Tk.Flex.shrink: 0; horizontalAlignment: Text.AlignRight }
        Tk.Overline { title: qsTr("Rate"); Tk.Flex.basis: Tk.Theme.size.tableColumn * 1.1; Tk.Flex.shrink: 0; horizontalAlignment: Text.AlignRight }
        Tk.Overline { title: qsTr("Amount"); Tk.Flex.basis: Tk.Theme.size.tableColumn * 1.2; Tk.Flex.shrink: 0; horizontalAlignment: Text.AlignRight }
        Tk.Overline { visible: table.showTax; title: qsTr("Tax"); Tk.Flex.basis: Tk.Theme.size.control; Tk.Flex.shrink: 0}
        Item { implicitWidth: Tk.Theme.size.control }
    }
    Tk.Divider { }
    Repeater {
        model: table.lines.length
        delegate: Tk.Flex {
            id: line
            required property int index
            readonly property var lineData: table.lines[index] || ({})
            objectName: "documentLine_" + index
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.ComboBox {
                objectName: "lineItem"
                Tk.Flex.flex: 2
                small: true
                enabled: !table.readOnly
                textRole: "name"
                model: [{ id: 0, name: table.mode === "item" ? qsTr("(description only)") : qsTr("Choose a category") }]
                       .concat(table.mode === "item" ? table.items : table.accounts)
                currentIndex: Math.max(0, model.map(function(m) { return m.id }).indexOf(table.mode === "item" ? (line.lineData.itemId || 0) : (line.lineData.accountId || 0)))
                onActivated: function(i) { table.update(line.index, table.mode === "item" ? "itemId" : "accountId", model[i].id) }
            }
            Tk.TextField {
                objectName: "lineDescription"
                Tk.Flex.flex: 3
                small: true
                readOnly: table.readOnly
                text: String(line.lineData.description || "")
                placeholderText: qsTr("Description")
                onEditingFinished: if (text !== String(line.lineData.description || "")) table.update(line.index, "description", text)
            }
            Tk.TextField {
                objectName: "lineQuantity"
                implicitWidth: Tk.Theme.size.controlLg * 2.2
                small: true
                mono: true
                readOnly: table.readOnly
                horizontalAlignment: Text.AlignRight
                text: String(line.lineData.quantity || "")
                onEditingFinished: if (text !== String(line.lineData.quantity || "")) table.update(line.index, "quantity", text)
            }
            Tk.TextField {
                objectName: "lineRate"
                implicitWidth: Tk.Theme.size.tableColumn * 1.1
                small: true
                mono: true
                readOnly: table.readOnly
                horizontalAlignment: Text.AlignRight
                placeholderText: "0.00"
                text: String(line.lineData.rate || "")
                onEditingFinished: if (text !== String(line.lineData.rate || "")) table.update(line.index, "rate", text)
            }
            Tk.Mono {
                objectName: "lineAmount"
                Tk.Flex.basis: Tk.Theme.size.tableColumn * 1.2; Tk.Flex.shrink: 0
                horizontalAlignment: Text.AlignRight
                text: String(table.amounts[line.index] || "")
            }
            Tk.CheckBox {
                visible: table.showTax
                implicitWidth: Tk.Theme.size.control
                enabled: !table.readOnly
                checked: line.lineData.taxable === true
                tooltip: qsTr("Taxable")
                onToggled: table.update(line.index, "taxable", checked)
            }
            Tk.IconButton {
                small: true
                iconName: "x"
                enabled: !table.readOnly
                tooltip: qsTr("Remove this line")
                onClicked: table.remove(line.index)
            }
        }
    }
    Tk.Button {
        objectName: "lineAdd"
        visible: !table.readOnly
        small: true
        variant: "ghost"
        iconName: "plus"
        text: qsTr("Add a line")
        onClicked: table.add()
    }
}
