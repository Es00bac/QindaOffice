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
    };
    return kCommands;
}

} // namespace QindaOffice::Apps::Plan
