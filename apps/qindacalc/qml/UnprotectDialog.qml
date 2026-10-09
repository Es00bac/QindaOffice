// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// Review ▸ Unprotect Sheet or Unprotect Workbook when a password guards it
// (M66, D-075). A wrong password leaves the protection on; the host says
// so in the status bar.
Tk.Dialog {
    id: dialog

    required property CalcHost host
    property bool workbook: false

    objectName: "unprotectDialog"
    title: workbook ? qsTr("Unprotect workbook") : qsTr("Unprotect sheet")
    dialogWidth: 360
    primaryText: qsTr("Unprotect")
    onOpened: { password.text = ""; password.forceActiveFocus(Qt.TabFocusReason) }
    onAccepted: {
        if (dialog.workbook) dialog.host.unprotectWorkbookWith(password.text)
        else dialog.host.unprotectSheetWith(password.text)
    }

    Tk.PropertyRow {
        label: qsTr("Password")
        Tk.TextField {
            id: password; objectName: "unprotectPassword"; width: 200; echoMode: TextInput.Password
            onAccepted: dialog.accept()
        }
    }
}
