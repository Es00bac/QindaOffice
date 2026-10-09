// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Plan.App

// Change Working Time: pick a calendar (the project's, or a resource's
// own), set each weekday's hours ("08:00–12:00, 13:00–17:00",
// "Nonworking", or "default" to inherit from the base calendar) and keep
// the list of exceptions — holidays and odd working days. Every change is
// applied at once as its own undo step and the schedule follows. M79:
// Project's recurring exceptions and alternative work weeks from its XML
// are listed (and scheduled) but not edited here.
Tk.Dialog {
    id: dialog

    property PlanService plan: null
    property int calendarUid: 0
    property var calendarList: []
    property var week: []
    property var exceptions: []

    objectName: "workingTimeDialog"
    title: qsTr("Change Working Time")
    dialogWidth: Tk.Theme.size.dialogWidth * 1.6
    primaryText: qsTr("Done")
    secondaryText: ""

    function refresh() {
        calendarList = plan.calendars()
        week = plan.calendarWeek(calendarUid)
        exceptions = plan.calendarExceptions(calendarUid)
    }
    function openFor(uid) {
        calendarUid = uid
        refresh()
        error.visible = false
        open()
    }
    function report(r) {
        error.visible = !r.ok
        if (!r.ok) error.text = String(r.error || "")
        refresh()
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.PropertyRow {
            label: qsTr("Calendar")
            Tk.ComboBox {
                objectName: "workingTimeCalendar"
                small: true
                model: dialog.calendarList.map(function(c) {
                    return c.name + (c.isProject ? qsTr(" (project)") : c.resources.length ? " — " + c.resources : "")
                })
                currentIndex: Math.max(0, dialog.calendarList.map(function(c) { return c.uid }).indexOf(dialog.calendarUid))
                onActivated: function(index) { dialog.calendarUid = dialog.calendarList[index].uid; dialog.refresh() }
            }
        }
        Tk.Notice { id: error; visible: false; variant: "danger"; title: qsTr("Not changed") }
        Tk.Overline { title: qsTr("Work week") }
        Repeater {
            model: dialog.week
            delegate: Tk.PropertyRow {
                id: dayRow
                required property var modelData
                label: String(modelData.name)
                hint: modelData.inherited ? qsTr("From the base calendar") : ""
                Tk.TextField {
                    objectName: "workingTimeDay_" + dayRow.modelData.day
                    small: true
                    mono: true
                    text: String(dayRow.modelData.hours)
                    onEditingFinished: if (text !== String(dayRow.modelData.hours))
                        dialog.report(dialog.plan.setCalendarDay(dialog.calendarUid, dayRow.modelData.day, text))
                }
            }
        }
        Tk.Overline { title: qsTr("Exceptions") }
        Tk.Caption { visible: dialog.exceptions.length === 0; text: qsTr("No holidays or exceptions yet.") }
        Repeater {
            model: dialog.exceptions
            delegate: Tk.Flex {
                id: exceptionRow
                required property var modelData
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                Tk.Icon { name: exceptionRow.modelData.working ? "calendar-check" : "calendar-x"; size: Tk.Theme.size.icon; color: Tk.Theme.color.textMuted }
                Tk.Label { text: String(exceptionRow.modelData.name); Tk.Flex.grow: 1; Tk.Flex.basis: 0; elide: Text.ElideRight }
                Tk.Caption {
                    visible: exceptionRow.modelData.readOnly === true
                    Tk.Flex.shrink: 0
                    text: String(exceptionRow.modelData.recurrence || "")
                }
                Tk.Caption {
                    Tk.Flex.shrink: 0
                    text: exceptionRow.modelData.from === exceptionRow.modelData.to ? String(exceptionRow.modelData.from)
                          : String(exceptionRow.modelData.from) + " – " + String(exceptionRow.modelData.to)
                }
                Tk.Caption { Tk.Flex.shrink: 0; text: String(exceptionRow.modelData.hours) }
                Tk.IconButton {
                    visible: exceptionRow.modelData.readOnly !== true
                    iconName: "trash-2"
                    tooltip: qsTr("Remove this exception")
                    onClicked: dialog.report(dialog.plan.removeCalendarException(dialog.calendarUid, exceptionRow.modelData.index))
                }
            }
        }
        Tk.Grid {
            columns: "2fr 1fr 1fr 2fr auto"
            columnGap: Tk.Theme.space.sm
            Tk.TextField { id: exName; objectName: "workingTimeExceptionName"; small: true; placeholderText: qsTr("Name, e.g. Memorial Day") }
            Tk.TextField { id: exFrom; objectName: "workingTimeExceptionFrom"; small: true; placeholderText: qsTr("From") }
            Tk.TextField { id: exTo; small: true; placeholderText: qsTr("To (optional)") }
            Tk.TextField { id: exHours; small: true; mono: true; placeholderText: qsTr("Nonworking") }
            Tk.Button {
                objectName: "workingTimeAddException"
                small: true
                iconName: "calendar-plus"
                text: qsTr("Add")
                enabled: exFrom.text.trim().length > 0
                onClicked: {
                    dialog.report(dialog.plan.addCalendarException(dialog.calendarUid, exName.text, exFrom.text, exTo.text, exHours.text))
                    if (!error.visible) { exName.text = ""; exFrom.text = ""; exTo.text = ""; exHours.text = "" }
                }
            }
        }
    }
}
