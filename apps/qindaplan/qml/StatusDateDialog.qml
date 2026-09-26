// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Plan.App

// Status Date and Update Project: pick the date progress is measured
// against (a red line on the Gantt). Update Project also marks the work
// that should be done by then as complete, task by task.
Tk.Dialog {
    id: dialog

    property PlanService plan: null
    property bool updating: false
    property string chosen: ""

    objectName: "statusDateDialog"
    title: updating ? qsTr("Update Project") : qsTr("Status Date")
    primaryText: updating ? qsTr("Update") : qsTr("Set")
    secondaryText: qsTr("Cancel")
    tertiaryText: updating ? "" : qsTr("Clear")
    primaryEnabled: dateField.text.trim().length > 0

    function openFor(update) {
        updating = update
        const current = String(plan.projectInfo().statusDate || "")
        dateField.text = current.length > 0 ? current : plan.today
        picker.selectedDate = plan.today
        picker.showDate(plan.today)
        open()
    }
    onAccepted: {
        const r = updating ? plan.updateProject(dateField.text) : plan.setStatusDate(dateField.text)
        if (!r.ok) plan.notify(title, String(r.error || ""), "warning")
    }
    onTertiary: plan.setStatusDate("NA")

    readonly property Tk.LocaleService localeService: Tk.LocaleService { }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
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
            text: dialog.updating ? qsTr("Tasks that should have finished by this date are marked complete; tasks under way get the share of their duration that has passed.")
                                  : qsTr("Late tasks and the reports are measured against this date instead of today.")
        }
    }
}
