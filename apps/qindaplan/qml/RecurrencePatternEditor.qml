// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Project's recurrence pattern and range (M83, D-092) — Recurring Task
// Information's and Change Working Time's "Details": daily (every N days,
// or working days), weekly on days, monthly or yearly on a day or on the
// first … last weekday, from a first day to a last one or for N times.
// load(fields) / collect() speak PlanService's recurrence fields
// (plan_service.h: frequency, every, workdays, days, byPosition, day,
// position, weekday, month, from, to, count). The engine checks them.
Tk.Flex {
    id: editor

    property bool showWorkdays: true // recurring tasks only (calendars have no "workdays")
    property string frequency: "weekly"
    property int days: 1
    property bool byPosition: false
    property bool byCount: true
    readonly property var frequencies: ["daily", "weekly", "monthly", "yearly"]
    readonly property var dayNames: [qsTr("Mon"), qsTr("Tue"), qsTr("Wed"), qsTr("Thu"), qsTr("Fri"), qsTr("Sat"), qsTr("Sun")]
    readonly property var weekdayNames: [qsTr("Monday"), qsTr("Tuesday"), qsTr("Wednesday"), qsTr("Thursday"), qsTr("Friday"),
                                         qsTr("Saturday"), qsTr("Sunday")]
    readonly property var monthNames: [qsTr("January"), qsTr("February"), qsTr("March"), qsTr("April"), qsTr("May"),
                                       qsTr("June"), qsTr("July"), qsTr("August"), qsTr("September"), qsTr("October"),
                                       qsTr("November"), qsTr("December")]

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.sm

    function load(f) {
        frequency = String(f.frequency || "weekly")
        everyField.text = String(f.every || 1)
        workdaysSwitch.checked = f.workdays === true
        days = Number(f.days || 0)
        byPosition = f.byPosition === true
        dayField.text = String(f.day || 1)
        positionBox.currentIndex = Number(f.position || 0)
        weekdayBox.currentIndex = Math.max(0, Number(f.weekday || 1) - 1)
        monthBox.currentIndex = Math.max(0, Number(f.month || 1) - 1)
        fromField.text = String(f.from || "")
        toField.text = String(f.to || "")
        countField.text = Number(f.count || 0) > 0 ? String(f.count) : "10"
        byCount = Number(f.count || 0) > 0 || String(f.to || "").length === 0
    }
    function collect() {
        return { frequency: frequency, every: parseInt(everyField.text) || 1, workdays: workdaysSwitch.checked,
                 days: days, byPosition: byPosition, day: parseInt(dayField.text) || 1,
                 position: positionBox.currentIndex, weekday: weekdayBox.currentIndex + 1,
                 month: monthBox.currentIndex + 1, from: fromField.text, to: byCount ? "" : toField.text,
                 count: byCount ? (parseInt(countField.text) || 0) : 0 }
    }

    Tk.Segmented {
        objectName: "recurrenceFrequency"
        small: true
        model: [qsTr("Daily"), qsTr("Weekly"), qsTr("Monthly"), qsTr("Yearly")]
        currentIndex: editor.frequencies.indexOf(editor.frequency)
        tooltip: qsTr("How often it recurs")
        onActivated: function(i) { editor.frequency = editor.frequencies[i] }
    }
    Tk.PropertyRow {
        visible: editor.frequency !== "yearly"
        label: editor.frequency === "daily" ? qsTr("Every … days") : editor.frequency === "weekly" ? qsTr("Every … weeks")
                                                                                              : qsTr("Every … months")
        Tk.TextField { id: everyField; objectName: "recurrenceEvery"; small: true; inputMethodHints: Qt.ImhDigitsOnly }
    }
    Tk.Switch {
        id: workdaysSwitch
        objectName: "recurrenceWorkdays"
        visible: editor.showWorkdays && editor.frequency === "daily"
        text: qsTr("Working days only")
    }
    Tk.Flex {
        visible: editor.frequency === "weekly"
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.sm
        wrap: Tk.Flex.Wrap
        Repeater {
            model: 7
            delegate: Tk.CheckBox {
                required property int index
                objectName: "recurrenceDay_" + index
                text: editor.dayNames[index]
                checked: ((editor.days >> index) & 1) === 1
                onToggled: editor.days = checked ? (editor.days | (1 << index)) : (editor.days & ~(1 << index))
            }
        }
    }
    Tk.Flex {
        visible: editor.frequency === "monthly" || editor.frequency === "yearly"
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.ComboBox {
            id: monthBox
            objectName: "recurrenceMonth"
            visible: editor.frequency === "yearly"
            small: true
            model: editor.monthNames
            tooltip: qsTr("Month")
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.RadioButton {
                objectName: "recurrenceByDay"
                text: qsTr("On day")
                checked: !editor.byPosition
                onToggled: if (checked) editor.byPosition = false
            }
            Tk.TextField { id: dayField; objectName: "recurrenceDayOfMonth"; small: true; Tk.Flex.basis: Tk.Theme.size.control * 2 }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.RadioButton {
                objectName: "recurrenceByPosition"
                text: qsTr("On the")
                checked: editor.byPosition
                onToggled: if (checked) editor.byPosition = true
            }
            Tk.ComboBox {
                id: positionBox
                objectName: "recurrencePosition"
                small: true
                model: [qsTr("first"), qsTr("second"), qsTr("third"), qsTr("fourth"), qsTr("last")]
            }
            Tk.ComboBox { id: weekdayBox; objectName: "recurrenceWeekday"; small: true; model: editor.weekdayNames }
        }
    }
    Tk.PropertyRow {
        label: qsTr("Starting")
        Tk.TextField { id: fromField; objectName: "recurrenceFrom"; small: true; placeholderText: qsTr("e.g. 2 Mar 2026") }
    }
    Tk.Flex {
        direction: Tk.Flex.Row
        align: Tk.Flex.Center
        gap: Tk.Theme.space.sm
        Tk.RadioButton {
            objectName: "recurrenceByCount"
            text: qsTr("End after")
            checked: editor.byCount
            onToggled: if (checked) editor.byCount = true
        }
        Tk.TextField { id: countField; objectName: "recurrenceCount"; small: true; Tk.Flex.basis: Tk.Theme.size.control * 2 }
        Tk.Caption { text: qsTr("occurrences") }
    }
    Tk.Flex {
        direction: Tk.Flex.Row
        align: Tk.Flex.Center
        gap: Tk.Theme.space.sm
        Tk.RadioButton {
            objectName: "recurrenceByDate"
            text: qsTr("End by")
            checked: !editor.byCount
            onToggled: if (checked) editor.byCount = false
        }
        Tk.TextField { id: toField; objectName: "recurrenceTo"; small: true; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
    }
}
