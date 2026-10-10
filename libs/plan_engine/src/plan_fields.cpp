// SPDX-License-Identifier: GPL-3.0-or-later
// The task sheet's fields as text (plan_edit.h).
#include "baselines.h"
#include "custom_fields.h"
#include "leveling.h"
#include "plan_edit.h"
#include "plan_fields_p.h"
#include "plan_time.h"
#include "scheduler.h"
#include "task_types.h"
#include "work_calendar.h"


namespace QindaOffice::Plan {

namespace {

struct FieldInfo {
    const char *id;
    const char *title;
    bool editable;
};

constexpr FieldInfo kTaskFields[] = {
    {"id", "ID", false},
    {"wbs", "WBS", false},
    {"indicators", "Indicators", false},
    {"mode", "Mode", true},
    {"name", "Task Name", true},
    {"duration", "Duration", true},
    {"start", "Start", true},
    {"finish", "Finish", true},
    {"predecessors", "Predecessors", true},
    {"successors", "Successors", false},
    {"resources", "Resource Names", true},
    {"percent", "% Complete", true},
    {"work", "Work", true}, // M83: typed, Project's task-type rules apply
    {"cost", "Cost", false},
    {"fixedCost", "Fixed Cost", true},
    {"actualCost", "Actual Cost", true}, // M83: when the project lets it be typed
    {"remainingCost", "Remaining Cost", false},
    {"notes", "Notes", true},
    {"constraint", "Constraint Type", true},
    {"constraintDate", "Constraint Date", true},
    // M63: Project's Priority and Deadline (Microsoft Project XML keeps them).
    {"priority", "Priority", true},
    {"deadline", "Deadline", true},
    {"actualStart", "Actual Start", true},
    {"actualFinish", "Actual Finish", true},
    {"baselineStart", "Baseline Start", false},
    {"baselineFinish", "Baseline Finish", false},
    {"baselineDuration", "Baseline Duration", false},
    {"baselineCost", "Baseline Cost", false},
    {"startVariance", "Start Var.", false},
    {"finishVariance", "Finish Var.", false},
    {"durationVariance", "Duration Var.", false},
    {"costVariance", "Cost Var.", false},
    {"totalSlack", "Total Slack", false},
    {"freeSlack", "Free Slack", false},
    {"earlyStart", "Early Start", false},
    {"earlyFinish", "Early Finish", false},
    {"lateStart", "Late Start", false},
    {"lateFinish", "Late Finish", false},
    {"active", "Active", true},
    {"milestone", "Milestone", true},
    {"calendar", "Task Calendar", true},
    {"critical", "Critical", false},
    // M79: tracking by work and duration, leveling (Project's Tracking,
    // Work, Delay and Baseline tables).
    {"actualDuration", "Act. Dur.", true},
    {"remainingDuration", "Rem. Dur.", true},
    {"actualWork", "Act. Work", true}, // M83
    {"remainingWork", "Rem. Work", false},
    {"baselineWork", "Baseline Work", false},
    {"workVariance", "Work Var.", false},
    {"levelingDelay", "Leveling Delay", true},
    {"preLeveledStart", "Preleveled Start", false},
    {"preLeveledFinish", "Preleveled Finish", false},
    // M83 (D-092): Project's task type and effort driving (task_types.h).
    {"type", "Type", true},
    {"effortDriven", "Effort Driven", true},
};

const FieldInfo *taskField(const QString &id)
{
    for (const FieldInfo &f : kTaskFields) {
        if (id == QLatin1String(f.id)) {
            return &f;
        }
    }
    return nullptr;
}

QString yesNo(bool value)
{
    return value ? QStringLiteral("Yes") : QStringLiteral("No");
}

QString calendarName(const Project &project, int uid)
{
    const int c = project.calendarIndex(uid);
    return c >= 0 ? project.calendars.at(c).name : QString();
}

// The date and, when it is not the day's default, the time.
QString editDate(Minutes m, int defaultMinute, const QLocale &locale)
{
    if (m == kNoDate) {
        return {};
    }
    return minuteOfDay(m) == defaultMinute ? formatDate(m, locale) : formatDateTime(m, locale);
}

} // namespace

QStringList taskFieldIds()
{
    QStringList out;
    for (const FieldInfo &f : kTaskFields) {
        out.append(QLatin1String(f.id));
    }
    return out;
}

bool isTaskFieldEditable(const QString &field)
{
    const FieldInfo *f = taskField(field);
    return (f != nullptr && f->editable) || isCustomField(field); // M83: Text1 … Finish10
}

QString taskFieldTitle(const QString &field)
{
    const FieldInfo *f = taskField(field);
    return f != nullptr ? QString::fromUtf8(f->title) : field;
}

QString taskFieldText(const Project &project, int index, const QString &field, const QLocale &locale)
{
    if (index < 0 || index >= project.tasks.size()) {
        return {};
    }
    const Task &t = project.tasks.at(index);
    const DurationFormat fmt = durationFormatOf(project.info);
    const QString &cur = project.info.currency;
    const auto baseline = [&]() -> const BaselineData & { return t.baselines[0]; };
    if (field == QLatin1String("id")) {
        return QString::number(t.id);
    }
    if (field == QLatin1String("wbs")) {
        return t.wbs;
    }
    if (field == QLatin1String("indicators")) {
        QStringList flags;
        if (t.constraintConflict || !t.warning.isEmpty()) {
            flags << QStringLiteral("!");
        }
        if (t.overallocated) {
            flags << QStringLiteral("overallocated");
        }
        if (t.deadline != kNoDate && t.finish != kNoDate && t.finish > t.deadline && t.percentComplete < 100) {
            flags << QStringLiteral("missed-deadline");
        }
        if (t.percentComplete >= 100) {
            flags << QStringLiteral("done");
        }
        if (!t.notes.isEmpty()) {
            flags << QStringLiteral("note");
        }
        if (!t.splits.isEmpty()) {
            flags << QStringLiteral("split"); // M83
        }
        if (t.recurring) {
            flags << QStringLiteral("recurring"); // M83
        }
        return flags.join(QLatin1Char(' '));
    }
    if (field == QLatin1String("mode")) {
        return t.manual ? QStringLiteral("Manual") : QStringLiteral("Auto");
    }
    if (field == QLatin1String("name")) {
        return t.name;
    }
    if (field == QLatin1String("duration")) {
        return taskDurationText(t, fmt, locale); // M83: elapsed durations read "2 edays"
    }
    if (field == QLatin1String("start")) {
        return formatDate(t.start, locale);
    }
    if (field == QLatin1String("finish")) {
        return formatDate(t.finish, locale);
    }
    if (field == QLatin1String("predecessors")) {
        return predecessorsText(project, index, locale);
    }
    if (field == QLatin1String("successors")) {
        return successorsText(project, index, locale);
    }
    if (field == QLatin1String("resources")) {
        return resourceNamesText(project, index, locale);
    }
    if (field == QLatin1String("percent")) {
        return locale.toString(t.percentComplete) + QLatin1Char('%');
    }
    if (field == QLatin1String("work")) {
        return formatWork(t.work, locale);
    }
    if (field == QLatin1String("cost")) {
        return formatMoney(t.cost, cur, locale);
    }
    if (field == QLatin1String("fixedCost")) {
        return formatMoney(t.fixedCost, cur, locale);
    }
    if (field == QLatin1String("actualCost")) {
        return formatMoney(actualCostOf(project, t), cur, locale); // M83: typed when allowed
    }
    if (field == QLatin1String("remainingCost")) {
        return formatMoney(t.cost - actualCostOf(project, t), cur, locale);
    }
    if (field == QLatin1String("notes")) {
        return t.notes.section(QLatin1Char('\n'), 0, 0);
    }
    if (field == QLatin1String("constraint")) {
        return constraintName(t.constraint);
    }
    if (field == QLatin1String("constraintDate")) {
        return constraintNeedsDate(t.constraint) ? formatDate(t.constraintDate, locale) : QStringLiteral("NA");
    }
    if (field == QLatin1String("priority")) {
        return locale.toString(t.priority);
    }
    if (field == QLatin1String("deadline")) {
        return t.deadline == kNoDate ? QStringLiteral("NA") : formatDate(t.deadline, locale);
    }
    if (field == QLatin1String("actualStart")) {
        return t.actualStart == kNoDate ? QStringLiteral("NA") : formatDate(t.actualStart, locale);
    }
    if (field == QLatin1String("actualFinish")) {
        return t.actualFinish == kNoDate ? QStringLiteral("NA") : formatDate(t.actualFinish, locale);
    }
    const Variance v = variance(project, t, 0);
    const auto na = [&](const QString &text) { return baseline().set ? text : QStringLiteral("NA"); };
    if (field == QLatin1String("baselineStart")) {
        return na(formatDate(baseline().start, locale));
    }
    if (field == QLatin1String("baselineFinish")) {
        return na(formatDate(baseline().finish, locale));
    }
    if (field == QLatin1String("baselineDuration")) {
        return na(formatDuration(baseline().duration, DurationUnit::Days, fmt, false, locale));
    }
    if (field == QLatin1String("baselineCost")) {
        return na(formatMoney(baseline().cost, cur, locale));
    }
    if (field == QLatin1String("startVariance")) {
        return v.valid ? formatSpan(v.start, fmt, locale) : QString();
    }
    if (field == QLatin1String("finishVariance")) {
        return v.valid ? formatSpan(v.finish, fmt, locale) : QString();
    }
    if (field == QLatin1String("durationVariance")) {
        return v.valid ? formatSpan(v.duration, fmt, locale) : QString();
    }
    if (field == QLatin1String("costVariance")) {
        return v.valid ? formatMoney(v.cost, cur, locale) : QString();
    }
    if (field == QLatin1String("totalSlack")) {
        return formatSpan(t.totalSlack, fmt, locale);
    }
    if (field == QLatin1String("freeSlack")) {
        return formatSpan(t.freeSlack, fmt, locale);
    }
    if (field == QLatin1String("earlyStart")) {
        return formatDate(t.earlyStart, locale);
    }
    if (field == QLatin1String("earlyFinish")) {
        return formatDate(t.earlyFinish, locale);
    }
    if (field == QLatin1String("lateStart")) {
        return formatDate(t.lateStart, locale);
    }
    if (field == QLatin1String("lateFinish")) {
        return formatDate(t.lateFinish, locale);
    }
    if (field == QLatin1String("active")) {
        return yesNo(t.active);
    }
    if (field == QLatin1String("milestone")) {
        return yesNo(t.isMilestone());
    }
    if (field == QLatin1String("calendar")) {
        return t.calendarUid >= 0 ? calendarName(project, t.calendarUid) : QStringLiteral("None");
    }
    if (field == QLatin1String("critical")) {
        return yesNo(t.critical);
    }
    return trackingFieldText(project, index, field, locale);
}

QString trackingFieldText(const Project &project, int index, const QString &field, const QLocale &locale)
{
    const Task &t = project.tasks.at(index);
    const DurationFormat fmt = durationFormatOf(project.info);
    const Minutes actual = actualDurationOf(t, project.info.minutesPerDay);
    const Minutes actualWork = actualWorkOf(t, project.info.minutesPerDay); // M83: as typed
    const bool elapsed = !t.summary && t.durationElapsed;
    if (field == QLatin1String("actualDuration")) {
        return formatDuration(actual, t.summary ? DurationUnit::Days : t.durationUnit, fmt, false, locale, elapsed);
    }
    if (field == QLatin1String("remainingDuration")) {
        return formatDuration(t.duration - actual, t.summary ? DurationUnit::Days : t.durationUnit, fmt, false, locale,
                              elapsed);
    }
    if (field == QLatin1String("actualWork")) {
        return formatWork(actualWork, locale);
    }
    if (field == QLatin1String("remainingWork")) {
        return formatWork(t.work - actualWork, locale);
    }
    if (field == QLatin1String("baselineWork")) {
        return t.baselines[0].set ? formatWork(t.baselines[0].work, locale) : QStringLiteral("NA");
    }
    if (field == QLatin1String("workVariance")) {
        const Variance v = variance(project, t, 0);
        return v.valid ? formatWork(v.work, locale) : QString();
    }
    if (field == QLatin1String("levelingDelay")) {
        return formatLevelingDelay(t, project.info, locale);
    }
    if (field == QLatin1String("preLeveledStart")) {
        return t.preLeveledStart == kNoDate ? QStringLiteral("NA") : formatDate(t.preLeveledStart, locale);
    }
    if (field == QLatin1String("preLeveledFinish")) {
        return t.preLeveledFinish == kNoDate ? QStringLiteral("NA") : formatDate(t.preLeveledFinish, locale);
    }
    return workOrCustomFieldText(project, index, field, locale); // M83
}

QString taskFieldEditText(const Project &project, int index, const QString &field, const QLocale &locale)
{
    if (index < 0 || index >= project.tasks.size()) {
        return {};
    }
    const Task &t = project.tasks.at(index);
    const ProjectInfo &info = project.info;
    if (field == QLatin1String("notes")) {
        return t.notes;
    }
    if (field == QLatin1String("start")) {
        return editDate(t.start, info.defaultStartMinute, locale);
    }
    if (field == QLatin1String("finish")) {
        return editDate(t.finish, info.defaultFinishMinute, locale);
    }
    if (field == QLatin1String("percent")) {
        return QString::number(t.percentComplete);
    }
    if (field == QLatin1String("priority")) {
        return QString::number(t.priority);
    }
    if (isCustomField(field)) {
        return customFieldEditText(project, index, field, locale); // M83
    }
    return taskFieldText(project, index, field, locale);
}

} // namespace QindaOffice::Plan
