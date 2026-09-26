// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Plan.App

// Project Information: title, manager, company, start date, status date,
// the project calendar, currency and hours per day. The finish is the
// scheduler's and shown read-only. One undo step.
Tk.Dialog {
    id: dialog

    property PlanService plan: null
    property var info: ({})
    property var calendarNames: []

    objectName: "projectInfoDialog"
    title: qsTr("Project Information")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.3
    primaryText: qsTr("Save")
    secondaryText: qsTr("Cancel")

    function openDialog() {
        info = plan.projectInfo()
        calendarNames = plan.calendars().map(function(c) { return c.name })
        titleField.text = String(info.title || "")
        managerField.text = String(info.manager || "")
        companyField.text = String(info.company || "")
        startField.text = String(info.start || "")
        statusField.text = String(info.statusDate || "")
        currencyField.text = String(info.currency || "")
        hoursField.text = String(info.hoursPerDay || "")
        calendarBox.currentIndex = Math.max(0, calendarNames.indexOf(String(info.calendar || "")))
        notesField.text = String(info.notes || "")
        error.visible = false
        open()
    }
    onAccepted: {
        const fields = { title: titleField.text, manager: managerField.text, company: companyField.text,
                         notes: notesField.text, currency: currencyField.text, calendar: calendarBox.currentText }
        if (startField.text !== String(info.start || "")) fields.start = startField.text
        if (statusField.text !== String(info.statusDate || "")) fields.statusDate = statusField.text
        if (hoursField.text !== String(info.hoursPerDay || "")) fields.hoursPerDay = hoursField.text
        const r = plan.setProjectInfo(fields)
        if (!r.ok) {
            error.text = String(r.error || "")
            error.visible = true
            open()
        }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Notice { id: error; visible: false; variant: "danger"; title: qsTr("Not saved") }
        Tk.PropertyRow { label: qsTr("Title"); Tk.TextField { id: titleField; objectName: "projectTitle" } }
        Tk.PropertyRow { label: qsTr("Manager"); Tk.TextField { id: managerField; objectName: "projectManager" } }
        Tk.PropertyRow { label: qsTr("Company"); Tk.TextField { id: companyField; objectName: "projectCompany" } }
        Tk.Grid {
            columns: "1fr 1fr"
            columnGap: Tk.Theme.space.lg
            rowGap: Tk.Theme.space.sm
            Tk.PropertyRow { label: qsTr("Start date"); Tk.TextField { id: startField; objectName: "projectStart" } }
            Tk.PropertyRow { label: qsTr("Finish date"); Tk.Label { text: String(dialog.info.finish || ""); Tk.Flex.basis: Tk.Theme.size.fieldWidth; Tk.Flex.shrink: 0 } }
            Tk.PropertyRow { label: qsTr("Status date"); Tk.TextField { id: statusField; objectName: "projectStatusDate"; placeholderText: qsTr("NA") } }
            Tk.PropertyRow {
                label: qsTr("Calendar")
                Tk.ComboBox { id: calendarBox; objectName: "projectCalendar"; small: true; model: dialog.calendarNames }
            }
            Tk.PropertyRow { label: qsTr("Currency"); Tk.TextField { id: currencyField; objectName: "projectCurrency"; mono: true } }
            Tk.PropertyRow { label: qsTr("Hours per day"); Tk.TextField { id: hoursField; objectName: "projectHoursPerDay" } }
        }
        Tk.TextArea { id: notesField; rows: 3; placeholderText: qsTr("Notes about the project") }
    }
}
