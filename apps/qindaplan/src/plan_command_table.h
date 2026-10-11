// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The QindaPlan command table (rule 5: ids are forever). `group` is nullptr
// for a plain command, "" for a checkable toggle and a radio-group name for
// a radio member. Icons are QindaTK Lucide names (the ribbon gate checks).

#include <QtGlobal>

#include <span>

namespace QindaOffice::Apps::Plan {

struct PlanCommand {
    const char *id;
    const char *text;
    const char *shortcut;
    const char *icon;
    const char *group;
};

inline std::span<const PlanCommand> planCommands()
{
    static constexpr PlanCommand kCommands[] = {
        // Views (radio, Ctrl+1…6; PlanHost::viewKeys order).
        {"planViewGanttAction", QT_TRANSLATE_NOOP("PlanHost", "&Gantt Chart"), "Ctrl+1", "chart-bar", "planView"},
        {"planViewTaskSheetAction", QT_TRANSLATE_NOOP("PlanHost", "Task &Sheet"), "Ctrl+2", "table", "planView"},
        {"planViewResourceSheetAction", QT_TRANSLATE_NOOP("PlanHost", "&Resource Sheet"), "Ctrl+3", "users", "planView"},
        {"planViewResourceUsageAction", QT_TRANSLATE_NOOP("PlanHost", "Resource &Usage"), "Ctrl+4", "chart-column", "planView"},
        {"planViewCalendarAction", QT_TRANSLATE_NOOP("PlanHost", "&Calendar"), "Ctrl+5", "calendar-days", "planView"},
        {"planViewReportsAction", QT_TRANSLATE_NOOP("PlanHost", "Re&ports"), "Ctrl+6", "layout-dashboard", "planView"},
        // Task.
        {"planInsertTaskAction", QT_TRANSLATE_NOOP("PlanHost", "&Task"), "Ctrl+Ins", "list-plus", nullptr},
        {"planInsertMilestoneAction", QT_TRANSLATE_NOOP("PlanHost", "&Milestone"), "", "diamond", nullptr},
        {"planInsertSummaryAction", QT_TRANSLATE_NOOP("PlanHost", "&Summary Task"), "", "list-tree", nullptr},
        {"planDeleteTaskAction", QT_TRANSLATE_NOOP("PlanHost", "&Delete Task"), "", "trash-2", nullptr},
        {"planIndentAction", QT_TRANSLATE_NOOP("PlanHost", "&Indent Task"), "Alt+Shift+Right", "list-indent-increase", nullptr},
        {"planOutdentAction", QT_TRANSLATE_NOOP("PlanHost", "&Outdent Task"), "Alt+Shift+Left", "list-indent-decrease", nullptr},
        {"planMoveUpAction", QT_TRANSLATE_NOOP("PlanHost", "Move &Up"), "Alt+Shift+Up", "arrow-up", nullptr},
        {"planMoveDownAction", QT_TRANSLATE_NOOP("PlanHost", "Move Do&wn"), "Alt+Shift+Down", "arrow-down", nullptr},
        {"planLinkAction", QT_TRANSLATE_NOOP("PlanHost", "&Link Tasks"), "Ctrl+F2", "link", nullptr},
        {"planUnlinkAction", QT_TRANSLATE_NOOP("PlanHost", "U&nlink Tasks"), "Ctrl+Shift+F2", "unlink", nullptr},
        {"planMark0Action", QT_TRANSLATE_NOOP("PlanHost", "0% Complete"), "", "circle", nullptr},
        {"planMark25Action", QT_TRANSLATE_NOOP("PlanHost", "25% Complete"), "", "circle-dot", nullptr},
        {"planMark50Action", QT_TRANSLATE_NOOP("PlanHost", "50% Complete"), "", "contrast", nullptr},
        {"planMark75Action", QT_TRANSLATE_NOOP("PlanHost", "75% Complete"), "", "loader-circle", nullptr},
        {"planMark100Action", QT_TRANSLATE_NOOP("PlanHost", "100% Complete"), "", "circle-check", nullptr},
        {"planRespectLinksAction", QT_TRANSLATE_NOOP("PlanHost", "&Respect Links"), "", "route", nullptr},
        {"planInactivateAction", QT_TRANSLATE_NOOP("PlanHost", "Inacti&vate"), "", "circle-off", ""},
        {"planManualScheduleAction", QT_TRANSLATE_NOOP("PlanHost", "&Manually Schedule"), "", "pin", "planMode"},
        {"planAutoScheduleAction", QT_TRANSLATE_NOOP("PlanHost", "&Auto Schedule"), "", "refresh-cw", "planMode"},
        {"planTaskInfoAction", QT_TRANSLATE_NOOP("PlanHost", "Task &Information…"), "Shift+F2", "info", nullptr},
        {"planTaskNotesAction", QT_TRANSLATE_NOOP("PlanHost", "&Notes…"), "", "sticky-note", nullptr},
        {"planScrollToTaskAction", QT_TRANSLATE_NOOP("PlanHost", "Scroll to &Task"), "Ctrl+Shift+F5", "crosshair", nullptr},
        {"planToggleSubtasksAction", QT_TRANSLATE_NOOP("PlanHost", "Show/Hide &Subtasks"), "", "chevrons-down-up", nullptr},
        {"planExpandAllAction", QT_TRANSLATE_NOOP("PlanHost", "Show &All Subtasks"), "", "chevrons-up-down", nullptr},
        // Resource.
        {"planAssignResourcesAction", QT_TRANSLATE_NOOP("PlanHost", "&Assign Resources…"), "Alt+F10", "user-plus", nullptr},
        {"planAddResourceAction", QT_TRANSLATE_NOOP("PlanHost", "Add &Work Resource"), "", "user-round", nullptr},
        {"planAddMaterialAction", QT_TRANSLATE_NOOP("PlanHost", "Add &Material Resource"), "", "package", nullptr},
        // M79: Project's cost resources (an amount per assignment).
        {"planAddCostResourceAction", QT_TRANSLATE_NOOP("PlanHost", "Add C&ost Resource"), "", "circle-dollar-sign", nullptr},
        {"planDeleteResourceAction", QT_TRANSLATE_NOOP("PlanHost", "&Delete Resource"), "", "trash", nullptr},
        {"planResourceInfoAction", QT_TRANSLATE_NOOP("PlanHost", "Resource &Information…"), "", "contact", nullptr},
        {"planResourceCalendarAction", QT_TRANSLATE_NOOP("PlanHost", "Resource &Calendar…"), "", "calendar-range", nullptr},
        {"planLevelResourceAction", QT_TRANSLATE_NOOP("PlanHost", "&Level Resource…"), "", "scale", nullptr},
        {"planLevelAllAction", QT_TRANSLATE_NOOP("PlanHost", "Level &All"), "", "layers", nullptr},
        {"planClearLevelingAction", QT_TRANSLATE_NOOP("PlanHost", "&Clear Leveling"), "", "eraser", nullptr},
        {"planNextOverallocationAction", QT_TRANSLATE_NOOP("PlanHost", "&Next Overallocation"), "", "triangle-alert", nullptr},
        // Project.
        {"planProjectInfoAction", QT_TRANSLATE_NOOP("PlanHost", "Project &Information…"), "", "clipboard-list", nullptr},
        {"planWorkingTimeAction", QT_TRANSLATE_NOOP("PlanHost", "Change &Working Time…"), "", "calendar-clock", nullptr},
        {"planSetBaselineAction", QT_TRANSLATE_NOOP("PlanHost", "Set &Baseline…"), "", "baseline", nullptr},
        {"planClearBaselineAction", QT_TRANSLATE_NOOP("PlanHost", "Cl&ear Baseline…"), "", "circle-x", nullptr},
        {"planStatusDateAction", QT_TRANSLATE_NOOP("PlanHost", "&Status Date…"), "", "calendar-check", nullptr},
        {"planUpdateProjectAction", QT_TRANSLATE_NOOP("PlanHost", "&Update Project…"), "", "refresh-ccw", nullptr},
        {"planRecalculateAction", QT_TRANSLATE_NOOP("PlanHost", "Re&calculate"), "F9", "calculator", nullptr},
        // Report and files.
        {"planReportOverviewAction", QT_TRANSLATE_NOOP("PlanHost", "Project &Overview"), "", "layout-dashboard", nullptr},
        {"planReportCriticalAction", QT_TRANSLATE_NOOP("PlanHost", "&Critical Tasks"), "", "flag", nullptr},
        {"planReportResourcesAction", QT_TRANSLATE_NOOP("PlanHost", "&Resource Overview"), "", "users-round", nullptr},
        {"planReportLateAction", QT_TRANSLATE_NOOP("PlanHost", "&Late Tasks"), "", "timer", nullptr},
        {"planExportPdfAction", QT_TRANSLATE_NOOP("PlanHost", "Export &PDF…"), "", "file-output", nullptr},
        {"planExportCsvAction", QT_TRANSLATE_NOOP("PlanHost", "Export Task Sheet as &CSV…"), "", "file-spreadsheet", nullptr},
        // M63 (D-071): the task sheet with numbers and dates as numbers.
        {"planExportXlsxAction", QT_TRANSLATE_NOOP("PlanHost", "Export Task Sheet as E&xcel…"), "", "sheet", nullptr},
        {"planExportMspdiAction", QT_TRANSLATE_NOOP("PlanHost", "Export &Microsoft Project XML…"), "", "file-down", nullptr},
        {"planExportTimelineAction", QT_TRANSLATE_NOOP("PlanHost", "Export &Timeline Image…"), "", "image", nullptr},
        {"planImportMspdiAction", QT_TRANSLATE_NOOP("PlanHost", "&Import Microsoft Project XML…"), "", "file-input", nullptr},
        {"planPageSetupAction", QT_TRANSLATE_NOOP("PlanHost", "Gantt Page Se&tup…"), "", "sliders-horizontal", nullptr},
        // View: tables, timescale, what the Gantt shows.
        {"planTableEntryAction", QT_TRANSLATE_NOOP("PlanHost", "&Entry Table"), "", "table", "planTable"},
        {"planTableTrackingAction", QT_TRANSLATE_NOOP("PlanHost", "&Tracking Table"), "", "list-checks", "planTable"},
        {"planTableVarianceAction", QT_TRANSLATE_NOOP("PlanHost", "&Variance Table"), "", "trending-up", "planTable"},
        {"planTableCostAction", QT_TRANSLATE_NOOP("PlanHost", "&Cost Table"), "", "circle-dollar-sign", "planTable"},
        {"planTableScheduleAction", QT_TRANSLATE_NOOP("PlanHost", "&Schedule Table"), "", "clock", "planTable"},
        {"planTimescaleDaysAction", QT_TRANSLATE_NOOP("PlanHost", "&Days"), "", "calendar-days", "planTimescale"},
        {"planTimescaleWeeksAction", QT_TRANSLATE_NOOP("PlanHost", "&Weeks"), "", "calendar-range", "planTimescale"},
        {"planTimescaleMonthsAction", QT_TRANSLATE_NOOP("PlanHost", "&Months"), "", "calendar", "planTimescale"},
        {"planGoToTodayAction", QT_TRANSLATE_NOOP("PlanHost", "Go to &Today"), "", "navigation", nullptr},
        {"planShowTimelineAction", QT_TRANSLATE_NOOP("PlanHost", "&Timeline"), "", "gallery-horizontal-end", ""},
        {"planShowCriticalAction", QT_TRANSLATE_NOOP("PlanHost", "&Critical Path"), "", "waypoints", ""},
        {"planShowBaselineAction", QT_TRANSLATE_NOOP("PlanHost", "&Baseline Bars"), "", "baseline", ""},
        {"planShowBarTextAction", QT_TRANSLATE_NOOP("PlanHost", "&Resource Names on Bars"), "", "tag", ""},
        {"planUsageWeeksAction", QT_TRANSLATE_NOOP("PlanHost", "Usage by &Week"), "", "calendar-range", ""},
        // ---- M79 (D-088): Project's scheduling, tracking and views ----
        // More views in the planView group (no Ctrl+digit: Project has none).
        {"planViewTrackingGanttAction", QT_TRANSLATE_NOOP("PlanHost", "Trac&king Gantt"), "", "activity", "planView"},
        {"planViewLevelingGanttAction", QT_TRANSLATE_NOOP("PlanHost", "Le&veling Gantt"), "", "between-horizontal-start", "planView"},
        {"planViewNetworkAction", QT_TRANSLATE_NOOP("PlanHost", "&Network Diagram"), "", "workflow", "planView"},
        {"planViewTaskUsageAction", QT_TRANSLATE_NOOP("PlanHost", "Task Us&age"), "", "chart-column-big", "planView"},
        // Project's Work, Baseline and Delay tables.
        {"planTableWorkAction", QT_TRANSLATE_NOOP("PlanHost", "&Work Table"), "", "briefcase", "planTable"},
        {"planTableBaselineAction", QT_TRANSLATE_NOOP("PlanHost", "&Baseline Table"), "", "baseline", "planTable"},
        {"planTableDelayAction", QT_TRANSLATE_NOOP("PlanHost", "&Delay Table"), "", "timer", "planTable"},
        // View ▸ Filter (plan_views.h ids) and its related-summaries option.
        {"planFilterAllAction", QT_TRANSLATE_NOOP("PlanHost", "&All Tasks"), "", "list", "planFilter"},
        {"planFilterCriticalAction", QT_TRANSLATE_NOOP("PlanHost", "&Critical"), "", "waypoints", "planFilter"},
        {"planFilterIncompleteAction", QT_TRANSLATE_NOOP("PlanHost", "&Incomplete Tasks"), "", "circle-dot", "planFilter"},
        {"planFilterCompletedAction", QT_TRANSLATE_NOOP("PlanHost", "C&ompleted Tasks"), "", "circle-check", "planFilter"},
        {"planFilterLateAction", QT_TRANSLATE_NOOP("PlanHost", "&Late Tasks"), "", "timer", "planFilter"},
        {"planFilterSlippingAction", QT_TRANSLATE_NOOP("PlanHost", "&Slipping Tasks"), "", "trending-down", "planFilter"},
        {"planFilterMilestonesAction", QT_TRANSLATE_NOOP("PlanHost", "&Milestones"), "", "diamond", "planFilter"},
        {"planFilterSummaryAction", QT_TRANSLATE_NOOP("PlanHost", "S&ummary Tasks"), "", "list-tree", "planFilter"},
        {"planFilterDeadlinesAction", QT_TRANSLATE_NOOP("PlanHost", "Tasks With &Deadlines"), "", "flag", "planFilter"},
        {"planFilterResourceAction", QT_TRANSLATE_NOOP("PlanHost", "Using &Resource…"), "", "user", "planFilter"},
        {"planFilterDateRangeAction", QT_TRANSLATE_NOOP("PlanHost", "Date Ra&nge…"), "", "calendar-range", "planFilter"},
        {"planFilterRelatedSummariesAction", QT_TRANSLATE_NOOP("PlanHost", "Show Related &Summary Rows"), "", "rows-3", ""},
        // View ▸ Sort (outline kept, as Project's "Keep outline structure").
        {"planSortIdAction", QT_TRANSLATE_NOOP("PlanHost", "by &ID"), "", "hash", "planSort"},
        {"planSortStartAction", QT_TRANSLATE_NOOP("PlanHost", "by &Start Date"), "", "calendar-days", "planSort"},
        {"planSortFinishAction", QT_TRANSLATE_NOOP("PlanHost", "by &Finish Date"), "", "calendar-check", "planSort"},
        {"planSortPriorityAction", QT_TRANSLATE_NOOP("PlanHost", "by &Priority"), "", "star", "planSort"},
        {"planSortCostAction", QT_TRANSLATE_NOOP("PlanHost", "by &Cost"), "", "dollar-sign", "planSort"},
        // Which baseline the Gantt draws (Gantt Chart Format ▸ Baseline).
        {"planBaselineShown0Action", QT_TRANSLATE_NOOP("PlanHost", "&Baseline"), "", "baseline", "planBaselineShown"},
        {"planBaselineShown1Action", QT_TRANSLATE_NOOP("PlanHost", "Baseline &1"), "", "baseline", "planBaselineShown"},
        {"planBaselineShown2Action", QT_TRANSLATE_NOOP("PlanHost", "Baseline &2"), "", "baseline", "planBaselineShown"},
        {"planBaselineShown3Action", QT_TRANSLATE_NOOP("PlanHost", "Baseline &3"), "", "baseline", "planBaselineShown"},
        {"planBaselineShown4Action", QT_TRANSLATE_NOOP("PlanHost", "Baseline &4"), "", "baseline", "planBaselineShown"},
        {"planBaselineShown5Action", QT_TRANSLATE_NOOP("PlanHost", "Baseline &5"), "", "baseline", "planBaselineShown"},
        {"planBaselineShown6Action", QT_TRANSLATE_NOOP("PlanHost", "Baseline &6"), "", "baseline", "planBaselineShown"},
        {"planBaselineShown7Action", QT_TRANSLATE_NOOP("PlanHost", "Baseline &7"), "", "baseline", "planBaselineShown"},
        {"planBaselineShown8Action", QT_TRANSLATE_NOOP("PlanHost", "Baseline &8"), "", "baseline", "planBaselineShown"},
        {"planBaselineShown9Action", QT_TRANSLATE_NOOP("PlanHost", "Baseline &9"), "", "baseline", "planBaselineShown"},
        {"planBaselineShown10Action", QT_TRANSLATE_NOOP("PlanHost", "Baseline 1&0"), "", "baseline", "planBaselineShown"},
        {"planShowSlackAction", QT_TRANSLATE_NOOP("PlanHost", "&Slack"), "", "between-horizontal-end", ""},
        // Resource ▸ Level.
        {"planLevelingOptionsAction", QT_TRANSLATE_NOOP("PlanHost", "Leveling &Options…"), "", "settings-2", nullptr},
        {"planLevelSelectionAction", QT_TRANSLATE_NOOP("PlanHost", "Level &Selection"), "", "lasso-select", nullptr},
        {"planClearLevelingSelectedAction", QT_TRANSLATE_NOOP("PlanHost", "Clear Leveling of Selected &Tasks"), "", "eraser", nullptr},
        // Task ▸ tracking.
        {"planUpdateTasksAction", QT_TRANSLATE_NOOP("PlanHost", "&Update Tasks…"), "", "list-todo", nullptr},
        {"planMarkOnTrackAction", QT_TRANSLATE_NOOP("PlanHost", "Mark on &Track"), "", "check-check", nullptr},
        // Report ▸ Dashboards.
        {"planReportCostAction", QT_TRANSLATE_NOOP("PlanHost", "C&ost Overview"), "", "circle-dollar-sign", nullptr},
        {"planReportWorkAction", QT_TRANSLATE_NOOP("PlanHost", "&Work Overview"), "", "briefcase", nullptr},
        // ---- M83 (D-092): task types, custom fields, schedule tools ----
        // Task ▸ Task Type (the selection's; Project's Task Information).
        {"planTaskTypeFixedUnitsAction", QT_TRANSLATE_NOOP("PlanHost", "Fixed &Units"), "", "users", "planTaskType"},
        {"planTaskTypeFixedDurationAction", QT_TRANSLATE_NOOP("PlanHost", "Fixed &Duration"), "", "clock", "planTaskType"},
        {"planTaskTypeFixedWorkAction", QT_TRANSLATE_NOOP("PlanHost", "Fixed &Work"), "", "briefcase", "planTaskType"},
        {"planEffortDrivenAction", QT_TRANSLATE_NOOP("PlanHost", "&Effort Driven"), "", "scale", ""},
        // Task ▸ Schedule and Insert.
        {"planSplitTaskAction", QT_TRANSLATE_NOOP("PlanHost", "S&plit Task…"), "", "scissors", nullptr},
        {"planRemoveSplitsAction", QT_TRANSLATE_NOOP("PlanHost", "Remove S&plits"), "", "merge", nullptr},
        {"planInsertRecurringAction", QT_TRANSLATE_NOOP("PlanHost", "&Recurring Task…"), "", "repeat", nullptr},
        {"planRecurringInfoAction", QT_TRANSLATE_NOOP("PlanHost", "Recurring Task In&formation…"), "", "list-restart", nullptr},
        // Project.
        {"planCustomFieldsAction", QT_TRANSLATE_NOOP("PlanHost", "Custom &Fields…"), "", "text-cursor-input", nullptr},
        {"planRescheduleWorkAction", QT_TRANSLATE_NOOP("PlanHost", "Reschedule Uncompleted &Work…"), "", "history", nullptr},
        // View: the custom fields as a table, filtered and sorted by one.
        {"planTableCustomAction", QT_TRANSLATE_NOOP("PlanHost", "C&ustom Fields Table"), "", "columns-3", "planTable"},
        {"planFilterCustomAction", QT_TRANSLATE_NOOP("PlanHost", "Custom &Field…"), "", "funnel", "planFilter"},
        {"planSortCustomAction", QT_TRANSLATE_NOOP("PlanHost", "by Custom &Field…"), "", "tag", "planSort"},
        {"planShowProgressLinesAction", QT_TRANSLATE_NOOP("PlanHost", "&Progress Lines"), "", "chart-line", ""},
        // File ▸ Import and Export: Project's Import Wizard for tasks.
        {"planImportTasksAction", QT_TRANSLATE_NOOP("PlanHost", "Import &Tasks from Excel or CSV…"), "", "import", nullptr},
        // ---- M89 (D-097): QindaTK's group rows and planning bars ----
        // View ▸ Group By (plan_groups.h ids; Custom Field… asks first).
        {"planGroupNoneAction", QT_TRANSLATE_NOOP("PlanHost", "&No Group"), "", "list", "planGroup"},
        {"planGroupCompleteAction", QT_TRANSLATE_NOOP("PlanHost", "&Complete and Incomplete Tasks"), "", "circle-check", "planGroup"},
        {"planGroupConstraintAction", QT_TRANSLATE_NOOP("PlanHost", "Constraint &Type"), "", "calendar-clock", "planGroup"},
        {"planGroupCriticalAction", QT_TRANSLATE_NOOP("PlanHost", "C&ritical"), "", "waypoints", "planGroup"},
        {"planGroupDurationAction", QT_TRANSLATE_NOOP("PlanHost", "&Duration"), "", "clock", "planGroup"},
        {"planGroupMilestonesAction", QT_TRANSLATE_NOOP("PlanHost", "&Milestones"), "", "diamond", "planGroup"},
        {"planGroupPriorityAction", QT_TRANSLATE_NOOP("PlanHost", "&Priority"), "", "star", "planGroup"},
        {"planGroupResourceAction", QT_TRANSLATE_NOOP("PlanHost", "&Resource Names"), "", "users", "planGroup"},
        {"planGroupStatusAction", QT_TRANSLATE_NOOP("PlanHost", "&Status"), "", "activity", "planGroup"},
        {"planGroupModeAction", QT_TRANSLATE_NOOP("PlanHost", "&Auto v. Manually Scheduled"), "", "pin", "planGroup"},
        {"planGroupActiveAction", QT_TRANSLATE_NOOP("PlanHost", "Active v. &Inactive"), "", "circle-off", "planGroup"},
        {"planGroupCustomAction", QT_TRANSLATE_NOOP("PlanHost", "Custom &Field…"), "", "tag", "planGroup"},
        // View ▸ Highlight: the filter's tasks marked, the others kept.
        {"planHighlightNoneAction", QT_TRANSLATE_NOOP("PlanHost", "&No Highlight"), "", "list", "planHighlight"},
        {"planHighlightCriticalAction", QT_TRANSLATE_NOOP("PlanHost", "&Critical"), "", "waypoints", "planHighlight"},
        {"planHighlightIncompleteAction", QT_TRANSLATE_NOOP("PlanHost", "&Incomplete Tasks"), "", "circle-dot", "planHighlight"},
        {"planHighlightCompletedAction", QT_TRANSLATE_NOOP("PlanHost", "C&ompleted Tasks"), "", "circle-check", "planHighlight"},
        {"planHighlightLateAction", QT_TRANSLATE_NOOP("PlanHost", "&Late Tasks"), "", "timer", "planHighlight"},
        {"planHighlightSlippingAction", QT_TRANSLATE_NOOP("PlanHost", "&Slipping Tasks"), "", "trending-down", "planHighlight"},
        {"planHighlightMilestonesAction", QT_TRANSLATE_NOOP("PlanHost", "&Milestones"), "", "diamond", "planHighlight"},
        {"planHighlightSummaryAction", QT_TRANSLATE_NOOP("PlanHost", "S&ummary Tasks"), "", "list-tree", "planHighlight"},
        {"planHighlightDeadlinesAction", QT_TRANSLATE_NOOP("PlanHost", "Tasks With &Deadlines"), "", "flag", "planHighlight"},
        // Gantt Chart Format ▸ a second baseline lane under the first.
        {"planShowBaseline2Action", QT_TRANSLATE_NOOP("PlanHost", "&Second Baseline"), "", "baseline", ""},
        {"planBaseline2Shown0Action", QT_TRANSLATE_NOOP("PlanHost", "&Baseline"), "", "baseline", "planBaseline2Shown"},
        {"planBaseline2Shown1Action", QT_TRANSLATE_NOOP("PlanHost", "Baseline &1"), "", "baseline", "planBaseline2Shown"},
        {"planBaseline2Shown2Action", QT_TRANSLATE_NOOP("PlanHost", "Baseline &2"), "", "baseline", "planBaseline2Shown"},
        {"planBaseline2Shown3Action", QT_TRANSLATE_NOOP("PlanHost", "Baseline &3"), "", "baseline", "planBaseline2Shown"},
        {"planBaseline2Shown4Action", QT_TRANSLATE_NOOP("PlanHost", "Baseline &4"), "", "baseline", "planBaseline2Shown"},
        {"planBaseline2Shown5Action", QT_TRANSLATE_NOOP("PlanHost", "Baseline &5"), "", "baseline", "planBaseline2Shown"},
        {"planBaseline2Shown6Action", QT_TRANSLATE_NOOP("PlanHost", "Baseline &6"), "", "baseline", "planBaseline2Shown"},
        {"planBaseline2Shown7Action", QT_TRANSLATE_NOOP("PlanHost", "Baseline &7"), "", "baseline", "planBaseline2Shown"},
        {"planBaseline2Shown8Action", QT_TRANSLATE_NOOP("PlanHost", "Baseline &8"), "", "baseline", "planBaseline2Shown"},
        {"planBaseline2Shown9Action", QT_TRANSLATE_NOOP("PlanHost", "Baseline &9"), "", "baseline", "planBaseline2Shown"},
        {"planBaseline2Shown10Action", QT_TRANSLATE_NOOP("PlanHost", "Baseline 1&0"), "", "baseline", "planBaseline2Shown"},
    };
    return kCommands;
}

} // namespace QindaOffice::Apps::Plan
