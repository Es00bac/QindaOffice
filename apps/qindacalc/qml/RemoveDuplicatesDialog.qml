// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// Data ▸ Remove Duplicates (M78, D-087): the columns that decide what a
// duplicate is (Select All / Unselect All), "My data has headers", and
// Excel's report afterwards ("2 duplicate values found and removed; 5
// unique values remain."). One undo step.
Tk.Dialog {
    id: dialog

    required property CalcHost host
    property var state: ({})
    property var picked: []
    property bool hasHeader: false

    signal reported(string message)

    objectName: "removeDuplicatesDialog"
    title: qsTr("Remove Duplicates")
    subtitle: dialog.state.range || ""
    dialogWidth: Tk.Theme.size.dialogWidth
    primaryText: qsTr("OK")
    primaryEnabled: dialog.picked.length > 0

    function load() {
        const first = dialog.host.removeDuplicatesState(true)
        dialog.hasHeader = first.suggestedHeader === true
        dialog.reload()
        dialog.picked = (dialog.state.columns || []).map(function(c) { return c.index })
    }
    function reload() { dialog.state = dialog.host.removeDuplicatesState(dialog.hasHeader) }
    function toggle(column, on) {
        const next = dialog.picked.filter(function(c) { return c !== column })
        if (on) next.push(column)
        dialog.picked = next
    }
    onAccepted: dialog.reported(dialog.host.applyRemoveDuplicates(dialog.picked, dialog.hasHeader))

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Caption { text: qsTr("To delete duplicate values, select one or more columns that contain duplicates.") }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            Tk.Button {
                objectName: "removeDuplicatesSelectAll"; small: true; text: qsTr("Select All")
                onClicked: dialog.picked = (dialog.state.columns || []).map(function(c) { return c.index })
            }
            Tk.Button { objectName: "removeDuplicatesUnselectAll"; small: true; text: qsTr("Unselect All"); onClicked: dialog.picked = [] }
            Tk.Spacer { }
            Tk.CheckBox {
                objectName: "removeDuplicatesHeader"
                text: qsTr("My data has headers")
                checked: dialog.hasHeader
                onToggled: { dialog.hasHeader = checked; dialog.reload() }
            }
        }
        Tk.Scroll {
            implicitHeight: Math.min(Tk.Theme.size.row * 7, columnRows.implicitHeight)
            Tk.Flex {
                id: columnRows
                direction: Tk.Flex.Column
                gap: 0
                Repeater {
                    model: dialog.state.columns || []
                    Tk.CheckBox {
                        required property var modelData
                        objectName: "removeDuplicatesColumn_" + modelData.index
                        text: modelData.name
                        checked: dialog.picked.indexOf(modelData.index) >= 0
                        onToggled: dialog.toggle(modelData.index, checked)
                    }
                }
            }
        }
    }
}
