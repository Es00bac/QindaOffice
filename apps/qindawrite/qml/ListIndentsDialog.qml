// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Adjust List Indents (M73, D-082): where the caret's list level puts its
// number and its text, what follows the number (tab, space, nothing) and an
// optional tab stop after it — for every item of that level in the list,
// as in Word. Answered through ListController (write_lists.h); inches here,
// document px to the host.
Tk.Dialog {
    id: dialog
    required property QtObject lists
    readonly property var suffixes: ["tab", "space", "nothing"]
    objectName: "listIndentsDialog"
    title: qsTr("Adjust List Indents")
    dialogWidth: 400
    primaryText: qsTr("OK")

    onAccepted: lists.applyIndents({ numberPosition: numberField.value * 96, textIndent: textField.value * 96,
                                    suffix: suffixes[follow.currentIndex], tab: addTab.checked ? tabField.value * 96 : -1 })

    Connections {
        target: dialog.lists
        function onListIndentsRequested() {
            const info = dialog.lists.indentsInfo()
            if (info.numberPosition === undefined) return
            numberField.value = info.numberPosition / 96
            textField.value = info.textIndent / 96
            follow.currentIndex = Math.max(0, dialog.suffixes.indexOf(info.suffix))
            addTab.checked = info.tab >= 0
            tabField.value = info.tab >= 0 ? info.tab / 96 : info.textIndent / 96
            dialog.open()
        }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Number position")
            Tk.NumberField {
                id: numberField
                objectName: "listNumberPosition"
                from: -10; to: 22; stepSize: 0.05; decimals: 2; suffix: qsTr(" in")
                onValueModified: function(v) { value = v }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Text indent")
            Tk.NumberField {
                id: textField
                objectName: "listTextIndent"
                from: -10; to: 22; stepSize: 0.05; decimals: 2; suffix: qsTr(" in")
                onValueModified: function(v) { value = v }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Follow number with")
            Tk.ComboBox {
                id: follow
                objectName: "listFollow"
                model: [qsTr("Tab character"), qsTr("Space"), qsTr("Nothing")]
            }
        }
        Tk.CheckBox {
            id: addTab
            objectName: "listAddTab"
            text: qsTr("Add tab stop at")
            enabled: follow.currentIndex === 0
        }
        Tk.PropertyRow {
            label: qsTr("Tab stop")
            Tk.NumberField {
                id: tabField
                objectName: "listTabStop"
                enabled: addTab.checked && follow.currentIndex === 0
                from: 0; to: 22; stepSize: 0.05; decimals: 2; suffix: qsTr(" in")
                onValueModified: function(v) { value = v }
            }
        }
    }
}
