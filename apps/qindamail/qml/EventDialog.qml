// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Mail.App

// Create or edit one event (D-042): title, where, when (dates from a month
// picker, times on a 15-minute list, or all day), which calendar, how it
// repeats, notes. A repeating occurrence asks whether the edit is for this
// one or the whole series. Read-only calendars (feeds) open for viewing.
Tk.Dialog {
    id: dialog

    property MailService service: null
    property string eventId: ""
    property string occurrenceId: ""
    property bool readOnlyEvent: false
    property bool repeating: false
    readonly property var repeatChoices: [
        { text: qsTr("Does not repeat"), rule: null },
        { text: qsTr("Every day"), rule: { frequency: "daily", interval: 1 } },
        { text: qsTr("Every weekday"), rule: { frequency: "weekly", interval: 1, weekdays: [1, 2, 3, 4, 5] } },
        { text: qsTr("Every week"), rule: { frequency: "weekly", interval: 1 } },
        { text: qsTr("Every month"), rule: { frequency: "monthly", interval: 1 } },
        { text: qsTr("Every year"), rule: { frequency: "yearly", interval: 1 } }]
    readonly property var times: {
        const out = []
        for (let m = 0; m < 24 * 60; m += 15) out.push(String(Math.floor(m / 60)).padStart(2, "0") + ":" + String(m % 60).padStart(2, "0"))
        return out
    }
    readonly property var calendarChoices: service !== null ? service.writableCalendars : []

    objectName: "eventDialog"
    title: eventId.length > 0 ? (readOnlyEvent ? qsTr("Event") : qsTr("Edit event")) : qsTr("New event")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.35
    primaryText: readOnlyEvent ? qsTr("Close") : qsTr("Save")
    secondaryText: readOnlyEvent ? "" : qsTr("Cancel")
    tertiaryText: eventId.length > 0 && !readOnlyEvent ? qsTr("Delete") : ""
    destructive: false
    primaryEnabled: readOnlyEvent || titleField.text.trim().length > 0

    function pad(n) { return String(n).padStart(2, "0") }
    function isoDate(d) { return d.getFullYear() + "-" + pad(d.getMonth() + 1) + "-" + pad(d.getDate()) }
    function roundedNow() {
        const d = new Date(); d.setMinutes(Math.ceil(d.getMinutes() / 30) * 30, 0, 0); return d
    }
    function load(fields) {
        titleField.text = String(fields.title || "")
        locationField.text = String(fields.location || "")
        notesField.text = String(fields.description || "")
        const start = String(fields.startLocal || ""), end = String(fields.endLocal || "")
        allDaySwitch.checked = !!fields.allDay || start.length === 10
        startDate.text = start.slice(0, 10)
        endDate.text = end.slice(0, 10)
        startTime.currentIndex = Math.max(0, times.indexOf(start.slice(11, 16)))
        endTime.currentIndex = Math.max(0, times.indexOf(end.slice(11, 16)))
        calendarBox.currentIndex = Math.max(0, calendarChoices.map(function(c) { return String(c.id) }).indexOf(String(fields.calendarId || "")))
        const rule = fields.recurrenceRule
        repeating = !!(rule && rule.frequency)
        repeatBox.currentIndex = !repeating ? 0 : rule.frequency === "daily" ? 1
            : rule.frequency === "weekly" ? ((rule.weekdays || []).length === 5 ? 2 : 3) : rule.frequency === "monthly" ? 4 : 5
        scopeBox.currentIndex = 0
    }
    function openNew(startLocal, endLocal, allDay) {
        eventId = ""; occurrenceId = ""; readOnlyEvent = false
        const start = roundedNow(), end = new Date(start.getTime() + 30 * 60000)
        load({ startLocal: startLocal || isoDate(start) + "T" + pad(start.getHours()) + ":" + pad(start.getMinutes()),
               endLocal: endLocal || isoDate(end) + "T" + pad(end.getHours()) + ":" + pad(end.getMinutes()), allDay: allDay })
        open()
        titleField.forceActiveFocus()
    }
    function openExisting(id, occurrence) {
        const record = service.eventById(id)
        if (!record || !record.id) return
        eventId = id; occurrenceId = occurrence || ""; readOnlyEvent = record.readOnly === true
        load(record)
        open()
    }
    function collect() {
        const allDay = allDaySwitch.checked
        return {
            id: eventId, occurrenceId: occurrenceId, scope: repeating && scopeBox.currentIndex === 0 ? "occurrence" : "series",
            title: titleField.text.trim(), location: locationField.text.trim(), description: notesField.text,
            allDay: allDay,
            startLocal: allDay ? startDate.text : startDate.text + "T" + times[startTime.currentIndex] + ":00",
            endLocal: allDay ? (endDate.text || startDate.text) : (endDate.text || startDate.text) + "T" + times[endTime.currentIndex] + ":00",
            calendarId: calendarChoices.length > 0 ? String(calendarChoices[calendarBox.currentIndex].id) : "local",
            recurrenceRule: repeatChoices[repeatBox.currentIndex].rule
        }
    }
    onAccepted: {
        if (readOnlyEvent) return
        if (eventId.length > 0) service.updateEvent(collect()); else service.createEvent(collect())
    }
    onTertiary: service.deleteEvent(eventId, occurrenceId, repeating && scopeBox.currentIndex === 0 ? "occurrence" : "series")

    component DateField: Tk.Flex {
        id: dateField
        property alias text: field.text
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.xs
        Tk.TextField { id: field; mono: true; small: true; implicitWidth: Tk.Theme.size.fieldWidth; placeholderText: "yyyy-mm-dd"; readOnly: dialog.readOnlyEvent
                       error: text.length > 0 && !/^\d{4}-\d{2}-\d{2}$/.test(text) }
        Tk.IconButton { id: pickButton; small: true; iconName: "calendar"; tooltip: qsTr("Pick a date"); enabled: !dialog.readOnlyEvent; onClicked: picker.open() }
        Tk.Popover {
            id: picker
            anchorItem: pickButton
            Tk.CalendarPicker {
                localeService: pickerLocale
                selectedDate: field.text
                onDateSelected: function(iso) { field.text = iso; picker.close() }
            }
        }
    }
    Tk.LocaleService { id: pickerLocale }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.TextField { id: titleField; objectName: "eventTitle"; placeholderText: qsTr("Add a title"); readOnly: dialog.readOnlyEvent }
        Tk.PropertyRow { label: qsTr("Where"); Tk.TextField { id: locationField; objectName: "eventLocation"; iconName: "map-pin"; placeholderText: qsTr("Location or link"); readOnly: dialog.readOnlyEvent } }
        Tk.PropertyRow { label: qsTr("All day"); Tk.Switch { id: allDaySwitch; objectName: "eventAllDay"; enabled: !dialog.readOnlyEvent } }
        Tk.PropertyRow {
            label: qsTr("Starts")
            Tk.Flex {
                direction: Tk.Flex.Row; gap: Tk.Theme.space.sm
                DateField { id: startDate; objectName: "eventStartDate" }
                Tk.ComboBox { id: startTime; objectName: "eventStartTime"; small: true; visible: !allDaySwitch.checked; model: dialog.times; enabled: !dialog.readOnlyEvent
                              onActivated: function(index) { if (endTime.currentIndex <= index) endTime.currentIndex = Math.min(dialog.times.length - 1, index + 2) } }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Ends")
            Tk.Flex {
                direction: Tk.Flex.Row; gap: Tk.Theme.space.sm
                DateField { id: endDate; objectName: "eventEndDate" }
                Tk.ComboBox { id: endTime; objectName: "eventEndTime"; small: true; visible: !allDaySwitch.checked; model: dialog.times; enabled: !dialog.readOnlyEvent }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Calendar")
            Tk.ComboBox { id: calendarBox; objectName: "eventCalendar"; small: true; textRole: "name"; model: dialog.calendarChoices; enabled: !dialog.readOnlyEvent && dialog.eventId.length === 0 }
        }
        Tk.PropertyRow {
            label: qsTr("Repeats")
            Tk.ComboBox { id: repeatBox; objectName: "eventRepeat"; small: true; textRole: "text"; model: dialog.repeatChoices; enabled: !dialog.readOnlyEvent }
        }
        Tk.PropertyRow {
            visible: dialog.repeating && dialog.eventId.length > 0 && !dialog.readOnlyEvent
            label: qsTr("Apply to")
            Tk.Segmented { id: scopeBox; objectName: "eventScope"; small: true; model: [qsTr("This event"), qsTr("All events")] }
        }
        Tk.TextArea { id: notesField; objectName: "eventNotes"; rows: 4; placeholderText: qsTr("Notes"); readOnly: dialog.readOnlyEvent }
        Tk.Notice { visible: dialog.readOnlyEvent; variant: "info"; title: qsTr("Read-only calendar"); text: qsTr("This event comes from a subscribed calendar; change it at its source.") }
    }
}
