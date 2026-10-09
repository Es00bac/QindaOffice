// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaOffice.Shell

// QindaPlan's ribbon (D-041, D-051): File · Task · Resource · Report ·
// Project · View, and the contextual Gantt Chart Format tab while a Gantt
// view is shown (PlanHost's "ganttFormat" context). The Task tab is the
// "home" tab the window opens on. Every string is an ActionEntry id PlanHost
// registers (plan_command_table.h). M79 (D-088): the Tracking and Leveling
// Gantt, Network Diagram and Task Usage views, Filter and Sort, the baseline
// the bars show, Slack, Mark on Track / Update Tasks, Level Selection,
// Leveling Options, the Cost and Work Overview dashboards.
QtObject {
    readonly property var views: ["planViewGanttAction", "planViewTrackingGanttAction", "planViewLevelingGanttAction",
                                  "planViewNetworkAction", "planViewTaskSheetAction", "planViewTaskUsageAction", "|",
                                  "planViewResourceSheetAction", "planViewResourceUsageAction", "planViewCalendarAction",
                                  "planViewReportsAction"]
    readonly property var tables: ["planTableEntryAction", "planTableTrackingAction", "planTableVarianceAction",
                                   "planTableCostAction", "planTableScheduleAction", "planTableWorkAction",
                                   "planTableBaselineAction", "planTableDelayAction"]
    readonly property var filters: ["planFilterAllAction", "|", "planFilterCriticalAction", "planFilterIncompleteAction",
                                    "planFilterCompletedAction", "planFilterLateAction", "planFilterSlippingAction",
                                    "planFilterMilestonesAction", "planFilterSummaryAction", "planFilterDeadlinesAction", "|",
                                    "planFilterResourceAction", "planFilterDateRangeAction", "|",
                                    "planFilterRelatedSummariesAction"]
    readonly property var sorts: ["planSortIdAction", "planSortStartAction", "planSortFinishAction",
                                  "planSortPriorityAction", "planSortCostAction"]
    readonly property var baselinesShown: ["planBaselineShown0Action", "planBaselineShown1Action", "planBaselineShown2Action",
                                           "planBaselineShown3Action", "planBaselineShown4Action", "planBaselineShown5Action",
                                           "planBaselineShown6Action", "planBaselineShown7Action", "planBaselineShown8Action",
                                           "planBaselineShown9Action", "planBaselineShown10Action"]

    readonly property var tabs: [
        StandardRibbon.fileTab([
            { id: "exchange", label: qsTr("Microsoft Project"), items: [
                { action: "planImportMspdiAction", size: "large" }, "planExportMspdiAction",
                "planExportCsvAction", "planExportXlsxAction", "planExportTimelineAction"] }],
            [], [{ action: "planExportPdfAction", iconName: "file-output" }, "planPageSetupAction"]),
        { id: "home", label: qsTr("Task"), keyTip: "T", groups: [
            { id: "taskView", label: qsTr("View"), items: [
                { action: "planViewGanttAction", size: "large", split: true, menu: views }] },
            StandardRibbon.clipboardGroup(),
            // Project's Schedule group: progress on top, the outline and
            // links below, icon-sized so the whole Task tab fits one line.
            { id: "schedule", label: qsTr("Schedule"), rows: [
                [{ action: "planMark0Action", size: "icon" }, { action: "planMark25Action", size: "icon" },
                 { action: "planMark50Action", size: "icon" }, { action: "planMark75Action", size: "icon" },
                 { action: "planMark100Action", size: "icon" },
                 { id: "planTracking", label: qsTr("Mark on Track"), iconName: "check-check", size: "icon",
                   menu: ["planMarkOnTrackAction", "planUpdateTasksAction"] },
                 "planRespectLinksAction"],
                [{ action: "planOutdentAction", size: "icon" }, { action: "planIndentAction", size: "icon" },
                 { action: "planLinkAction", size: "icon" }, { action: "planUnlinkAction", size: "icon" },
                 { action: "planMoveUpAction", size: "icon" }, { action: "planMoveDownAction", size: "icon" },
                 "planInactivateAction"]] },
            { id: "mode", label: qsTr("Tasks"), items: ["planManualScheduleAction", "planAutoScheduleAction"] },
            { id: "insert", label: qsTr("Insert"), items: [
                { action: "planInsertTaskAction", size: "large", split: true,
                  menu: ["planInsertTaskAction", "planInsertMilestoneAction", "planInsertSummaryAction"] },
                "planInsertMilestoneAction", "planInsertSummaryAction",
                "planDeleteTaskAction"] },
            { id: "properties", label: qsTr("Properties"), items: [
                { action: "planTaskInfoAction", size: "large" }, "planTaskNotesAction", "planScrollToTaskAction"] },
            StandardRibbon.historyGroup(),
            StandardRibbon.editingGroup([])
        ] },
        { id: "resource", label: qsTr("Resource"), keyTip: "R", groups: [
            { id: "assignments", label: qsTr("Assignments"), items: [{ action: "planAssignResourcesAction", size: "large" }] },
            { id: "pool", label: qsTr("Resource pool"), items: [
                { action: "planViewResourceSheetAction", size: "large" }, "planViewResourceUsageAction", "planUsageWeeksAction"] },
            { id: "insertResource", label: qsTr("Insert"), items: [
                { action: "planAddResourceAction", size: "large" }, "planAddMaterialAction", "planAddCostResourceAction",
                "planDeleteResourceAction"] },
            { id: "resourceProperties", label: qsTr("Properties"), items: [
                { action: "planResourceInfoAction", size: "large" }, "planResourceCalendarAction"] },
            { id: "level", label: qsTr("Level"), items: [
                { action: "planLevelAllAction", size: "large" }, "planLevelSelectionAction", "planLevelResourceAction",
                "planLevelingOptionsAction",
                { id: "planClearLeveling", label: qsTr("Clear Leveling"), iconName: "eraser",
                  menu: ["planClearLevelingAction", "planClearLevelingSelectedAction"] },
                "planNextOverallocationAction"] }
        ] },
        { id: "report", label: qsTr("Report"), keyTip: "P", groups: [
            { id: "reports", label: qsTr("Reports"), items: [
                { action: "planReportOverviewAction", size: "large" }, "planReportCostAction", "planReportWorkAction",
                "planReportCriticalAction", "planReportResourcesAction", "planReportLateAction"] },
            { id: "output", label: qsTr("Share"), items: [
                { action: "planExportPdfAction", size: "large" }, "filePrintAction", "filePrintPreviewAction",
                "planExportTimelineAction", "planExportCsvAction", "planExportXlsxAction"] }
        ] },
        { id: "project", label: qsTr("Project"), keyTip: "J", groups: [
            { id: "projectProperties", label: qsTr("Properties"), items: [
                { action: "planProjectInfoAction", size: "large" }, "planWorkingTimeAction"] },
            { id: "projectSchedule", label: qsTr("Schedule"), items: [
                { action: "planShowCriticalAction", size: "large" }, "planRecalculateAction"] },
            { id: "status", label: qsTr("Status"), items: [
                { action: "planStatusDateAction", size: "large" }, "planUpdateProjectAction"] },
            { id: "baseline", label: qsTr("Baseline"), items: [
                { action: "planSetBaselineAction", size: "large" }, "planClearBaselineAction"] }
        ] },
        StandardRibbon.viewTab([
            { id: "views", label: qsTr("Views"), items: [
                { action: "planViewGanttAction", size: "large", split: true, menu: views },
                "planViewTrackingGanttAction", "planViewNetworkAction", "planViewTaskUsageAction"] },
            { id: "data", label: qsTr("Data"), items: [
                { id: "planTables", label: qsTr("Tables"), iconName: "table", size: "large", menu: tables },
                { id: "planFilter", label: qsTr("Filter"), iconName: "funnel", menu: filters },
                { id: "planSort", label: qsTr("Sort"), iconName: "arrow-up-down", menu: sorts }] },
            { id: "timescale", label: qsTr("Timescale"), items: [
                { id: "planTimescale", label: qsTr("Timescale"), iconName: "calendar-range", size: "large",
                  menu: ["planTimescaleDaysAction", "planTimescaleWeeksAction", "planTimescaleMonthsAction"] },
                "planGoToTodayAction"] },
            { id: "outline", label: qsTr("Outline"), items: ["planToggleSubtasksAction", "planExpandAllAction"] },
            { id: "show", label: qsTr("Show"), items: [
                { action: "planShowTimelineAction", size: "large" }, "planShowCriticalAction", "planShowBaselineAction"] },
            StandardRibbon.assistantGroup([])
        ]),
        { id: "ganttFormat", label: qsTr("Gantt Chart Format"), keyTip: "G", contextId: "ganttFormat", groups: [
            { id: "barStyles", label: qsTr("Bar styles"), items: [
                { action: "planShowCriticalAction", size: "large" }, "planShowSlackAction",
                { action: "planShowBaselineAction", split: true, menu: baselinesShown }, "planShowBarTextAction"] },
            { id: "formatTimescale", label: qsTr("Timescale"), items: [
                { action: "planTimescaleWeeksAction", size: "large" }, "planTimescaleDaysAction",
                "planTimescaleMonthsAction", "viewZoomInAction", "viewZoomOutAction"] },
            { id: "formatShow", label: qsTr("Show"), items: [{ action: "planShowTimelineAction", size: "large" }] }
        ] }
    ]
}
