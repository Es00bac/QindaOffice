// SPDX-License-Identifier: GPL-3.0-or-later
// A task's working segments (task_segments.h).
#include "task_segments.h"

#include "work_calendar.h"

#include <algorithm>

namespace QindaOffice::Plan {

namespace {

Minutes advance(const Task &task, const WorkCalendar &calendar, Minutes from, Minutes minutes)
{
    if (minutes <= 0) {
        return from;
    }
    return task.durationElapsed ? from + minutes : calendar.add(from, minutes);
}

} // namespace

Minutes taskMinutesBetween(const Task &task, const WorkCalendar &calendar, Minutes from, Minutes to)
{
    if (to <= from) {
        return 0;
    }
    return task.durationElapsed ? to - from : calendar.workBetween(from, to);
}

QList<TimeInterval> taskSegments(const Task &task, const WorkCalendar &calendar, Minutes start)
{
    QList<TimeInterval> out;
    const Minutes from = start != kNoDate ? start : task.start;
    if (task.summary || from == kNoDate) {
        return out;
    }
    if (task.duration <= 0) {
        out.append({from, from});
        return out;
    }
    QList<TaskSplit> splits = task.splits;
    std::sort(splits.begin(), splits.end(), [](const TaskSplit &a, const TaskSplit &b) { return a.at < b.at; });
    Minutes cursor = task.durationElapsed ? from : calendar.snapForward(from);
    Minutes done = 0;
    for (const TaskSplit &s : std::as_const(splits)) {
        if (s.at <= done || s.at >= task.duration || s.gap <= 0) {
            continue;
        }
        const Minutes end = advance(task, calendar, cursor, s.at - done);
        out.append({cursor, end});
        cursor = advance(task, calendar, end, s.gap);
        if (!task.durationElapsed) {
            cursor = calendar.snapForward(cursor);
        }
        done = s.at;
    }
    out.append({cursor, advance(task, calendar, cursor, task.duration - done)});
    // An actual finish earlier than the plan ends the work there.
    if (start == kNoDate && task.finish != kNoDate && task.finish > from) {
        while (!out.isEmpty() && out.last().from >= task.finish) {
            out.removeLast();
        }
        if (!out.isEmpty() && out.last().to > task.finish) {
            out.last().to = task.finish;
        }
    }
    return out;
}

QList<TimeInterval> workingWindow(const Task &task, const WorkCalendar &calendar, Minutes span)
{
    QList<TimeInterval> out;
    Minutes left = span;
    for (const TimeInterval &segment : taskSegments(task, calendar)) {
        if (left <= 0) {
            break;
        }
        const Minutes length = taskMinutesBetween(task, calendar, segment.from, segment.to);
        if (length <= left) {
            out.append(segment);
            left -= length;
        } else {
            out.append({segment.from, advance(task, calendar, segment.from, left)});
            left = 0;
        }
    }
    return out;
}

} // namespace QindaOffice::Plan
