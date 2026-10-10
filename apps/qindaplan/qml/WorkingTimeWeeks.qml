// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Plan.App

// Change Working Time ▸ work weeks (M83, D-092): Project's alternative
// work weeks — a date range whose days replace the calendar's week (a day
// left "default" keeps it) — listed, made, changed and removed here. Each
// edit is one undo step; the engine writes Project's <WorkWeeks> element
// (calendar_edit.h).
Tk.Flex {
    id: section

    property PlanService plan: null
    property int calendarUid: 0
    property var items: []
    property int editing: -2 // -2 closed, -1 a new one, else the one being changed
    property var days: ["", "", "", "", "", "", ""]
    readonly property var dayNames: [qsTr("Monday"), qsTr("Tuesday"), qsTr("Wednesday"), qsTr("Thursday"), qsTr("Friday"),
                                     qsTr("Saturday"), qsTr("Sunday")]
    signal report(var result)

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.sm

    function refresh() { items = plan !== null ? plan.workWeeks(calendarUid) : [] }
    function edit(index) {
        editing = index
        const w = index >= 0 ? items[index] : { name: "", from: plan.projectStartIso, to: "", days: [] }
        nameField.text = String(w.name || "")
        fromField.text = String(w.from || "")
        toField.text = String(w.to || "")
        const d = []
        for (let i = 0; i < 7; ++i) {
            const text = w.days && w.days[i] !== undefined ? String(w.days[i]) : "default"
            d.push(text === "default" ? "" : text)
        }
        days = d
    }
    function save() {
        const r = plan.saveWorkWeek(calendarUid, editing, { name: nameField.text, from: fromField.text, to: toField.text,
                                                            days: days.map(function(t) { return t.trim().length ? t : "default" }) })
        if (r.ok) editing = -2
        report(r)
    }

    Tk.Overline { title: qsTr("Work weeks") }
    Tk.Caption { visible: section.items.length === 0; text: qsTr("No alternative work weeks.") }
    Repeater {
        model: section.items
        delegate: Tk.Flex {
            id: row
            required property var modelData
            required property int index
            objectName: "workingTimeWeek_" + index
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Icon { name: "calendar-range"; size: Tk.Theme.size.icon; color: Tk.Theme.color.textMuted }
            Tk.Label { text: String(row.modelData.name || qsTr("(no name)")); elide: Text.ElideRight; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.Caption { Tk.Flex.shrink: 0; text: String(row.modelData.range) }
            Tk.IconButton {
                objectName: "workingTimeWeekEdit_" + row.index
                iconName: "pencil"
                tooltip: qsTr("Change this work week")
                onClicked: section.edit(row.index)
            }
            Tk.IconButton {
                iconName: "trash-2"
                tooltip: qsTr("Remove this work week")
                onClicked: section.report(section.plan.deleteWorkWeek(section.calendarUid, row.index))
            }
        }
    }
    Tk.Button {
        objectName: "workingTimeAddWeek"
        visible: section.editing === -2
        small: true
        iconName: "calendar-plus"
        text: qsTr("Add a work week")
        onClicked: section.edit(-1)
    }
    Tk.Flex {
        visible: section.editing > -2
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Grid {
            columns: "2fr 1fr 1fr"
            columnGap: Tk.Theme.space.sm
            Tk.TextField { id: nameField; objectName: "workingTimeWeekName"; small: true; placeholderText: qsTr("Name, e.g. Summer hours") }
            Tk.TextField { id: fromField; objectName: "workingTimeWeekFrom"; small: true; placeholderText: qsTr("From") }
            Tk.TextField { id: toField; objectName: "workingTimeWeekTo"; small: true; placeholderText: qsTr("To") }
        }
        Repeater {
            model: 7
            delegate: Tk.PropertyRow {
                id: dayRow
                required property int index
                label: section.dayNames[index]
                dense: true
                Tk.TextField {
                    objectName: "workingTimeWeekDay_" + dayRow.index
                    small: true
                    mono: true
                    text: section.days[dayRow.index]
                    placeholderText: qsTr("default (the calendar's own)")
                    onEditingFinished: { const d = section.days.slice(); d[dayRow.index] = text; section.days = d }
                }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Item { Tk.Flex.grow: 1 }
            Tk.Button { small: true; text: qsTr("Cancel"); onClicked: section.editing = -2 }
            Tk.Button { objectName: "workingTimeWeekSave"; small: true; variant: "accent"; text: qsTr("Save"); onClicked: section.save() }
        }
    }
}
