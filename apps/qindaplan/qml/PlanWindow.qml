// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Shell
import QindaOffice.Plan.App

// The QindaPlan window (D-051): one project per window. A rail picks the
// view — Gantt, Tracking Gantt, Leveling Gantt, Network Diagram, Task
// Sheet, Task Usage, Resource Sheet, Resource Usage, Calendar, Reports
// (M79 added four, D-088) — and the window answers the host's requests
// with dialogs.
//
// AGENT-NOTE: every date, span and amount arrives from PlanService already
// formatted by the engine; this QML never computes schedule figures.
OfficeWindow {
    id: window

    readonly property PlanHost planHost: host as PlanHost
    readonly property PlanService plan: planHost !== null ? planHost.plan : null
    readonly property string view: planHost !== null ? planHost.currentView : "gantt"

    width: 1320
    height: 840

    function trigger(id) { window.host.actions.trigger(id) }
    function showToast(title, text, variant) {
        toast.title = title
        toast.message = text
        toast.variant = variant
        toast.show()
    }

    readonly property PlanRibbonLayout ribbonLayout: PlanRibbonLayout { }
    ribbonTabs: ribbonLayout.tabs

    // AGENT-GUARD: the window seats exactly one Item; the Flex fills a
    // plain Item and the toast lives inside it (never a window child).
    Item {
        objectName: "planContent"
        Tk.Flex {
            objectName: "planRoot"
            anchors.fill: parent
            direction: Tk.Flex.Row
            gap: 0
            Tk.NavigationRail {
                objectName: "planRail"
                Tk.Flex.shrink: 0
                expanded: window.width >= 1000
                expandedWidth: Tk.Theme.size.panelMinWidth * 0.8
                currentId: window.view
                tooltip: qsTr("Views")
                groups: [{ id: "views", label: window.plan !== null ? window.plan.title : "", entries: [
                    { id: "gantt", label: qsTr("Gantt Chart"), destination: "planViewGanttAction", iconName: "chart-bar" },
                    { id: "tracking", label: qsTr("Tracking Gantt"), destination: "planViewTrackingGanttAction", iconName: "activity" },
                    { id: "leveling", label: qsTr("Leveling Gantt"), destination: "planViewLevelingGanttAction", iconName: "between-horizontal-start" },
                    { id: "network", label: qsTr("Network Diagram"), destination: "planViewNetworkAction", iconName: "workflow" },
                    { id: "sheet", label: qsTr("Task Sheet"), destination: "planViewTaskSheetAction", iconName: "table" },
                    { id: "taskUsage", label: qsTr("Task Usage"), destination: "planViewTaskUsageAction", iconName: "chart-column-big" },
                    { id: "resources", label: qsTr("Resources"), destination: "planViewResourceSheetAction", iconName: "users" },
                    { id: "usage", label: qsTr("Resource Usage"), destination: "planViewResourceUsageAction", iconName: "chart-column" },
                    { id: "calendar", label: qsTr("Calendar"), destination: "planViewCalendarAction", iconName: "calendar-days" },
                    { id: "reports", label: qsTr("Reports"), destination: "planViewReportsAction", iconName: "layout-dashboard" }] }]
                onDestinationRequested: function(id, destination) { window.trigger(destination) }
            }
            Tk.Divider { vertical: true }
            Item {
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                Tk.Toast {
                    id: toast
                    objectName: "planToast"
                    z: 10
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: Tk.Theme.space.lg
                }
                GanttWorkspace {
                    id: gantt
                    anchors.fill: parent
                    visible: ["gantt", "sheet", "tracking", "leveling"].indexOf(window.view) >= 0
                    showChart: window.view !== "sheet"
                    plan: window.plan
                    host: window.planHost
                }
                NetworkView {
                    id: network
                    objectName: "planNetworkView"
                    anchors.fill: parent
                    visible: window.view === "network"
                    plan: window.plan
                    host: window.planHost
                    onOpenTask: function(taskId) { taskInfo.openFor(taskId, "general") }
                }
                TaskUsageView {
                    objectName: "planTaskUsageView"
                    anchors.fill: parent
                    visible: window.view === "taskUsage"
                    plan: window.plan
                    host: window.planHost
                }
                ResourceSheetView {
                    anchors.fill: parent
                    visible: window.view === "resources"
                    plan: window.plan
                }
                ResourceUsageView {
                    anchors.fill: parent
                    visible: window.view === "usage"
                    plan: window.plan
                    host: window.planHost
                }
                CalendarWorkspace {
                    anchors.fill: parent
                    visible: window.view === "calendar"
                    plan: window.plan
                    onOpenTask: function(taskId) { taskInfo.openFor(taskId, "general") }
                }
                ReportsView {
                    anchors.fill: parent
                    visible: window.view === "reports"
                    plan: window.plan
                    host: window.planHost
                }
            }
        }
    }

    // ---- dialogs ----
    TaskInfoDialog { id: taskInfo; plan: window.plan }
    ProjectInfoDialog { id: projectInfo; plan: window.plan }
    WorkingTimeDialog { id: workingTime; plan: window.plan }
    BaselineDialog { id: baseline; plan: window.plan }
    StatusDateDialog { id: statusDate; plan: window.plan }
    AssignResourcesDialog { id: assign; plan: window.plan }
    LevelDialog { id: level; plan: window.plan }
    PageSetupDialog { id: pageSetup; host: window.planHost }
    ResourceInfoDialog { id: resourceInfo; plan: window.plan; host: window.planHost }
    PlanFileDialogs { id: files; plan: window.plan; host: window.planHost }
    FindTaskDialog { id: findDialog; plan: window.plan; host: window.planHost }
    UpdateTasksDialog { id: updateTasks; plan: window.plan }
    FilterDialog { id: filterDialog; plan: window.plan }

    Binding {
        target: window.planHost
        property: "statusInfo"
        value: window.plan === null ? "" : qsTr("%1 tasks · %2 – %3 · %4 complete")
            .arg(window.plan.taskCount).arg(window.plan.summary.start).arg(window.plan.summary.finish)
            .arg(window.plan.summary.complete || "0%")
    }

    Connections {
        target: window.planHost
        function onTaskInfoRequested(taskId, page) { taskInfo.openFor(taskId, page) }
        function onProjectInfoRequested() { projectInfo.openDialog() }
        function onWorkingTimeRequested(calendarUid) { workingTime.openFor(calendarUid) }
        function onBaselineRequested(clear) { baseline.openFor(clear) }
        function onStatusDateRequested(update) { statusDate.openFor(update) }
        function onAssignResourcesRequested() { assign.openDialog() }
        function onLevelRequested() { level.openDialog() }
        function onPageSetupRequested() { pageSetup.openDialog() }
        function onResourceInfoRequested(uid) { resourceInfo.openFor(uid) }
        function onFindRequested(replace) { findDialog.openFor(replace) }
        function onFileRequested(what) { files.request(what) }
        function onScrollToTaskRequested(taskId) { window.view === "network" ? network.reveal(taskId) : gantt.reveal(taskId) }
        function onGoToTodayRequested() { gantt.goToToday() }
        function onFilterRequested(filterId) { filterDialog.openFor(filterId) }
        function onUpdateTasksRequested() { updateTasks.openDialog() }
        function onLevelingOptionsRequested() { level.openDialog() }
    }
    Connections {
        target: window.plan
        function onNotify(title, text, variant) { window.showToast(title, text, variant) }
        function onRevealTask(taskId) { window.view === "network" ? network.reveal(taskId) : gantt.reveal(taskId) }
    }
}
