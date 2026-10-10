// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Plan.App

// Status Date and Update Project: pick the date progress is measured
// against (a red line on the Gantt). Update Project either marks the work
// that should be done by then as complete, task by task, or (M83, D-092,
// Project's second choice) reschedules the uncompleted work to start after
// the date — an in-progress task splits there, one that should have
// started gets Start No Earlier Than the date.
Tk.Dialog {
    id: dialog

    property PlanService plan: null
    property bool updating: false
    property bool rescheduling: false
    property string chosen: ""

    objectName: "statusDateDialog"
    title: updating ? qsTr("Update Project") : qsTr("Status Date")
    primaryText: updating ? qsTr("Update") : qsTr("Set")
    secondaryText: qsTr("Cancel")
    tertiaryText: updating ? "" : qsTr("Clear")
    primaryEnabled: dateField.text.trim().length > 0

    function openReschedule() {
        openFor(true)
        rescheduling = true
    }
    function openFor(update) {
        updating = update
        rescheduling = false
        const current = String(plan.projectInfo().statusDate || "")
        dateField.text = current.length > 0 ? current : plan.today
        picker.selectedDate = plan.today
        picker.showDate(plan.today)
        open()
    }
    onAccepted: {
        const r = !updating ? plan.setStatusDate(dateField.text)
                : rescheduling ? plan.rescheduleUncompletedWork(dateField.text) : plan.updateProject(dateField.text)
        if (!r.ok) plan.notify(title, String(r.error || ""), "warning")
    }
    onTertiary: plan.setStatusDate("NA")

    readonly property Tk.LocaleService localeService: Tk.LocaleService { }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        // Project's two choices, as its Update Project dialog has them.
        Tk.RadioButton {
            objectName: "statusDateUpdate"
            visible: dialog.updating
            text: qsTr("Update work as complete through")
            checked: !dialog.rescheduling
            onToggled: if (checked) dialog.rescheduling = false
        }
        Tk.RadioButton {
            objectName: "statusDateReschedule"
            visible: dialog.updating
            text: qsTr("Reschedule uncompleted work to start after")
            checked: dialog.rescheduling
            onToggled: if (checked) dialog.rescheduling = true
        }
        Tk.CalendarPicker {
            id: picker
            objectName: "statusDatePicker"
            localeService: dialog.localeService
            tooltip: qsTr("Status date")
            onDateSelected: function(iso) { dateField.text = iso }
        }
        Tk.PropertyRow {
            label: qsTr("Date")
            Tk.TextField { id: dateField; objectName: "statusDateField" }
        }
        Tk.Caption {
            wrapMode: Text.WordWrap
            text: !dialog.updating ? qsTr("Late tasks and the reports are measured against this date instead of today.")
                  : dialog.rescheduling ? qsTr("Work not done by this date moves after it: a task under way is split where its progress ends; one that should have started starts no earlier than the date.")
                  : qsTr("Tasks that should have finished by this date are marked complete; tasks under way get the share of their duration that has passed.")
        }
    }
}
