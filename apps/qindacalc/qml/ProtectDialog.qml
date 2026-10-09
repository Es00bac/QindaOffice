// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// Review ▸ Protect Sheet and Protect Workbook (M66, D-075): an optional
// password, typed twice, and — for a sheet — what everyone may still do
// (Excel's list and defaults). A workbook lock keeps the sheet structure.
// Protection is a guard against accidents, not encryption; the dialog
// says so.
Tk.Dialog {
    id: dialog

    required property CalcHost host
    property bool workbook: false

    objectName: "protectDialog"
    title: workbook ? qsTr("Protect workbook structure") : qsTr("Protect sheet")
    subtitle: qsTr("A password keeps others from removing the protection; it does not encrypt the file.")
    dialogWidth: 440
    primaryText: qsTr("Protect")
    primaryEnabled: password.text === confirm.text

    // label, option key (true = allowed), Excel's default
    readonly property var permissions: [
        [qsTr("Select locked cells"), "selectLockedCells", true],
        [qsTr("Select unlocked cells"), "selectUnlockedCells", true],
        [qsTr("Format cells"), "formatCells", false],
        [qsTr("Format columns"), "formatColumns", false],
        [qsTr("Format rows"), "formatRows", false],
        [qsTr("Insert columns"), "insertColumns", false],
        [qsTr("Insert rows"), "insertRows", false],
        [qsTr("Insert links"), "insertHyperlinks", false],
        [qsTr("Delete columns"), "deleteColumns", false],
        [qsTr("Delete rows"), "deleteRows", false],
        [qsTr("Sort"), "sort", false],
        [qsTr("Use the filter"), "autoFilter", false],
        [qsTr("Edit objects and notes"), "objects", false]
    ]

    onOpened: {
        password.text = ""
        confirm.text = ""
        for (let i = 0; i < allowed.count; ++i) allowed.itemAt(i).checked = dialog.permissions[i][2]
        password.forceActiveFocus(Qt.TabFocusReason)
    }
    onAccepted: {
        if (dialog.workbook) {
            dialog.host.protectWorkbookWith(password.text)
            return
        }
        const options = { password: password.text }
        for (let i = 0; i < allowed.count; ++i) options[dialog.permissions[i][1]] = allowed.itemAt(i).checked
        dialog.host.protectSheetWith(options)
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Password (optional)")
            Tk.TextField { id: password; objectName: "protectPassword"; width: 220; echoMode: TextInput.Password }
        }
        Tk.PropertyRow {
            label: qsTr("Type it again")
            Tk.TextField { id: confirm; objectName: "protectConfirm"; width: 220; echoMode: TextInput.Password; error: confirm.text !== password.text }
        }
        Tk.Caption {
            visible: !dialog.workbook
            text: qsTr("Everyone may still:")
        }
        Tk.Grid {
            visible: !dialog.workbook
            columns: "1fr 1fr"
            columnGap: Tk.Theme.space.md
            rowGap: Tk.Theme.space.xs
            Repeater {
                id: allowed
                model: dialog.permissions
                Tk.CheckBox {
                    required property var modelData
                    required property int index
                    objectName: "protectAllow_" + modelData[1]
                    text: modelData[0]
                    small: true
                }
            }
        }
    }
}
