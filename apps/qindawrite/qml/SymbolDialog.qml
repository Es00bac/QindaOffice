// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Insert ▸ Special Character: the characters a document needs that no
// keyboard has a key for, as a grid of buttons; a click inserts.
Tk.Dialog {
    id: dialog
    property var symbols: []
    signal symbolPicked(string text)
    objectName: "symbolDialog"
    title: qsTr("Special Character")
    dialogWidth: 420
    primaryText: ""
    secondaryText: qsTr("Close")
    Grid {
        columns: 9
        spacing: Tk.Theme.space.xs
        Repeater {
            model: dialog.symbols
            delegate: Tk.Button {
                required property string modelData
                required property int index
                objectName: "symbol_" + index
                text: modelData
                variant: "ghost"
                implicitWidth: 36
                onClicked: { dialog.symbolPicked(modelData); dialog.close() }
            }
        }
    }
}
