// SPDX-License-Identifier: GPL-3.0-or-later
// The task fields M83 added (D-092): Work typed on a task, its Type and
// Effort Driven flag (task_types.h has Project's rules), Act. Work and Act.
// Cost typed, and the custom fields (custom_fields.h).
#include "custom_fields.h"
#include "plan_edit.h"
#include "plan_fields_p.h"
#include "plan_time.h"
#include "scheduler.h"
#include "task_types.h"
#include "work_calendar.h"

namespace QindaOffice::Plan {

namespace {

bool isNa(const QString &text)
{
    const QString t = text.trimmed();
    return t.isEmpty() || t.compare(QLatin1String("NA"), Qt::CaseInsensitive) == 0;
}

// The finish of a task that runs `duration` (and its splits) from `start`.
Minutes finishFrom(const Project &project, int index, Minutes start)
{
    const Task &t = project.tasks.at(index);
    Minutes length = t.duration;
    for (const TaskSplit &s : t.splits) {
        length += s.at < t.duration ? s.gap : 0;
    }
    if (t.durationElapsed) {
        return start + length;
    }
    const WorkCalendar c(project, effectiveCalendarUid(project, index));
    return length > 0 ? c.add(c.snapForward(start), length) : start;
}

// Act. Work typed: % complete follows it; more than the work finishes the
// task and makes the work what it took (Project's rule for actuals).
EditResult setActualWork(Project &project, int index, const QString &text)
{
    const std::optional<Minutes> actual = parseWorkText(text, project.info);
    if (!actual) {
        return EditResult::fail(QStringLiteral("“%1” is not work. Try 16h, 2d or 90m.").arg(text.trimmed()));
    }
    const Task &t = project.tasks.at(index);
    const Minutes work = t.work;
    if (work <= 0) {
        return EditResult::fail(QStringLiteral("“%1” has no work to track. Assign a resource or type its work first.")
                                    .arg(t.name));
    }
    if (*actual <= 0) {
        setPercentComplete(project, {index}, 0);
        return {};
    }
    if (*actual < work) {
        setPercentComplete(project, {index}, int(std::clamp<qint64>(divRound(*actual * 100, work), 1, 99)));
        return {};
    }
    if (*actual > work) {
        const EditResult r = setTaskWork(project, index, *actual);
        if (!r.ok) {
            return r;
        }
    }
    Task &done = project.tasks[index];
    if (done.actualStart == kNoDate) {
        done.actualStart = done.start;
    }
    done.actualFinish = finishFrom(project, index, done.actualStart);
    setPercentComplete(project, {index}, 100);
    return {};
}

EditResult setActualCost(Project &project, int index, const QString &text, const QLocale &locale)
{
    if (!project.info.editableActualCosts) {
        return EditResult::fail(QStringLiteral("Actual costs are calculated from the work done. To type them, turn off "
                                               "“Actual costs are always calculated” in Project Information."));
    }
    Task &t = project.tasks[index];
    if (isNa(text)) {
        t.actualCostInput = -1;
        return {};
    }
    const std::optional<qint64> money = parseMoney(text, project.info.currency, locale);
    if (!money || *money < 0) {
        return EditResult::fail(QStringLiteral("“%1” is not an amount.").arg(text.trimmed()));
    }
    t.actualCostInput = *money;
    return {};
}

} // namespace

std::optional<Minutes> parseWorkText(const QString &text, const ProjectInfo &info)
{
    const QString t = text.trimmed();
    bool bare = false;
    const double hours = QString(t).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&bare);
    if (bare) {
        return hours >= 0 && hours < 1e9 ? std::optional<Minutes>(std::llround(hours * 60.0)) : std::nullopt;
    }
    const std::optional<ParsedDuration> d = parseDuration(t, durationFormatOf(info));
    return d && !d->elapsed ? std::optional<Minutes>(d->minutes) : std::nullopt;
}

Minutes actualWorkOf(const Task &t, int minutesPerDay)
{
    const Minutes exact = (t.work * t.percentComplete + 50) / 100;
    if (t.percentComplete <= 0 || t.percentComplete >= 100 || t.work <= 0) {
        return exact;
    }
    const double lo = double(t.work) * (t.percentComplete - 0.5) / 100.0;
    const double hi = double(t.work) * (t.percentComplete + 0.5) / 100.0;
    for (const Minutes step : {Minutes(std::max(1, minutesPerDay)), Minutes(60), Minutes(30), Minutes(15)}) {
        const Minutes round = (exact + step / 2) / step * step;
        if (double(round) >= lo && double(round) < hi) {
            return round;
        }
    }
    return exact;
}

qint64 actualCostOf(const Project &project, const Task &task)
{
    if (!task.summary && task.actualCostInput >= 0 && project.info.editableActualCosts) {
        return task.actualCostInput;
    }
    return divRound(task.cost * task.percentComplete, 100);
}

EditResult setWorkOrCustomField(Project &project, int index, const QString &field, const QString &text,
                                const QLocale &locale)
{
    if (isCustomField(field)) {
        return setCustomField(project, index, field, text, locale);
    }
    const Task &t = project.tasks.at(index);
    const bool known = field == QLatin1String("work") || field == QLatin1String("type")
        || field == QLatin1String("effortDriven") || field == QLatin1String("actualWork")
        || field == QLatin1String("actualCost");
    if (!known) {
        return EditResult::fail(QStringLiteral("Unknown field %1.").arg(field));
    }
    if (t.summary) {
        return EditResult::fail(QStringLiteral("A summary task's %1 comes from its subtasks.").arg(taskFieldTitle(field)));
    }
    if (field == QLatin1String("work")) {
        const std::optional<Minutes> work = parseWorkText(text, project.info);
        if (!work) {
            return EditResult::fail(QStringLiteral("“%1” is not work. Try 40h, 5d or 90m.").arg(text.trimmed()));
        }
        return setTaskWork(project, index, *work);
    }
    if (field == QLatin1String("type")) {
        const std::optional<TaskType> type = taskTypeFromName(text);
        if (!type) {
            return EditResult::fail(QStringLiteral("Type is Fixed Units, Fixed Duration or Fixed Work."));
        }
        return setTaskType(project, index, *type);
    }
    if (field == QLatin1String("effortDriven")) {
        const QString l = text.trimmed().toLower();
        const bool yes = l == QLatin1String("yes") || l == QLatin1String("y") || l == QLatin1String("true") || l == QLatin1String("1");
        const bool no = l == QLatin1String("no") || l == QLatin1String("n") || l == QLatin1String("false") || l == QLatin1String("0");
        if (!yes && !no) {
            return EditResult::fail(QStringLiteral("Type Yes or No."));
        }
        return setEffortDriven(project, index, yes);
    }
    if (field == QLatin1String("actualWork")) {
        return setActualWork(project, index, text);
    }
    return setActualCost(project, index, text, locale);
}

QString workOrCustomFieldText(const Project &project, int index, const QString &field, const QLocale &locale)
{
    if (isCustomField(field)) {
        return customFieldText(project, index, field, locale);
    }
    const Task &t = project.tasks.at(index);
    if (field == QLatin1String("type")) {
        return taskTypeName(t.summary ? TaskType::FixedDuration : t.type); // Project's summaries are fixed duration
    }
    if (field == QLatin1String("effortDriven")) {
        return !t.summary && (t.effortDriven || t.type == TaskType::FixedWork) ? QStringLiteral("Yes") : QStringLiteral("No");
    }
    return {};
}

} // namespace QindaOffice::Plan
