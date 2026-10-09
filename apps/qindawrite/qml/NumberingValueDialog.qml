// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Home ▸ Numbering ▸ Set Numbering Value (M73, D-082): start a new list at
// a value from the caret's item on (written as a new w:num with Word's
// w:startOverride), or continue the list before it. Answered through
// ListController (write_lists.h).
Tk.Dialog {
    id: dialog
    required property QtObject lists
    property bool canContinue: false
    objectName: "numberingValueDialog"
    title: qsTr("Set Numbering Value")
    dialogWidth: 380
    primaryText: qsTr("OK")

    onAccepted: lists.applyNumberingValue({ mode: continueList.checked ? "continue" : "new", value: valueField.value })

    Connections {
        target: dialog.lists
        function onNumberingValueRequested() {
            const info = dialog.lists.numberingValueInfo()
            dialog.canContinue = info.canContinue
            newList.checked = true
            valueField.value = info.value
            dialog.open()
        }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.RadioButton {
            id: newList
            objectName: "numberingNewList"
            text: qsTr("Start new list")
            onClicked: continueList.checked = false
        }
        Tk.RadioButton {
            id: continueList
            objectName: "numberingContinue"
            text: qsTr("Continue from previous list")
            enabled: dialog.canContinue
            onClicked: newList.checked = false
        }
        Tk.PropertyRow {
            label: qsTr("Set value to")
            Tk.NumberField {
                id: valueField
                objectName: "numberingValue"
                enabled: newList.checked
                from: 0; to: 32767; stepSize: 1; decimals: 0
                onValueModified: function(v) { value = v }
            }
        }
    }
}
