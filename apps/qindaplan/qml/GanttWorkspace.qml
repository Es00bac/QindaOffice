// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk
import QindaOffice.Plan.App

// The Gantt Chart view (and, with showChart false, the Task Sheet view):
// the Timeline strip above a split of the task sheet (Tk.EditableGrid) and
// the chart (Tk.GanttView on the calendar time scale). Both show the same
// rows in the same order at the same row height; their vertical scrolling
// is kept in step both ways and the header heights match, so a task's row
// lines up with its bar. One selection (PlanService.selection) drives both.
Item {
    id: workspace

    property PlanService plan: null
    property PlanHost host: null
    property bool showChart: true
    readonly property real headerHeight: Tk.Theme.size.row * 2

    // Brings a task into view in both halves and centres its bar.
    function reveal(taskId) {
        if (workspace.plan === null || taskId === "") return
        sheet.reveal(taskId)
        const row = workspace.plan.ganttModel.indexOfId(taskId)
        if (row < 0) return
        const record = workspace.plan.ganttModel.get(row)
        chart.visibleStartUtc = workspace.shiftUtc(record.startUtc, -3)
    }
    function goToToday() {
        const marker = workspace.plan !== null && workspace.plan.markers.length > 0 ? workspace.plan.markers[0] : null
        if (marker !== null) chart.visibleStartUtc = workspace.shiftUtc(marker.utc, -3)
    }
    // A UTC instant `days` whole days earlier/later, at midnight.
    function shiftUtc(utc, days) {
        const day = 86400000
        return String(Math.floor(Number(utc) / day) * day + days * day)
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: 0
        TimelineStrip {
            objectName: "planTimelineStrip"
            Tk.Flex.shrink: 0
            Tk.Flex.basis: Tk.Theme.size.row * 7.5
            visible: workspace.showChart && workspace.host !== null && workspace.host.showTimeline
            plan: workspace.plan
        }
        Tk.Splitter {
            id: split
            objectName: "planSplit"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            orientation: Qt.Horizontal
            tooltip: qsTr("Resize the task sheet and the chart")
            TaskSheet {
                id: sheet
                T.SplitView.preferredWidth: workspace.showChart ? Math.max(split.width * 0.44, Tk.Theme.size.panelMinWidth)
                                                                : split.width
                T.SplitView.minimumWidth: Tk.Theme.size.panelMinWidth
                T.SplitView.fillWidth: !workspace.showChart
                plan: workspace.plan
                headerHeight: workspace.headerHeight
                onScrollYChanged: if (Math.abs(chart.contentY - scrollY) > 0.5) chart.contentY = scrollY
            }
            Tk.GanttView {
                id: chart
                objectName: "planGantt"
                visible: workspace.showChart
                T.SplitView.fillWidth: workspace.showChart
                T.SplitView.minimumWidth: Tk.Theme.size.panelMinWidth
                tooltip: qsTr("Gantt chart")
                timeScale: "calendar"
                labelWidth: 0
                headerHeight: workspace.headerHeight
                rowHeight: Tk.Theme.size.row
                taskModel: workspace.plan !== null ? workspace.plan.ganttModel : null
                calendar: workspace.plan !== null ? workspace.plan.ganttCalendar : null
                selectionModel: workspace.plan !== null ? workspace.plan.selection : null
                pixelsPerDay: workspace.plan !== null && workspace.host !== null
                              ? workspace.plan.dayScale * Tk.Theme.size.control * workspace.host.zoom : Tk.Theme.size.control
                markers: workspace.plan !== null ? workspace.plan.markers : []
                onContentYChanged: if (Math.abs(sheet.scrollY - contentY) > 0.5) sheet.scrollY = contentY
                onTaskActivated: function(taskId) {
                    workspace.plan.currentTaskId = taskId
                    if (workspace.host !== null) workspace.host.actions.trigger("planTaskInfoAction")
                }
                Component.onCompleted: if (workspace.plan !== null) visibleStartUtc = workspace.plan.ganttStartUtc
            }
        }
    }
    onPlanChanged: if (plan !== null) chart.visibleStartUtc = plan.ganttStartUtc
    // A project opened into this window starts the chart at its start.
    Connections {
        target: workspace.host
        function onDocumentChanged() { if (workspace.plan !== null) chart.visibleStartUtc = workspace.plan.ganttStartUtc }
    }
}
