// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Edit Recipient List ▸ Find Duplicates (M77, D-086): records whose every
// value is the same, grouped; clear the copies to leave them out of the
// merge (Word keeps the first of each group checked).
Tk.Dialog {
    id: dialog
    required property QtObject mailMerge
    property var groups: []
    objectName: "duplicatesDialog"
    title: qsTr("Find Duplicates")
    dialogWidth: 560
    primaryText: qsTr("OK")
    secondaryText: ""

    function load() {
        groups = dialog.mailMerge.duplicates()
        open()
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Caption {
            text: dialog.groups.length === 0 ? qsTr("No duplicate records were found.")
                                             : qsTr("Clear the records you do not want in the merge.")
        }
        Repeater {
            model: dialog.groups
            delegate: Tk.Flex {
                id: group
                required property var modelData
                required property int index
                direction: Tk.Flex.Column
                gap: 0
                Tk.Overline { text: qsTr("Group %1").arg(group.index + 1) }
                Repeater {
                    model: group.modelData
                    delegate: Tk.CheckBox {
                        required property var modelData
                        required property int index
                        objectName: "duplicate_" + modelData.index
                        text: modelData.values.join(", ")
                        checked: modelData.included
                        onClicked: dialog.mailMerge.setIncluded(modelData.index, checked)
                    }
                }
            }
        }
    }
}
