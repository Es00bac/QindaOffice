// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Query Parameters (Access's PARAMETERS clause, M80 D-089 §3): the prompts
// a query asks for, in the order it asks them, each with its type — a
// declared [Start date] is a date even where it meets a text field. A
// prompt the criteria use but that is not declared is still asked for,
// typed by the field it meets.
Tk.Dialog {
    id: dialog

    property var types: []   // [{id, label}]
    property var rows: []    // [{prompt, type}]
    signal declared(var list)

    title: qsTr("Query parameters")
    subtitle: qsTr("What the query asks for before it runs, in this order.")
    primaryText: qsTr("OK")
    secondaryText: qsTr("Cancel")

    function edit(list) {
        rows = (list || []).map(function(p) { return { prompt: p.prompt || "", type: p.type || "text" } })
        if (rows.length === 0) rows = [{ prompt: "", type: "text" }]
        open()
    }
    function setRow(index, key, value) {
        const next = rows.slice()
        next[index] = Object.assign({}, next[index])
        next[index][key] = value
        rows = next
    }
    onAccepted: declared(rows.filter(function(r) { return r.prompt.trim().length > 0 })
                             .map(function(r) { return { prompt: r.prompt.trim(), type: r.type } }))

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Repeater {
            model: dialog.rows
            delegate: Tk.Flex {
                id: row
                required property var modelData
                required property int index
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                Tk.TextField {
                    objectName: "parameterPrompt_" + row.index
                    text: row.modelData.prompt
                    placeholderText: qsTr("Prompt, e.g. Start date")
                    tooltip: qsTr("What the query asks; criteria use it as [%1]").arg(row.modelData.prompt || qsTr("Prompt"))
                    Tk.Flex.grow: 2
                    Tk.Flex.basis: 0
                    onEditingFinished: if (text !== row.modelData.prompt) dialog.setRow(row.index, "prompt", text)
                }
                Tk.ComboBox {
                    objectName: "parameterType_" + row.index
                    small: true
                    textRole: "label"
                    model: dialog.types
                    currentIndex: Math.max(0, dialog.types.findIndex(function(t) { return t.id === row.modelData.type }))
                    tooltip: qsTr("Data type")
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    onActivated: function(i) { dialog.setRow(row.index, "type", dialog.types[i].id) }
                }
                Tk.IconButton {
                    iconName: "x"
                    small: true
                    tooltip: qsTr("Remove this parameter")
                    onClicked: { const next = dialog.rows.slice(); next.splice(row.index, 1); dialog.rows = next }
                }
            }
        }
        Tk.Button {
            objectName: "parameterAdd"
            text: qsTr("Add parameter")
            iconName: "plus"
            small: true
            onClicked: dialog.rows = dialog.rows.concat([{ prompt: "", type: "text" }])
        }
    }
}
