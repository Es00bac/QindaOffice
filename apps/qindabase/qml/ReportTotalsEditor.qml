// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// A list of report totals — a function over a field each — for a group's
// footer or the report's (ReportGroupsPanel.qml). Every footer prints the
// record count first, so Count here counts a field's filled values. The
// whole list goes back as `edited(totals)`.
Tk.Flex {
    id: editor

    property var totals: []    // [{field, function}]
    property var fields: []
    property var functions: [] // [{id, label}]
    property string objectPrefix: "reportTotal"
    signal edited(var totals)

    function changed(index, patch) {
        const next = JSON.parse(JSON.stringify(totals))
        Object.assign(next[index], patch)
        edited(next)
    }

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.xs

    Repeater {
        model: editor.totals
        delegate: Tk.Flex {
            id: total
            required property var modelData
            required property int index
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Icon { name: "sigma"; size: Tk.Theme.size.iconSm; color: Tk.Theme.color.textMuted }
            Tk.ComboBox {
                objectName: editor.objectPrefix + "Function_" + total.index
                small: true
                textRole: "label"
                model: editor.functions
                currentIndex: Math.max(0, editor.functions.findIndex(function(f) { return f.id === total.modelData.function }))
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                onActivated: function(i) { editor.changed(total.index, { function: editor.functions[i].id }) }
            }
            Tk.Caption { text: qsTr("of"); Tk.Flex.shrink: 0 }
            Tk.ComboBox {
                objectName: editor.objectPrefix + "Field_" + total.index
                small: true
                model: editor.fields
                currentIndex: model.indexOf(total.modelData.field)
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                onActivated: function(i) { editor.changed(total.index, { field: editor.fields[i] }) }
            }
            Tk.IconButton {
                iconName: "x"; small: true; tooltip: qsTr("Remove this total")
                onClicked: { const next = editor.totals.slice(); next.splice(total.index, 1); editor.edited(next) }
            }
        }
    }
    Tk.Button {
        objectName: editor.objectPrefix + "Add"
        text: qsTr("Add a total")
        iconName: "sigma"
        small: true
        enabled: editor.fields.length > 0
        onClicked: editor.edited(editor.totals.concat([{ field: editor.fields[editor.fields.length - 1], function: "sum" }]))
    }
}
