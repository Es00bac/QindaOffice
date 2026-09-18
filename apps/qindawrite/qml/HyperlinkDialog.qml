// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Insert ▸ Hyperlink: the address, and the text to show unless a
// selection already provides it.
Tk.Dialog {
    id: dialog
    property string selected: ""
    signal linkApplied(string url, string label)
    objectName: "hyperlinkDialog"
    title: qsTr("Hyperlink")
    dialogWidth: 420
    primaryText: qsTr("Insert")
    primaryEnabled: urlField.text.trim().length > 0 && urlField.text.trim() !== "https://"
    onAccepted: dialog.linkApplied(urlField.text, labelField.text)
    onOpened: { urlField.text = "https://"; labelField.text = ""; urlField.forceActiveFocus(Qt.TabFocusReason); urlField.selectAll() }
    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow { label: qsTr("Address"); Tk.TextField { id: urlField; objectName: "hyperlinkUrl"; width: 260 } }
        Tk.PropertyRow {
            visible: dialog.selected.length === 0
            label: qsTr("Text to show")
            Tk.TextField { id: labelField; objectName: "hyperlinkLabel"; width: 260; placeholderText: qsTr("The address itself") }
        }
        Tk.Caption { visible: dialog.selected.length > 0; text: qsTr("Links the selected text “%1”").arg(dialog.selected) }
    }
}
