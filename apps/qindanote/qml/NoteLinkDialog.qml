// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Insert ▸ Link (Ctrl+K; M84, D-093): the address, and the text to show
// unless the selection already provides it. A link that is there already
// opens filled in, and an empty address takes it off.
Tk.Dialog {
    id: dialog
    property string selected: ""
    property string address: ""
    signal linkApplied(string url, string label)
    objectName: "noteLinkDialog"
    title: qsTr("Link")
    dialogWidth: 440
    primaryText: dialog.address.length > 0 ? qsTr("Apply") : qsTr("Insert")
    primaryEnabled: urlField.text.trim().length > 0 && urlField.text.trim() !== "https://"
    onAccepted: dialog.linkApplied(urlField.text.trim(), labelField.text)
    onOpened: {
        urlField.text = dialog.address.length > 0 ? dialog.address : "https://"
        labelField.text = ""
        urlField.forceActiveFocus(Qt.TabFocusReason)
        urlField.selectAll()
    }
    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow { label: qsTr("Address"); Tk.TextField { id: urlField; objectName: "noteLinkUrl"; width: 280 } }
        Tk.PropertyRow {
            visible: dialog.selected.length === 0
            label: qsTr("Text to show")
            Tk.TextField { id: labelField; objectName: "noteLinkLabel"; width: 280; placeholderText: qsTr("The address itself") }
        }
        Tk.Caption { visible: dialog.selected.length > 0; text: qsTr("Links the selected text “%1”").arg(dialog.selected) }
        Tk.Caption { text: qsTr("A web address, an e-mail address (mailto:) or a document on this computer.") }
    }
}
