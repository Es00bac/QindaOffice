// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Plan.App

// Insert ▸ Recurring Task and Recurring Task Information (M83, D-092):
// Project's recurring task — a summary whose occurrences, one per date of
// the pattern, are its subtasks, each starting no earlier than its date at
// the time given. A new pattern makes the occurrences again (refused once
// one has progress). A recurring task read from Microsoft Project XML has
// no pattern to show (the format does not carry it).
Tk.Dialog {
    id: dialog

    property PlanService plan: null
    property string taskId: ""
    property var fields: ({})

    objectName: "recurringTaskDialog"
    title: taskId === "" ? qsTr("Recurring Task") : qsTr("Recurring Task Information")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.3
    primaryText: taskId === "" ? qsTr("Insert") : qsTr("Save")
    secondaryText: qsTr("Cancel")
    primaryEnabled: fields.readOnly !== true && nameField.text.trim().length > 0

    function openFor(id) {
        taskId = id
        fields = plan.recurringTaskFields(id)
        nameField.text = String(fields.name || "")
        durationField.text = String(fields.duration || "1 day")
        timeField.text = String(fields.startTime || "08:00")
        pattern.load(fields)
        error.visible = false
        open()
    }
    onAccepted: {
        const out = pattern.collect()
        out.name = nameField.text
        out.duration = durationField.text
        out.startTime = timeField.text
        const r = plan.saveRecurringTask(taskId, out)
        if (!r.ok) {
            error.text = String(r.error || "")
            error.visible = true
            open()
        }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { id: error; objectName: "recurringTaskError"; visible: false; variant: "danger"; title: qsTr("Not saved") }
        Tk.Notice {
            visible: dialog.fields.readOnly === true
            variant: "info"
            text: qsTr("This recurring task came from Microsoft Project XML, which keeps its occurrences but not its pattern.")
        }
        Tk.PropertyRow {
            label: qsTr("Task name")
            Tk.TextField { id: nameField; objectName: "recurringTaskName"; placeholderText: qsTr("e.g. Staff meeting") }
        }
        Tk.Grid {
            columns: "1fr 1fr"
            columnGap: Tk.Theme.space.lg
            Tk.PropertyRow {
                label: qsTr("Duration")
                Tk.TextField { id: durationField; objectName: "recurringTaskDuration"; small: true }
            }
            Tk.PropertyRow {
                label: qsTr("Starts at")
                Tk.TextField { id: timeField; objectName: "recurringTaskTime"; small: true; mono: true }
            }
        }
        Tk.Overline { title: qsTr("RECURRENCE") }
        RecurrencePatternEditor {
            id: pattern
            objectName: "recurringTaskPattern"
            enabled: dialog.fields.readOnly !== true
        }
    }
}
