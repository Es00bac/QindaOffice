// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Plan.App

// Split Task (M83, D-092): the current task's work stops on one date and
// resumes on a later one — Project's split, a gap of working time that
// moves with the task. The gap is drawn on paper and in PDF (QindaTK's
// GanttView has no split bars yet); Remove Splits closes them up.
Tk.Dialog {
    id: dialog

    property PlanService plan: null

    objectName: "splitTaskDialog"
    title: qsTr("Split Task")
    subtitle: plan !== null && plan.currentTask.name !== undefined ? String(plan.currentTask.name) : ""
    primaryText: qsTr("Split")
    secondaryText: qsTr("Cancel")
    primaryEnabled: stopField.text.trim().length > 0 && resumeField.text.trim().length > 0

    function openDialog() {
        const id = plan.currentTaskId
        stopField.text = plan.taskFieldText(id, "start")
        resumeField.text = ""
        error.visible = false
        open()
    }
    onAccepted: {
        const r = plan.splitCurrentTask(stopField.text, resumeField.text)
        if (!r.ok) {
            error.text = String(r.error || "")
            error.visible = true
            open()
        }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { id: error; objectName: "splitTaskError"; visible: false; variant: "danger"; title: qsTr("Not split") }
        Tk.PropertyRow {
            label: qsTr("Work stops after")
            hint: qsTr("A date (the end of that working day) or a date and time")
            Tk.TextField { id: stopField; objectName: "splitTaskStop" }
        }
        Tk.PropertyRow {
            label: qsTr("And resumes on")
            hint: qsTr("A date (the start of that working day) or a date and time")
            Tk.TextField { id: resumeField; objectName: "splitTaskResume" }
        }
    }
}
