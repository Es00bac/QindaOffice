// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// The lines of a purchase order (M68, D-077): an item or a category,
// description, customer:job, quantity, rate, the computed amount, what
// bills received, and — on a saved line — closed by hand. Quantities and
// rates stay TEXT; the service parses them exactly and returns each line's
// amount in `amounts`, so this view never multiplies money.
Tk.Flex {
    id: table

    property var lines: []
    property var items: []       // [{id, name, description, cost (text)}]
    property var accounts: []    // expense categories [{id, name}]
    property var customers: []   // [{id, name}]
    property bool readOnly: false
    property var amounts: []     // amountText per line, from computeTotals
    signal edited(var lines)
    signal closeLine(int lineId, bool closed)

    // One picker for both: "item:<id>" or "account:<id>".
    readonly property var choices: [{ key: "", name: qsTr("Choose an item or category") }]
        .concat(items.map(function(i) { return { key: "item:" + i.id, name: i.name } }))
        .concat(accounts.map(function(a) { return { key: "account:" + a.id, name: qsTr("Category: %1").arg(a.name) } }))
    readonly property var jobs: [{ id: 0, name: qsTr("(no customer:job)") }].concat(customers)

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.xs

    function blank() { return { itemId: 0, accountId: 0, description: "", quantity: "1", rate: "", customerId: 0 } }
    function keyOf(line) { return line.itemId ? "item:" + line.itemId : line.accountId ? "account:" + line.accountId : "" }
    function choose(index, key) {
        const next = lines.slice()
        const line = Object.assign({}, next[index])
        const id = Number(key.split(":")[1] || 0)
        if (key.indexOf("item:") === 0) {
            const item = items.find(function(i) { return i.id === id })
            Object.assign(line, { itemId: id, accountId: 0, description: item ? (item.description || item.name) : "",
                                  rate: item && item.cost !== "0.00" ? String(item.cost) : "" })
        } else {
            Object.assign(line, { itemId: 0, accountId: id })
        }
        next[index] = line
        edited(next)
    }
    function update(index, field, value) {
        const next = lines.slice()
        next[index] = Object.assign({}, next[index], { [field]: value })
        edited(next)
    }
    function remove(index) { const next = lines.slice(); next.splice(index, 1); edited(next.length ? next : [blank()]) }

    Tk.Flex {
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.sm
        Tk.Overline { title: qsTr("Item or category"); Tk.Flex.flex: 3 }
        Tk.Overline { title: qsTr("Description"); Tk.Flex.flex: 3 }
        Tk.Overline { title: qsTr("Customer:job"); Tk.Flex.flex: 2 }
        Tk.Overline { title: qsTr("Qty"); Tk.Flex.basis: Tk.Theme.size.controlLg * 1.8; Tk.Flex.shrink: 0; horizontalAlignment: Text.AlignRight }
        Tk.Overline { title: qsTr("Rate"); Tk.Flex.basis: Tk.Theme.size.tableColumn; Tk.Flex.shrink: 0; horizontalAlignment: Text.AlignRight }
        Tk.Overline { title: qsTr("Amount"); Tk.Flex.basis: Tk.Theme.size.tableColumn * 1.1; Tk.Flex.shrink: 0; horizontalAlignment: Text.AlignRight }
        Tk.Overline { title: qsTr("Received"); Tk.Flex.basis: Tk.Theme.size.tableColumn; Tk.Flex.shrink: 0; horizontalAlignment: Text.AlignRight }
        Tk.Overline { title: qsTr("Closed"); Tk.Flex.basis: Tk.Theme.size.control * 2.2; Tk.Flex.shrink: 0 }
        Item { implicitWidth: Tk.Theme.size.control }
    }
    Tk.Divider { }
    Repeater {
        model: table.lines.length
        delegate: Tk.Flex {
            id: line
            required property int index
            readonly property var lineData: table.lines[index] || ({})
            readonly property bool received: Number(lineData.received || 0) > 0
            objectName: "orderLine_" + index
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.ComboBox {
                objectName: "orderLineItem"
                Tk.Flex.flex: 3
                small: true
                enabled: !table.readOnly
                textRole: "name"
                model: table.choices
                currentIndex: Math.max(0, table.choices.map(function(c) { return c.key }).indexOf(table.keyOf(line.lineData)))
                onActivated: function(i) { table.choose(line.index, table.choices[i].key) }
            }
            Tk.TextField {
                objectName: "orderLineDescription"
                Tk.Flex.flex: 3
                small: true
                readOnly: table.readOnly
                text: String(line.lineData.description || "")
                placeholderText: qsTr("Description")
                onEditingFinished: if (text !== String(line.lineData.description || "")) table.update(line.index, "description", text)
            }
            Tk.ComboBox {
                objectName: "orderLineCustomer"
                Tk.Flex.flex: 2
                small: true
                enabled: !table.readOnly
                textRole: "name"
                model: table.jobs
                currentIndex: Math.max(0, table.jobs.map(function(j) { return j.id }).indexOf(line.lineData.customerId || 0))
                onActivated: function(i) { table.update(line.index, "customerId", table.jobs[i].id) }
            }
            Tk.TextField {
                objectName: "orderLineQuantity"
                implicitWidth: Tk.Theme.size.controlLg * 1.8
                small: true
                mono: true
                readOnly: table.readOnly
                horizontalAlignment: Text.AlignRight
                text: String(line.lineData.quantity || "")
                onEditingFinished: if (text !== String(line.lineData.quantity || "")) table.update(line.index, "quantity", text)
            }
            Tk.TextField {
                objectName: "orderLineRate"
                implicitWidth: Tk.Theme.size.tableColumn
                small: true
                mono: true
                readOnly: table.readOnly
                horizontalAlignment: Text.AlignRight
                placeholderText: "0.00"
                text: String(line.lineData.rate || "")
                onEditingFinished: if (text !== String(line.lineData.rate || "")) table.update(line.index, "rate", text)
            }
            Tk.Mono {
                objectName: "orderLineAmount"
                Tk.Flex.basis: Tk.Theme.size.tableColumn * 1.1; Tk.Flex.shrink: 0
                horizontalAlignment: Text.AlignRight
                text: String(table.amounts[line.index] || "")
            }
            Tk.Caption {
                objectName: "orderLineReceived"
                Tk.Flex.basis: Tk.Theme.size.tableColumn; Tk.Flex.shrink: 0
                horizontalAlignment: Text.AlignRight
                text: line.lineData.received !== undefined ? qsTr("%1 of %2").arg(line.lineData.received).arg(line.lineData.quantity) : "—"
            }
            Tk.CheckBox {
                objectName: "orderLineClosed"
                implicitWidth: Tk.Theme.size.control * 2.2
                enabled: !table.readOnly && Number(line.lineData.id || 0) > 0
                checked: line.lineData.closed === true
                tooltip: qsTr("Closed: nothing more is expected on this line")
                onToggled: {
                    table.closeLine(Number(line.lineData.id), checked)
                    checked = Qt.binding(function() { return line.lineData.closed === true })
                }
            }
            Tk.IconButton {
                small: true
                iconName: "x"
                enabled: !table.readOnly && !line.received
                tooltip: line.received ? qsTr("Received on a bill; close the line instead") : qsTr("Remove this line")
                onClicked: table.remove(line.index)
            }
        }
    }
    Tk.Button {
        objectName: "orderLineAdd"
        visible: !table.readOnly
        small: true
        variant: "ghost"
        iconName: "plus"
        text: qsTr("Add a line")
        onClicked: table.edited(table.lines.concat([table.blank()]))
    }
}
