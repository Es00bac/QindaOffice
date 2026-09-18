// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Row height / column width for the selection, in pixels.
Tk.Dialog {
    id: dialog

    property bool forRows: true
    property alias value: field.value
    signal valueApplied(real value)

    objectName: "sizeDialog"
    title: forRows ? qsTr("Row height") : qsTr("Column width")
    dialogWidth: 320
    primaryText: qsTr("Apply")
    onAccepted: dialog.valueApplied(field.value)
    onOpened: field.forceActiveFocus(Qt.TabFocusReason)

    Tk.PropertyRow {
        label: forRows ? qsTr("Height") : qsTr("Width")
        Tk.NumberField { id: field; objectName: "sizeValue"; from: 8; to: 2000; decimals: 0; suffix: " px" }
    }
}
