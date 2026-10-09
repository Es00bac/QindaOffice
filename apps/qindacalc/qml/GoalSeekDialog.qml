// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// Data ▸ What-If Analysis ▸ Goal Seek (M78, D-087): Set cell, To value, By
// changing cell — Excel's three fields. OK runs the search on the live
// workbook and hands over to Goal Seek Status (GoalSeekStatusDialog.qml);
// a refusal ("Cell must contain a formula.") reopens this dialog with it.
Tk.Dialog {
    id: dialog

    required property CalcHost host
    property string error: ""

    signal finished(var result)

    // A refusal shows its reason in the dialog again, once the closing
    // transition has run (open() during it would be lost).
    property bool reopen: false
    onClosed: if (dialog.reopen) {
        dialog.reopen = false
        dialog.open()
    }

    objectName: "goalSeekDialog"
    title: qsTr("Goal Seek")
    dialogWidth: Tk.Theme.size.dialogWidth
    primaryText: qsTr("OK")
    primaryEnabled: setCell.text.trim().length > 0 && target.text.trim().length > 0 && changing.text.trim().length > 0

    function load() {
        const state = dialog.host.goalSeekState()
        setCell.text = state.setCell || ""
        target.text = ""
        changing.text = state.changingCell || ""
        dialog.error = ""
    }
    onOpened: target.forceActiveFocus(Qt.TabFocusReason)
    onAccepted: {
        const value = Number(target.text.trim())
        if (!isFinite(value)) {
            dialog.error = qsTr("To value must be a number.")
            dialog.reopen = true
            return
        }
        const result = dialog.host.runGoalSeek(setCell.text, value, changing.text)
        if (!result.ok) {
            dialog.error = result.error
            dialog.reopen = true
            return
        }
        dialog.finished(result)
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { objectName: "goalSeekError"; visible: dialog.error.length > 0; variant: "danger"; text: dialog.error }
        Tk.PropertyRow {
            label: qsTr("Set cell")
            Tk.TextField { id: setCell; objectName: "goalSeekSetCell"; mono: true }
        }
        Tk.PropertyRow {
            label: qsTr("To value")
            Tk.TextField { id: target; objectName: "goalSeekTarget"; mono: true }
        }
        Tk.PropertyRow {
            label: qsTr("By changing cell")
            Tk.TextField { id: changing; objectName: "goalSeekChangingCell"; mono: true }
        }
    }
}
