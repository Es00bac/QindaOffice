// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Plan.App

// The Calendar view: the plan's tasks as all-day bars on a month (or week)
// grid (Tk.CalendarGridView over PlanService.calendarEvents). Tasks are
// coloured by state — to do, complete, milestone, critical — and read-only
// here: dates are the scheduler's; double-click opens Task Information.
Item {
    id: view

    property PlanService plan: null
    signal openTask(string taskId)

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: 0
        Tk.Flex {
            Tk.Flex.shrink: 0
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.sm
            padding: Tk.Theme.space.md
            Tk.IconButton {
                objectName: "planCalendarPrevious"
                iconName: "chevron-left"
                tooltip: qsTr("Previous")
                onClicked: grid.navigate(-1)
            }
            Tk.IconButton {
                objectName: "planCalendarNext"
                iconName: "chevron-right"
                tooltip: qsTr("Next")
                onClicked: grid.navigate(1)
            }
            Tk.Button {
                small: true
                variant: "outline"
                text: qsTr("Today")
                onClicked: grid.goToday(view.plan.today)
            }
            Tk.Heading {
                objectName: "planCalendarTitle"
                level: 2
                text: grid.title
                Tk.Flex.grow: 1
                Tk.Flex.basis: Tk.Theme.size.fieldWidth * 1.4
            }
            Repeater {
                model: [{ label: qsTr("Tasks"), variant: "accent" }, { label: qsTr("Complete"), variant: "success" },
                        { label: qsTr("Milestones"), variant: "warning" }, { label: qsTr("Critical"), variant: "danger" }]
                delegate: Tk.Badge {
                    required property var modelData
                    text: modelData.label
                    variant: modelData.variant
                }
            }
            Tk.Segmented {
                objectName: "planCalendarMode"
                small: true
                tooltip: qsTr("Month or week")
                model: [qsTr("Month"), qsTr("Week")]
                currentIndex: grid.mode === "month" ? 0 : 1
                onActivated: function(index) { grid.setMode(index === 0 ? "month" : "week") }
            }
        }
        Tk.CalendarGridView {
            id: grid
            objectName: "planCalendar"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            mode: "month"
            weekStart: 1
            readOnly: true
            tooltip: qsTr("Tasks by date")
            events: view.plan !== null ? view.plan.calendarEvents : []
            calendars: view.plan !== null ? view.plan.calendarSources : []
            now: view.plan !== null ? view.plan.today + "T12:00" : ""
            onOccurrenceSelected: function(eventId, occurrenceId) { view.plan.currentTaskId = eventId }
            onOpenRequested: function(eventId, occurrenceId) { view.openTask(eventId) }
        }
    }
    // Open on the month the project starts in.
    onPlanChanged: if (plan !== null) grid.anchorDate = plan.projectStartIso
}
