// SPDX-License-Identifier: GPL-3.0-or-later
// Split Task, Reschedule Uncompleted Work and the progress line
// (tracking.h).
#include "tracking.h"

#include "outline.h"
#include "plan_fields_p.h"
#include "plan_time.h"
#include "scheduler.h"
#include "task_segments.h"
#include "work_calendar.h"

#include <algorithm>

namespace QindaOffice::Plan {

namespace {

// The leaf tasks under the rows (a summary stands for its subtasks).
QList<int> leavesUnder(const Project &project, const QList<int> &rows)
{
    QList<int> out;
    for (int i : rows) {
        if (i < 0 || i >= project.tasks.size()) {
            continue;
        }
        const int end = subtreeEnd(project, i);
        for (int k = i; k < end; ++k) {
            if (!project.tasks.at(k).summary && !out.contains(k)) {
                out.append(k);
            }
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

void addSplit(Task &t, Minutes at, Minutes gap)
{
    for (TaskSplit &s : t.splits) {
        if (s.at == at) {
            s.gap += gap; // the same place: the stop just gets longer
            return;
        }
    }
    t.splits.append({at, gap});
    std::sort(t.splits.begin(), t.splits.end(), [](const TaskSplit &a, const TaskSplit &b) { return a.at < b.at; });
}

} // namespace

Minutes instantAfterWork(const Project &project, int index, Minutes minutes)
{
    const Task &t = project.tasks.at(index);
    if (t.start == kNoDate || minutes <= 0) {
        return t.start;
    }
    const WorkCalendar c(project, effectiveCalendarUid(project, index));
    const QList<TimeInterval> window = workingWindow(t, c, minutes);
    return window.isEmpty() ? t.start : window.last().to;
}

EditResult splitTask(Project &project, int index, Minutes stop, Minutes resume)
{
    if (index < 0 || index >= project.tasks.size()) {
        return EditResult::fail(QStringLiteral("There is no such task."));
    }
    const Task &t = project.tasks.at(index);
    if (t.summary) {
        return EditResult::fail(QStringLiteral("Split the subtasks of a summary task, not the summary."));
    }
    if (t.duration <= 0) {
        return EditResult::fail(QStringLiteral("A milestone has no work to split."));
    }
    if (t.start == kNoDate || stop <= t.start || stop >= t.finish) {
        return EditResult::fail(QStringLiteral("The work of “%1” can only stop between its start and its finish.").arg(t.name));
    }
    if (resume <= stop) {
        return EditResult::fail(QStringLiteral("The work must resume after it stops."));
    }
    const WorkCalendar c(project, effectiveCalendarUid(project, index));
    Minutes done = 0;
    bool inside = false;
    for (const TimeInterval &segment : taskSegments(t, c)) {
        if (stop >= segment.to) {
            done += taskMinutesBetween(t, c, segment.from, segment.to);
            continue;
        }
        if (stop > segment.from) {
            done += taskMinutesBetween(t, c, segment.from, stop);
            inside = true;
        }
        break;
    }
    if (!inside) {
        return EditResult::fail(QStringLiteral("“%1” is already stopped then.").arg(t.name));
    }
    const Minutes gap = taskMinutesBetween(t, c, stop, resume);
    if (gap <= 0) {
        return EditResult::fail(QStringLiteral("No working time lies between those dates, so the work would not stop."));
    }
    if (done <= 0 || done >= t.duration) {
        return EditResult::fail(QStringLiteral("The work of “%1” can only stop part-way through it.").arg(t.name));
    }
    addSplit(project.tasks[index], done, gap);
    return {};
}

int removeSplits(Project &project, const QList<int> &indices)
{
    int count = 0;
    for (int i : leavesUnder(project, indices)) {
        if (!project.tasks.at(i).splits.isEmpty()) {
            project.tasks[i].splits.clear();
            ++count;
        }
    }
    return count;
}

int rescheduleUncompletedWork(Project &project, Minutes after, const QList<int> &indices)
{
    QList<int> rows = indices;
    if (rows.isEmpty()) {
        for (int i = 0; i < project.tasks.size(); ++i) {
            rows.append(i);
        }
    }
    int moved = 0;
    for (int i : leavesUnder(project, rows)) {
        Task &t = project.tasks[i];
        if (!t.effectiveActive || t.percentComplete >= 100 || t.start == kNoDate || t.duration <= 0) {
            continue;
        }
        const bool started = t.percentComplete > 0 || t.actualStart != kNoDate;
        if (!started) {
            if (t.start >= after) {
                continue;
            }
            if (t.manual) {
                const Minutes shift = after - t.start;
                t.manualStart = after;
                t.manualFinish = t.manualFinish == kNoDate ? kNoDate : t.manualFinish + shift;
            } else {
                t.constraint = ConstraintType::StartNoEarlierThan;
                t.constraintDate = after;
            }
            ++moved;
            continue;
        }
        const Minutes actual = actualDurationOf(t, project.info.minutesPerDay);
        if (actual <= 0 || actual >= t.duration) {
            continue;
        }
        // Where the remaining work starts now: right where the actual work
        // ends, or after a split that is already there.
        const WorkCalendar c(project, effectiveCalendarUid(project, i));
        const QList<TimeInterval> segments = taskSegments(t, c);
        const Minutes stop = instantAfterWork(project, i, actual);
        Minutes resumes = stop;
        for (int k = 0; k + 1 < segments.size(); ++k) {
            if (segments.at(k).to == stop) {
                resumes = segments.at(k + 1).from;
            }
        }
        if (resumes >= after) {
            continue;
        }
        const Minutes gap = taskMinutesBetween(t, c, resumes, after);
        if (gap > 0) {
            addSplit(t, actual, gap);
            ++moved;
        }
    }
    return moved;
}

QList<ProgressPoint> progressLine(const Project &project, const QList<int> &rows, Minutes statusDate)
{
    QList<ProgressPoint> out;
    for (int i : rows) {
        ProgressPoint point{i, statusDate, false};
        if (i >= 0 && i < project.tasks.size() && statusDate != kNoDate) {
            const Task &t = project.tasks.at(i);
            const bool inProgress = t.percentComplete > 0;
            const bool shouldHaveStarted = t.start != kNoDate && t.start < statusDate;
            if (!t.summary && t.effectiveActive && t.start != kNoDate && t.percentComplete < 100
                && (inProgress || shouldHaveStarted)) {
                point.at = instantAfterWork(project, i, actualDurationOf(t, project.info.minutesPerDay));
                point.bent = point.at != statusDate;
            }
        }
        out.append(point);
    }
    return out;
}

} // namespace QindaOffice::Plan
