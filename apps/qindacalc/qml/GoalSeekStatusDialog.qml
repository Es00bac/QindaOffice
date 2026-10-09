// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Calc

// Goal Seek Status (M78, D-087): "Goal Seeking with Cell B3 found a
// solution." with the target and current values, the changing cell
// already holding the value found. OK keeps it (one undo step); Cancel
// puts the workbook back as it was.
Tk.Dialog {
    id: dialog

    required property CalcHost host
    property var outcome: ({})

    objectName: "goalSeekStatusDialog"
    title: qsTr("Goal Seek Status")
    dialogWidth: Tk.Theme.size.dialogWidth
    primaryText: qsTr("OK")
    secondaryText: qsTr("Cancel")

    onAccepted: dialog.host.finishGoalSeek(true)
    onRejected: dialog.host.finishGoalSeek(false)

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Label { objectName: "goalSeekMessage"; text: dialog.outcome.message || ""; wrapMode: Text.WordWrap }
        Tk.KeyValue { objectName: "goalSeekTargetValue"; key: qsTr("Target value"); value: dialog.outcome.target || "" }
        Tk.KeyValue { objectName: "goalSeekCurrentValue"; key: qsTr("Current value"); value: dialog.outcome.current || "" }
        Tk.Caption {
            muted: true
            text: qsTr("%n iteration(s)", "", dialog.outcome.iterations || 0)
        }
    }
}
