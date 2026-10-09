// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Mailings ▸ Find Recipient (M77, D-086): the next recipient whose field
// (or any field) holds the text; Preview Results moves to it.
Tk.Dialog {
    id: dialog
    required property QtObject mailMerge
    property string status: ""
    objectName: "findRecipientDialog"
    title: qsTr("Find Entry")
    dialogWidth: 420
    primaryText: qsTr("Find Next")
    secondaryText: qsTr("Close")

    Connections {
        target: dialog.mailMerge
        function onFindRequested() { dialog.status = ""; dialog.open() }
    }
    onAccepted: {
        const fields = dialog.mailMerge.fields
        const field = allFields.checked ? "" : fields[field_.currentIndex]
        const found = dialog.mailMerge.findRecipient(text.text, field, dialog.mailMerge.previewRecord - 1)
        if (found >= 0) {
            dialog.mailMerge.previewRecord = found + 1
            status = qsTr("Record %1").arg(found + 1)
        } else {
            status = qsTr("No recipient holds “%1”.").arg(text.text)
        }
        open()
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow { label: qsTr("Find"); Tk.TextField { id: text; objectName: "findRecipientText" } }
        Tk.RadioButton { id: allFields; objectName: "findRecipientAll"; text: qsTr("All fields"); checked: true; onClicked: oneField.checked = false }
        Tk.Flex {
            gap: Tk.Theme.space.xs
            Tk.RadioButton { id: oneField; objectName: "findRecipientThis"; text: qsTr("This field:"); onClicked: allFields.checked = false }
            Tk.ComboBox { id: field_; objectName: "findRecipientField"; implicitWidth: 180; enabled: oneField.checked; model: dialog.mailMerge.fields }
        }
        Tk.Caption { objectName: "findRecipientStatus"; text: dialog.status }
    }
}
