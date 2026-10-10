// SPDX-License-Identifier: GPL-3.0-or-later
// Project's recurring tasks (recurring_tasks.h).
#include "recurring_tasks.h"

#include "outline.h"
#include "plan_time.h"
#include "work_calendar.h"

#include <algorithm>

namespace QindaOffice::Plan {

namespace {

constexpr int kMaxOccurrences = 999; // Project's limit

using Kind = RecurringException::Kind;

QString weekdayNames(int mask, const QLocale &locale)
{
    QStringList names;
    for (int d = 0; d < 7; ++d) {
        if ((mask >> d) & 1) {
            names.append(locale.dayName(d + 1, QLocale::LongFormat));
        }
    }
    return names.join(QStringLiteral(", "));
}

QString ordinal(int position)
{
    static const char *const names[] = {"first", "second", "third", "fourth", "last"};
    return QString::fromLatin1(names[std::clamp(position, 0, 4)]);
}

// The occurrences of the recurring summary at `index` (its direct subtasks).
QList<int> occurrencesOf(const Project &project, int index)
{
    QList<int> out;
    const int end = subtreeEnd(project, index);
    for (int k = index + 1; k < end; ++k) {
        out.append(k);
    }
    return out;
}

void insertOccurrences(Project &project, int summary, const QList<QDate> &dates, const TaskRecurrence &r)
{
    const Task base = project.tasks.at(summary);
    for (int k = 0; k < dates.size(); ++k) {
        Task occurrence;
        occurrence.name = QStringLiteral("%1 %2").arg(base.name).arg(k + 1);
        occurrence.duration = std::max<Minutes>(0, r.duration);
        occurrence.durationUnit = r.duration % std::max(1, project.info.minutesPerDay) == 0 ? DurationUnit::Days
                                                                                            : DurationUnit::Hours;
        occurrence.constraint = ConstraintType::StartNoEarlierThan;
        occurrence.constraintDate = toMinutes(dates.at(k), r.startMinute);
        occurrence.type = project.info.defaultTaskType;
        occurrence.effortDriven = project.info.newTasksEffortDriven || occurrence.type == TaskType::FixedWork;
        const int at = insertTask(project, summary + 1 + k, occurrence);
        project.tasks[at].outlineLevel = base.outlineLevel + 1;
    }
    computeStructure(project);
}

} // namespace

QList<QDate> recurrenceDates(const Project &project, const TaskRecurrence &recurrence)
{
    QList<QDate> out;
    RecurringException pattern = recurrence.pattern;
    const QDate from = pattern.from;
    if (!from.isValid()) {
        return out;
    }
    const int limit = recurrence.count > 0 ? std::min(recurrence.count, kMaxOccurrences) : kMaxOccurrences;
    pattern.to = recurrence.count > 0 ? from.addYears(100) : pattern.to;
    if (!pattern.to.isValid() || pattern.to < from) {
        return out;
    }
    const WorkCalendar calendar(project, project.info.calendarUid);
    int workdays = 0;
    // AGENT-GUARD: bounded like every calendar walk (~100 years).
    for (QDate d = from; d <= pattern.to && out.size() < limit && from.daysTo(d) < 36600; d = d.addDays(1)) {
        bool hit = false;
        if (pattern.kind == Kind::EveryDays && recurrence.workdays) {
            if (!calendar.isWorkingDay(d)) {
                continue;
            }
            hit = workdays % std::max(1, pattern.every) == 0;
            ++workdays;
        } else {
            hit = pattern.fallsOn(d);
        }
        if (hit) {
            out.append(d);
        }
    }
    return out;
}

QString recurrenceText(const TaskRecurrence &r, const QLocale &locale)
{
    const RecurringException &p = r.pattern;
    const int n = std::max(1, p.every);
    QString what;
    switch (p.kind) {
    case Kind::EveryDays:
        what = n == 1 ? (r.workdays ? QStringLiteral("Every working day") : QStringLiteral("Every day"))
                      : QStringLiteral("Every %1 %2").arg(n).arg(r.workdays ? QStringLiteral("working days")
                                                                            : QStringLiteral("days"));
        break;
    case Kind::Weekly:
        what = (n == 1 ? QStringLiteral("Every week") : QStringLiteral("Every %1 weeks").arg(n))
            + QStringLiteral(" on ") + weekdayNames(p.daysOfWeek, locale);
        break;
    case Kind::MonthlyByDay:
        what = QStringLiteral("Day %1 of every %2").arg(p.day).arg(n == 1 ? QStringLiteral("month")
                                                                           : QStringLiteral("%1 months").arg(n));
        break;
    case Kind::MonthlyByPosition:
        what = QStringLiteral("The %1 %2 of every %3")
                   .arg(ordinal(p.position), locale.dayName(p.weekday, QLocale::LongFormat),
                        n == 1 ? QStringLiteral("month") : QStringLiteral("%1 months").arg(n));
        break;
    case Kind::YearlyByDay:
        what = QStringLiteral("Every %1 %2").arg(locale.monthName(p.month, QLocale::LongFormat)).arg(p.day);
        break;
    case Kind::YearlyByPosition:
        what = QStringLiteral("The %1 %2 of %3")
                   .arg(ordinal(p.position), locale.dayName(p.weekday, QLocale::LongFormat),
                        locale.monthName(p.month, QLocale::LongFormat));
        break;
    }
    const QString range = r.count > 0 ? QStringLiteral("%1 occurrences").arg(r.count)
                                      : QStringLiteral("until %1").arg(locale.toString(p.to, QLocale::ShortFormat));
    return QStringLiteral("%1 from %2 · %3").arg(what, locale.toString(p.from, QLocale::ShortFormat), range);
}

int insertRecurringTask(Project &project, int at, const QString &name, const TaskRecurrence &recurrence, QString *error)
{
    const QList<QDate> dates = recurrenceDates(project, recurrence);
    if (dates.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("The pattern gives no occurrence between those dates.");
        }
        return -1;
    }
    Task summary;
    summary.name = name.trimmed().isEmpty() ? QStringLiteral("Recurring task") : name.trimmed();
    summary.recurring = true;
    summary.recurrence = recurrence;
    const int index = insertTask(project, at, summary);
    insertOccurrences(project, index, dates, recurrence);
    return index;
}

EditResult setRecurrence(Project &project, int index, const TaskRecurrence &recurrence)
{
    if (index < 0 || index >= project.tasks.size() || !project.tasks.at(index).recurring) {
        return EditResult::fail(QStringLiteral("That is not a recurring task."));
    }
    const QList<int> occurrences = occurrencesOf(project, index);
    for (int k : occurrences) {
        const Task &t = project.tasks.at(k);
        if (t.percentComplete > 0 || t.actualStart != kNoDate) {
            return EditResult::fail(QStringLiteral("“%1” has progress, so the occurrences cannot be made again.").arg(t.name));
        }
    }
    const QList<QDate> dates = recurrenceDates(project, recurrence);
    if (dates.isEmpty()) {
        return EditResult::fail(QStringLiteral("The pattern gives no occurrence between those dates."));
    }
    removeTasks(project, occurrences);
    const int summary = project.taskIndex(project.tasks.at(index).uid); // unchanged: occurrences follow it
    project.tasks[summary].recurrence = recurrence;
    insertOccurrences(project, summary, dates, recurrence);
    return {};
}

} // namespace QindaOffice::Plan
