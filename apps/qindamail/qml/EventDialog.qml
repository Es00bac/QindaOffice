// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Mail.App

// Create or edit one event (D-042): title, where, when (dates from a month
// picker, times on a 15-minute list, or all day), which calendar, how it
// repeats, who to invite (M64: suggestions from People and mail; Save sends
// them a meeting request), notes. A repeating occurrence asks whether the
// edit is for this one or the whole series. Read-only calendars (feeds) open
// for viewing. M81 (D-090): categories from the calendar account's list.
Tk.Dialog {
    id: dialog

    property MailService service: null
    property string eventId: ""
    property string occurrenceId: ""
    property bool readOnlyEvent: false
    property bool repeating: false
    property var masterRecord: ({})
    property var occurrenceRecord: ({})
    property var originalRule: ({})
    property string originalRaw: ""
    property string eventZone: ""
    property bool loadingFields: false
    property string pendingSaveId: ""
    property var pendingSave: ({})
    property var pendingInvitees: []
    property var pendingCategories: []
    readonly property var repeatChoices: [
        { text: qsTr("Does not repeat"), rule: null },
        { text: qsTr("Every day"), rule: { frequency: "daily", interval: 1 } },
        { text: qsTr("Every weekday"), rule: { frequency: "weekly", interval: 1, weekdays: [1, 2, 3, 4, 5] } },
        { text: qsTr("Every week"), rule: { frequency: "weekly", interval: 1 } },
        { text: qsTr("Every month"), rule: { frequency: "monthly", interval: 1 } },
        { text: qsTr("Every year"), rule: { frequency: "yearly", interval: 1 } },
        { text: qsTr("Keep current recurrence"), rule: null }]
    readonly property var times: {
        const out = []
        for (let m = 0; m < 24 * 60; m += 15) out.push(String(Math.floor(m / 60)).padStart(2, "0") + ":" + String(m % 60).padStart(2, "0"))
        return out
    }
    readonly property var calendarChoices: service !== null ? service.writableCalendars : []
    // M64: the invitees as TokenField tokens; non-Item helpers are properties.
    property var invited: []
    readonly property RecipientCompleter completer: RecipientCompleter { service: dialog.service }
    readonly property Tk.MailAddressValidator addresses: Tk.MailAddressValidator { }

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
        loadingFields=true
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
        eventZone=String(fields.zone||"")
        startTime.editText=start.slice(11,19)
        endTime.editText=end.slice(11,19)
        originalRule=fields.recurrenceRule||({})
        originalRaw=String(fields.recurrenceRaw||"")
        repeating=!!originalRule.frequency||originalRaw.length>0
        repeatBox.currentIndex=repeating?6:0
        dialog.invited = (fields.attendees || []).map(function(a) {
            const label = a.name ? "\"" + String(a.name).replace(/"/g, "") + "\" <" + a.address + ">" : String(a.address)
            return { id: "inv-" + Math.random().toString(16).slice(2), label: label, value: String(a.address), valid: true } })
        attendeesField.tokens = dialog.invited
        eventCategories.names = fields.categories || []          // M81
        loadingFields=false
    }
    function newEventId() {
        return "event:" + "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx".replace(/[xy]/g, function(c) {
            const r = Math.random() * 16 | 0
            return (c === "x" ? r : (r & 3 | 8)).toString(16) })
    }
    function checkRecipient(text) {
        const parsed = dialog.addresses.parseOne(text)
        return { label: parsed.display, value: parsed.address, valid: parsed.valid, error: parsed.error }
    }
    function openNew(startLocal, endLocal, allDay) {
        eventId = ""; occurrenceId = ""; readOnlyEvent = false; masterRecord=({}); occurrenceRecord=({}); scopeBox.currentIndex=1
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
        masterRecord=record
        occurrenceRecord=occurrenceId.length?service.eventOccurrence(id,occurrenceId):record
        scopeBox.currentIndex=occurrenceId.length?0:1
        load(occurrenceRecord.id?occurrenceRecord:record)
        open()
    }
    function timeText(box) {
        const text=String(box.editText||times[box.currentIndex]||"").trim()
        return /^\d{2}:\d{2}$/.test(text)?text+":00":text
    }
    function collect() {
        const allDay = allDaySwitch.checked
        const result={
            id: eventId, occurrenceId: occurrenceId, scope: repeating && occurrenceId.length>0 && scopeBox.currentIndex === 0 ? "occurrence" : "series",
            zone:eventZone,
            title: titleField.text.trim(), location: locationField.text.trim(), description: notesField.text,
            allDay: allDay,
            startLocal: allDay ? startDate.text : startDate.text + "T" + timeText(startTime),
            endLocal: allDay ? (endDate.text || startDate.text) : (endDate.text || startDate.text) + "T" + timeText(endTime),
            calendarId: calendarChoices.length > 0 ? String(calendarChoices[calendarBox.currentIndex].id) : "local"
        }
        if(result.scope==="series"&&repeatBox.currentIndex!==6)result.recurrenceRule=repeatChoices[repeatBox.currentIndex].rule
        return result
    }
    onAccepted: {
        if (readOnlyEvent) return
        if (attendeesField.draft.trim().length > 0) attendeesField.commitDraft()
        const event = collect()
        if (eventId.length > 0) {
            pendingSaveId=service.updateEvent(event)
        } else {
            event.id = dialog.newEventId()
            pendingSaveId=service.createEvent(event)
        }
        pendingSave=event
        pendingInvitees=attendeesField.tokens.filter(function(t) { return t.valid !== false }).map(function(t) { return t.label })
        pendingCategories=Array.from(eventCategories.names)
    }
    onTertiary: service.deleteEvent(eventId, occurrenceId, repeating && occurrenceId.length>0 && scopeBox.currentIndex === 0 ? "occurrence" : "series")

    Connections {
        target:dialog.service
        function onEventChangeFinished(id,state,message) {
            if(id!==dialog.pendingSaveId)return
            dialog.pendingSaveId=""
            if(state!=="accepted"&&state!=="offline")return
            if(dialog.pendingSave.scope!=="occurrence")dialog.service.organizer.setCategories("event",[dialog.pendingSave.id],dialog.pendingCategories)
            if(dialog.pendingInvitees.length>0)dialog.service.sendInvitations(dialog.pendingSave.id,dialog.pendingInvitees,
                dialog.pendingSave.scope==="occurrence"?dialog.pendingSave.occurrenceId:"")
        }
    }
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
                Tk.ComboBox { id: startTime; objectName: "eventStartTime"; editable:true; small: true; visible: !allDaySwitch.checked; model: dialog.times; enabled: !dialog.readOnlyEvent
                              onActivated: function(index) { if (endTime.currentIndex <= index) endTime.currentIndex = Math.min(dialog.times.length - 1, index + 2) } }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Ends")
            Tk.Flex {
                direction: Tk.Flex.Row; gap: Tk.Theme.space.sm
                DateField { id: endDate; objectName: "eventEndDate" }
                Tk.ComboBox { id: endTime; objectName: "eventEndTime"; editable:true; small: true; visible: !allDaySwitch.checked; model: dialog.times; enabled: !dialog.readOnlyEvent }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Calendar")
            Tk.ComboBox { id: calendarBox; objectName: "eventCalendar"; small: true; textRole: "name"; model: dialog.calendarChoices; enabled: !dialog.readOnlyEvent && dialog.eventId.length === 0 }
        }
        Tk.Caption { objectName:"eventTimeZone"; text:qsTr("Time zone: %1").arg(dialog.eventZone||qsTr("System time zone")); wrapMode:Text.Wrap }
        Tk.PropertyRow {
            label: qsTr("Repeats")
            Tk.ComboBox { id: repeatBox; objectName: "eventRepeat"; small: true; textRole: "text"; model: dialog.repeatChoices; enabled: !dialog.readOnlyEvent && (dialog.eventId.length===0 || scopeBox.currentIndex===1) }
        }
        Tk.PropertyRow {
            visible: dialog.repeating && dialog.eventId.length > 0 && dialog.occurrenceId.length>0 && !dialog.readOnlyEvent
            label: qsTr("Apply to")
            Tk.Segmented { id: scopeBox; objectName: "eventScope"; small: true; model: [qsTr("This occurrence"), qsTr("Entire series")]
                onActivated:index=> { if(!dialog.loadingFields)dialog.load(index===0?dialog.occurrenceRecord:dialog.masterRecord) }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Invite")
            Tk.TokenField {
                id: attendeesField
                objectName: "eventAttendees"
                standalone: true
                enabled: !dialog.readOnlyEvent
                label: qsTr("People to invite")
                placeholderText: qsTr("Add people to invite")
                tooltip: qsTr("Invitees · press comma or Enter after each address; Save sends the invitations")
                validator: dialog.checkRecipient
                completionProvider: dialog.completer
                onTokensEdited: function(tokens) { dialog.invited = tokens }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Categories")
            CategoryField {
                id: eventCategories
                objectName: "eventCategories"
                service: dialog.service
                readOnly: dialog.readOnlyEvent || (dialog.repeating && dialog.occurrenceId.length>0 && scopeBox.currentIndex===0)
                accountId: dialog.calendarChoices.length > 0 ? String(dialog.calendarChoices[calendarBox.currentIndex].accountId || "local") : "local"
            }
        }
        Tk.TextArea { id: notesField; objectName: "eventNotes"; rows: 4; placeholderText: qsTr("Notes"); readOnly: dialog.readOnlyEvent }
        Tk.Notice { visible: dialog.readOnlyEvent; variant: "info"; title: qsTr("Read-only calendar"); text: qsTr("This event comes from a subscribed calendar; change it at its source.") }
    }
}
