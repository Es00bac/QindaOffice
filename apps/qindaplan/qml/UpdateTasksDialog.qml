// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Plan.App

// Update Tasks (Project's dialog, M79/D-088): % complete, actual and
// remaining duration, actual start and finish of the selected tasks. The
// fields open on the current task's values; Save applies what changed to
// every selected task as one undo step (the engine keeps Project's rules:
// an actual duration at or past the duration finishes the task, a remaining
// duration resets the duration to actual + remaining).
Tk.Dialog {
    id: dialog

    property PlanService plan: null
    property var info: ({})
    property int count: 0

    objectName: "updateTasksDialog"
    title: qsTr("Update Tasks")
    subtitle: count > 1 ? qsTr("%n selected tasks", "", count) : String(info.name || "")
    primaryText: qsTr("Save")
    secondaryText: qsTr("Cancel")

    function openDialog() {
        if (plan === null || plan.currentTaskId === "") return
        info = plan.taskInfo(plan.currentTaskId)
        count = plan.selectedTaskIds.length
        percentField.text = String(info.percent || "0")
        actualDuration.text = String(info.actualDuration || "")
        remainingDuration.text = String(info.remainingDuration || "")
        actualStart.text = String(info.actualStart || "NA")
        actualFinish.text = String(info.actualFinish || "NA")
        error.visible = false
        open()
    }
    onAccepted: {
        const fields = {}
        const fill = function(key, field) { if (field.text !== String(dialog.info[key] || "")) fields[key] = field.text }
        fill("percent", percentField)
        fill("actualDuration", actualDuration)
        fill("remainingDuration", remainingDuration)
        fill("actualStart", actualStart)
        fill("actualFinish", actualFinish)
        const r = plan.updateTasks(fields)
        if (!r.ok) {
            error.text = String(r.error || "")
            error.visible = true
            open()
        }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { id: error; objectName: "updateTasksError"; visible: false; variant: "danger"; title: qsTr("Not saved") }
        Tk.Grid {
            columns: "1fr 1fr"
            columnGap: Tk.Theme.space.lg
            rowGap: Tk.Theme.space.sm
            Tk.PropertyRow { label: qsTr("% Complete"); Tk.TextField { id: percentField; objectName: "updatePercent"; mono: true } }
            Tk.PropertyRow { label: qsTr("Duration"); Tk.Label { text: String(dialog.info.duration || "") } }
            Tk.PropertyRow { label: qsTr("Actual dur."); Tk.TextField { id: actualDuration; objectName: "updateActualDuration" } }
            Tk.PropertyRow { label: qsTr("Remaining dur."); Tk.TextField { id: remainingDuration; objectName: "updateRemainingDuration" } }
            Tk.PropertyRow { label: qsTr("Actual start"); Tk.TextField { id: actualStart; objectName: "updateActualStart" } }
            Tk.PropertyRow { label: qsTr("Actual finish"); Tk.TextField { id: actualFinish; objectName: "updateActualFinish" } }
        }
        Tk.Caption {
            wrapMode: Text.WordWrap
            text: qsTr("Current: %1 – %2. NA clears an actual date.").arg(String(dialog.info.start || "")).arg(String(dialog.info.finish || ""))
        }
    }
}
