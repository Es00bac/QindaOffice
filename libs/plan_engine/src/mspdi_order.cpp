// SPDX-License-Identifier: GPL-3.0-or-later
// The order Microsoft Project writes MSPDI children in (mspdi_keep.h).
//
// AGENT-NOTE (source of truth): the lists are the children of Project,
// Calendar, Task, Resource and Assignment as Microsoft Project 2019 writes
// them (MPXJ's junit/data/generated/*-project2019-mspdi.xml, merged per
// object), with the elements those files do not use placed where the MSPDI
// schema (mspdi_pj15.xsd, as MPXJ's JAXB classes list it) puts them. Where
// the two disagree, Project's own output wins: it is what Project reads
// back. Do not reorder an entry without such evidence (D-071).
#include "mspdi_keep.h"

#include <QHash>

namespace QindaOffice::Plan::Mspdi {

namespace {

const char *const kProject[] = {
    "SaveVersion", "BuildNumber", "UID", "Name", "GUID", "Title", "Subject", "Category", "Company", "Manager",
    "Author", "CreationDate", "Revision", "LastSaved", "ScheduleFromStart", "StartDate", "FinishDate", "FYStartDate",
    "CriticalSlackLimit", "CurrencyDigits", "CurrencySymbol", "CurrencyCode", "CurrencySymbolPosition", "CalendarUID",
    "DefaultStartTime", "DefaultFinishTime", "MinutesPerDay", "MinutesPerWeek", "DaysPerMonth", "DefaultTaskType",
    "DefaultFixedCostAccrual", "DefaultStandardRate", "DefaultOvertimeRate", "DurationFormat", "WorkFormat",
    "EditableActualCosts", "HonorConstraints", "EarnedValueMethod", "InsertedProjectsLikeSummary",
    "MultipleCriticalPaths", "NewTasksEffortDriven", "NewTasksEstimated", "SplitsInProgressTasks", "SpreadActualCost",
    "SpreadPercentComplete", "TaskUpdatesResource", "FiscalYearStart", "WeekStartDay", "MoveCompletedEndsBack",
    "MoveRemainingStartsBack", "MoveRemainingStartsForward", "MoveCompletedEndsForward", "BaselineForEarnedValue",
    "AutoAddNewResourcesAndTasks", "StatusDate", "CurrentDate", "MicrosoftProjectServerURL", "Autolink",
    "NewTaskStartDate", "NewTasksAreManual", "DefaultTaskEVMethod", "ProjectExternallyEdited", "ExtendedCreationDate",
    "ActualsInSync", "RemoveFileProperties", "AdminProject", "BaselineCalendar",
    "UpdateManuallyScheduledTasksWhenEditingLinks", "KeepTaskOnNearestWorkingTimeWhenMadeAutoScheduled", "Views",
    "Filters", "Groups", "Tables", "Maps", "Reports", "Drawings", "DataLinks", "VBAProjects", "OutlineCodes",
    "WBSMasks", "ExtendedAttributes", "Calendars", "Tasks", "Resources", "Assignments",
};

const char *const kCalendar[] = {
    "UID", "GUID", "Name", "IsBaseCalendar", "IsBaselineCalendar", "BaseCalendarUID", "WeekDays", "Exceptions",
    "WorkWeeks",
};

const char *const kTask[] = {
    "UID", "GUID", "ID", "Name", "Active", "Manual", "Type", "IsNull", "CreateDate", "Contact", "WBS", "WBSLevel",
    "OutlineNumber", "OutlineLevel", "Priority", "StartText", "FinishText", "DurationText", "Start", "Finish",
    "Duration", "ManualStart", "ManualFinish", "ManualDuration", "DurationFormat", "FreeformDurationFormat", "Work",
    "Stop", "Resume", "ResumeValid", "EffortDriven", "Recurring", "OverAllocated", "Estimated", "Milestone", "Summary",
    "DisplayAsSummary", "Critical", "IsSubproject", "IsSubprojectReadOnly", "SubprojectName", "ExternalTask",
    "ExternalTaskProject", "EarlyStart", "EarlyFinish", "LateStart", "LateFinish", "StartVariance", "FinishVariance",
    "WorkVariance", "FreeSlack", "TotalSlack", "StartSlack", "FinishSlack", "FixedCost", "FixedCostAccrual",
    "PercentComplete", "PercentWorkComplete", "Cost", "OvertimeCost", "OvertimeWork", "ActualStart", "ActualFinish",
    "ActualDuration", "ActualCost", "ActualOvertimeCost", "ActualWork", "ActualOvertimeWork", "RegularWork",
    "RemainingDuration", "RemainingCost", "RemainingWork", "RemainingOvertimeCost", "RemainingOvertimeWork", "ACWP",
    "CV", "ConstraintType", "CalendarUID", "ConstraintDate", "Deadline", "LevelAssignments", "LevelingCanSplit",
    "LevelingDelay", "LevelingDelayFormat", "PreLeveledStart", "PreLeveledFinish", "Hyperlink", "HyperlinkAddress",
    "HyperlinkSubAddress", "IgnoreResourceCalendar", "Notes", "HideBar", "Rollup", "BCWS", "BCWP",
    "PhysicalPercentComplete", "EarnedValueMethod", "PredecessorLink", "ActualWorkProtected",
    "ActualOvertimeWorkProtected", "IsPublished", "StatusManager", "CommitmentStart", "CommitmentFinish",
    "CommitmentType", "ExtendedAttribute", "Baseline", "OutlineCode", "TimephasedData",
};

const char *const kResource[] = {
    "UID", "GUID", "ID", "Name", "Type", "IsNull", "Initials", "Phonetics", "NTAccount", "MaterialLabel", "Code",
    "Group", "WorkGroup", "EmailAddress", "Hyperlink", "HyperlinkAddress", "HyperlinkSubAddress", "MaxUnits",
    "PeakUnits", "OverAllocated", "AvailableFrom", "AvailableTo", "Start", "Finish", "CanLevel", "AccrueAt", "Work",
    "RegularWork", "OvertimeWork", "ActualWork", "RemainingWork", "ActualOvertimeWork", "RemainingOvertimeWork",
    "PercentWorkComplete", "StandardRate", "StandardRateFormat", "Cost", "OvertimeRate", "OvertimeRateFormat",
    "OvertimeCost", "CostPerUse", "ActualCost", "ActualOvertimeCost", "RemainingCost", "RemainingOvertimeCost",
    "WorkVariance", "CostVariance", "SV", "CV", "ACWP", "CalendarUID", "Notes", "BCWS", "BCWP", "IsGeneric",
    "IsInactive", "IsEnterprise", "BookingType", "ActualWorkProtected", "ActualOvertimeWorkProtected",
    "ActiveDirectoryGUID", "CreationDate", "IsCostResource", "IsBudget", "ExtendedAttribute", "Baseline",
    "OutlineCode", "CostCenter", "AssnOwner", "AssnOwnerGuid", "AvailabilityPeriods", "Rates", "TimephasedData",
};

const char *const kAssignment[] = {
    "UID", "GUID", "TaskUID", "ResourceUID", "PercentWorkComplete", "ActualCost", "ActualFinish", "ActualOvertimeCost",
    "ActualOvertimeWork", "ActualStart", "ActualWork", "ACWP", "Confirmed", "Cost", "CostRateTable", "RateScale",
    "CostVariance", "CV", "Delay", "Finish", "FinishVariance", "Hyperlink", "HyperlinkAddress", "HyperlinkSubAddress",
    "WorkVariance", "HasFixedRateUnits", "FixedMaterial", "LevelingDelay", "LevelingDelayFormat", "LinkedFields",
    "Milestone", "Notes", "Overallocated", "OvertimeCost", "OvertimeWork", "PeakUnits", "RegularWork",
    "RemainingCost", "RemainingOvertimeCost", "RemainingOvertimeWork", "RemainingWork", "ResponsePending", "Start",
    "Stop", "Resume", "StartVariance", "Summary", "SV", "Units", "UpdateNeeded", "VAC", "Work", "WorkContour", "BCWS",
    "BCWP", "BookingType", "ActualWorkProtected", "ActualOvertimeWorkProtected", "CreationDate", "AssnOwner",
    "AssnOwnerGuid", "BudgetCost", "BudgetWork", "ExtendedAttribute", "Baseline", "TimephasedData",
};

template<std::size_t N>
QHash<QString, int> indexOf(const char *const (&names)[N])
{
    QHash<QString, int> out;
    for (std::size_t i = 0; i < N; ++i) {
        out.insert(QString::fromLatin1(names[i]), int(i));
    }
    return out;
}

} // namespace

int schemaRank(Kind kind, const QString &element)
{
    static const QHash<QString, int> project = indexOf(kProject);
    static const QHash<QString, int> calendar = indexOf(kCalendar);
    static const QHash<QString, int> task = indexOf(kTask);
    static const QHash<QString, int> resource = indexOf(kResource);
    static const QHash<QString, int> assignment = indexOf(kAssignment);
    switch (kind) {
    case Kind::Project: return project.value(element, -1);
    case Kind::Calendar: return calendar.value(element, -1);
    case Kind::Task: return task.value(element, -1);
    case Kind::Resource: return resource.value(element, -1);
    case Kind::Assignment: return assignment.value(element, -1);
    }
    return -1;
}

bool isDerived(Kind kind, const QString &element)
{
    // AGENT-NOTE: values Project derives from the dates, work and progress
    // QindaPlan edits. They travel only while the object's stamp holds
    // (mspdi_keep.h); stale ones would contradict the new schedule. Since
    // M79 a task's LevelingDelay, LevelingDelayFormat, PreLeveledStart and
    // PreLeveledFinish are modelled (the reader never keeps them); the
    // entries below still govern an assignment's own leveling delay.
    static const QStringList common{
        QStringLiteral("TimephasedData"), QStringLiteral("ActualWork"), QStringLiteral("RemainingWork"),
        QStringLiteral("ActualCost"), QStringLiteral("RemainingCost"), QStringLiteral("PercentWorkComplete"),
        QStringLiteral("ActualOvertimeWork"), QStringLiteral("ActualOvertimeCost"), QStringLiteral("RemainingOvertimeWork"),
        QStringLiteral("RemainingOvertimeCost"), QStringLiteral("RegularWork"), QStringLiteral("OvertimeWork"),
        QStringLiteral("OvertimeCost"), QStringLiteral("WorkVariance"), QStringLiteral("CostVariance"),
        QStringLiteral("SV"), QStringLiteral("CV"), QStringLiteral("VAC"), QStringLiteral("ACWP"), QStringLiteral("BCWS"),
        QStringLiteral("BCWP"), QStringLiteral("Stop"), QStringLiteral("Resume"), QStringLiteral("PeakUnits"),
        QStringLiteral("LevelingDelay"), QStringLiteral("LevelingDelayFormat")};
    if (kind == Kind::Project || kind == Kind::Calendar) {
        return false;
    }
    if (common.contains(element)) {
        return true;
    }
    switch (kind) {
    case Kind::Task:
        return element == QLatin1String("ActualDuration") || element == QLatin1String("RemainingDuration")
            || element == QLatin1String("ResumeValid") || element == QLatin1String("StartVariance")
            || element == QLatin1String("FinishVariance") || element == QLatin1String("StartSlack")
            || element == QLatin1String("FinishSlack");
    case Kind::Resource:
        return element == QLatin1String("Start") || element == QLatin1String("Finish") || element == QLatin1String("Rates");
    case Kind::Assignment:
        return element == QLatin1String("ActualStart") || element == QLatin1String("ActualFinish")
            || element == QLatin1String("StartVariance") || element == QLatin1String("FinishVariance")
            || element == QLatin1String("Overallocated") || element == QLatin1String("Milestone")
            || element == QLatin1String("Summary");
    case Kind::Project:
    case Kind::Calendar: break;
    }
    return false;
}

} // namespace QindaOffice::Plan::Mspdi
