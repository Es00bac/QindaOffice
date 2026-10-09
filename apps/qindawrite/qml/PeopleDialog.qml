// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Mailings ▸ Select Recipients ▸ Choose from QindaMail People (M77, D-086):
// every contact, or the contacts of one group (a category), read from
// QindaMail's address book without changing it (write_merge_sources.h).
Tk.Dialog {
    id: dialog
    required property QtObject mailMerge
    property var groups: []
    property int chosen: 0
    objectName: "peopleDialog"
    title: qsTr("Choose from QindaMail People")
    dialogWidth: 420
    primaryText: qsTr("OK")
    primaryEnabled: groups.length > 0

    Connections {
        target: dialog.mailMerge
        function onPeopleRequested() {
            dialog.groups = dialog.mailMerge.peopleGroups()
            dialog.chosen = 0
            dialog.open()
        }
    }
    onAccepted: if (groups.length > 0) dialog.mailMerge.usePeople(groups[chosen].group)

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.Caption {
            text: dialog.groups.length > 0 ? qsTr("Merge to these contacts:")
                                           : qsTr("QindaMail has no contacts yet: add them in QindaMail's People.")
        }
        Repeater {
            model: dialog.groups
            delegate: Tk.RadioButton {
                required property var modelData
                required property int index
                objectName: "peopleGroup" + index
                text: qsTr("%1 (%2)").arg(modelData.label).arg(modelData.count)
                checked: dialog.chosen === index
                onClicked: dialog.chosen = index
            }
        }
    }
}
