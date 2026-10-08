// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Plan.App

// Task Information (Shift+F2): the task's fields on four pages — General
// (name, duration, dates, progress, mode), Predecessors, Advanced
// (constraint, deadline, priority, calendar, fixed cost, active, milestone)
// and Notes. Save
// sends only what changed, as one undo step; the engine checks every value
// and a refusal keeps the dialog open with the reason.
Tk.Dialog {
    id: dialog

    property PlanService plan: null
    property string taskId: ""
    property var info: ({})

    objectName: "taskInfoDialog"
    title: info.summary === true ? qsTr("Summary Task Information") : qsTr("Task Information")
    subtitle: String(info.id || "") + " · " + String(info.name || "")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.45
    primaryText: qsTr("Save")
    secondaryText: qsTr("Cancel")

    function openFor(id, page) {
        if (plan === null || id === "") return
        taskId = id
        info = plan.taskInfo(id)
        general.load(info)
        predecessorsField.text = String(info.predecessors || "")
        constraintBox.currentIndex = Math.max(0, constraintBox.codes.indexOf(String(info.constraintCode || "ASAP")))
        constraintDate.text = String(info.constraintDate || "") === "NA" ? "" : String(info.constraintDate || "")
        calendarBox.model = info.calendars || []
        calendarBox.currentIndex = Math.max(0, (info.calendars || []).indexOf(String(info.calendar || "None")))
        fixedCost.text = String(info.fixedCost || "")
        deadlineField.text = String(info.deadline || "") === "NA" ? "" : String(info.deadline || "")
        priorityField.text = String(info.priority || "500")
        activeSwitch.checked = info.activeFlag !== false
        milestoneSwitch.checked = info.milestoneFlag === true
        notesField.text = String(info.notes || "")
        pages.currentIndex = page === "notes" ? 3 : page === "predecessors" ? 1 : page === "advanced" ? 2 : 0
        error.visible = false
        open()
    }
    function collect() {
        const out = general.collect()
        out.predecessors = predecessorsField.text
        out.constraint = constraintBox.codes[constraintBox.currentIndex]
        if (constraintBox.codes[constraintBox.currentIndex] !== "ASAP" && constraintBox.codes[constraintBox.currentIndex] !== "ALAP")
            out.constraintDate = constraintDate.text.trim().length > 0 ? constraintDate.text : String(info.start || "")
        out.calendar = calendarBox.currentText
        out.fixedCost = fixedCost.text
        out.deadline = deadlineField.text.trim().length > 0 ? deadlineField.text : "NA"
        out.priority = priorityField.text
        out.active = activeSwitch.checked ? "Yes" : "No"
        out.milestone = milestoneSwitch.checked ? "Yes" : "No"
        out.notes = notesField.text
        return out
    }
    onAccepted: {
        const r = plan.saveTaskInfo(taskId, collect())
        if (!r.ok) {
            error.text = String(r.error || "")
            error.visible = true
            open()
        }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { id: error; objectName: "taskInfoError"; visible: false; variant: "danger"; title: qsTr("Not saved") }
        Tk.Notice {
            visible: String(dialog.info.warning || "").length > 0
            variant: "warning"
            title: qsTr("Scheduling")
            text: String(dialog.info.warning || "")
        }
        Tk.TabStrip {
            id: pages
            objectName: "taskInfoPages"
            model: [qsTr("General"), qsTr("Predecessors"), qsTr("Advanced"), qsTr("Notes")]
            onTabActivated: function(index) { pages.currentIndex = index }
        }
        TaskInfoGeneral {
            id: general
            visible: pages.currentIndex === 0
        }
        Tk.Flex {
            visible: pages.currentIndex === 1
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.sm
            Tk.PropertyRow {
                label: qsTr("Predecessors")
                hint: qsTr("Task IDs with an optional type and lag: 3, 4SS, 5FS+2d, 6FF-1d, 7FS+50%")
                Tk.TextField { id: predecessorsField; objectName: "taskInfoPredecessors"; mono: true }
            }
            Repeater {
                model: dialog.info.predecessorList || []
                delegate: Tk.KeyValue {
                    required property var modelData
                    key: String(modelData.id) + "  " + String(modelData.name)
                    value: String(modelData.type) + (String(modelData.lag).length > 0 ? " " + String(modelData.lag) : "")
                }
            }
        }
        Tk.Flex {
            visible: pages.currentIndex === 2
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.sm
            Tk.PropertyRow {
                label: qsTr("Constraint")
                Tk.ComboBox {
                    id: constraintBox
                    objectName: "taskInfoConstraint"
                    small: true
                    readonly property var codes: ["ASAP", "ALAP", "MSO", "MFO", "SNET", "SNLT", "FNET", "FNLT"]
                    model: [qsTr("As Soon As Possible"), qsTr("As Late As Possible"), qsTr("Must Start On"),
                            qsTr("Must Finish On"), qsTr("Start No Earlier Than"), qsTr("Start No Later Than"),
                            qsTr("Finish No Earlier Than"), qsTr("Finish No Later Than")]
                }
            }
            Tk.PropertyRow {
                label: qsTr("Constraint date")
                enabled: constraintBox.currentIndex > 1
                Tk.TextField { id: constraintDate; objectName: "taskInfoConstraintDate"; placeholderText: qsTr("e.g. 16 Mar 2026") }
            }
            // M63: Project's Deadline (flags a later finish, moves nothing)
            // and Priority (0–1000), kept through Microsoft Project XML.
            Tk.PropertyRow {
                label: qsTr("Deadline")
                Tk.TextField { id: deadlineField; objectName: "taskInfoDeadline"; placeholderText: qsTr("none") }
            }
            Tk.PropertyRow {
                label: qsTr("Priority")
                Tk.TextField { id: priorityField; objectName: "taskInfoPriority"; mono: true; placeholderText: "500" }
            }
            Tk.PropertyRow {
                label: qsTr("Calendar")
                Tk.ComboBox { id: calendarBox; objectName: "taskInfoCalendar"; small: true }
            }
            Tk.PropertyRow {
                label: qsTr("Fixed cost")
                Tk.TextField { id: fixedCost; objectName: "taskInfoFixedCost" }
            }
            Tk.Switch { id: activeSwitch; objectName: "taskInfoActive"; text: qsTr("Active (inactive tasks stay in the plan but drive nothing)") }
            Tk.Switch { id: milestoneSwitch; objectName: "taskInfoMilestone"; text: qsTr("Mark as milestone") }
        }
        Tk.TextArea {
            id: notesField
            objectName: "taskInfoNotes"
            visible: pages.currentIndex === 3
            rows: 8
            placeholderText: qsTr("Notes for this task")
        }
    }
}
