// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Plan.App

// Change Working Time ▸ recurring exceptions (M83, D-092): Project's
// yearly, monthly, weekly and every-N-days exceptions — listed, made,
// changed and removed here (M79 only read and scheduled them). Each edit is
// one undo step; the engine writes Project's own <Exception> element
// (calendar_edit.h) so the .qplan and Microsoft Project XML carry it.
Tk.Flex {
    id: section

    property PlanService plan: null
    property int calendarUid: 0
    property var items: []
    property int editing: -2 // -2 closed, -1 a new one, else the one being changed
    signal report(var result)

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.sm

    function refresh() { items = plan !== null ? plan.recurringExceptions(calendarUid) : [] }
    function edit(index) {
        editing = index
        const f = index >= 0 ? items[index]
                             : { frequency: "yearly", every: 1, day: 1, month: 1, from: plan.projectStartIso, count: 0, to: "" }
        nameField.text = String(f.name || "")
        hoursField.text = f.working === true ? String(f.hours) : ""
        pattern.load(f)
    }
    function save() {
        const out = pattern.collect()
        out.name = nameField.text
        out.hours = hoursField.text
        const r = plan.saveRecurringException(calendarUid, editing, out)
        if (r.ok) editing = -2
        report(r)
    }

    Tk.Overline { title: qsTr("Recurring exceptions") }
    Tk.Caption { visible: section.items.length === 0; text: qsTr("No recurring holidays or working days.") }
    Repeater {
        model: section.items
        delegate: Tk.Flex {
            id: row
            required property var modelData
            required property int index
            objectName: "workingTimeRecurring_" + index
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Icon { name: "repeat"; size: Tk.Theme.size.icon; color: Tk.Theme.color.textMuted }
            Tk.Label { text: String(row.modelData.name || qsTr("(no name)")); elide: Text.ElideRight; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.Caption { Tk.Flex.shrink: 0; text: String(row.modelData.text) }
            Tk.Caption { Tk.Flex.shrink: 0; text: String(row.modelData.range) }
            Tk.Caption { Tk.Flex.shrink: 0; text: String(row.modelData.hours) }
            Tk.IconButton {
                objectName: "workingTimeRecurringEdit_" + row.index
                iconName: "pencil"
                tooltip: qsTr("Change this exception")
                onClicked: section.edit(row.index)
            }
            Tk.IconButton {
                iconName: "trash-2"
                tooltip: qsTr("Remove this exception")
                onClicked: section.report(section.plan.deleteRecurringException(section.calendarUid, row.index))
            }
        }
    }
    Tk.Button {
        objectName: "workingTimeAddRecurring"
        visible: section.editing === -2
        small: true
        iconName: "calendar-plus"
        text: qsTr("Add a recurring exception")
        onClicked: section.edit(-1)
    }
    Tk.Flex {
        visible: section.editing > -2
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Grid {
            columns: "1fr 1fr"
            columnGap: Tk.Theme.space.sm
            Tk.TextField { id: nameField; objectName: "workingTimeRecurringName"; small: true; placeholderText: qsTr("Name, e.g. Christmas") }
            Tk.TextField { id: hoursField; objectName: "workingTimeRecurringHours"; small: true; mono: true; placeholderText: qsTr("Nonworking") }
        }
        RecurrencePatternEditor { id: pattern; objectName: "workingTimeRecurringPattern"; showWorkdays: false }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Item { Tk.Flex.grow: 1 }
            Tk.Button { small: true; text: qsTr("Cancel"); onClicked: section.editing = -2 }
            Tk.Button { objectName: "workingTimeRecurringSave"; small: true; variant: "accent"; text: qsTr("Save"); onClicked: section.save() }
        }
    }
}
