// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// When a scheduled leaf task works (M83, D-092): from its start, its
// duration on its calendar — or on the wall clock for an elapsed duration —
// with its split gaps left out (Task::splits). The usage views spread an
// assignment's work over the first part of these segments (its span,
// task_types.h); the printed Gantt draws a bar per segment.

#include "plan_types.h"

namespace QindaOffice::Plan {

class WorkCalendar;

struct TimeInterval {
    Minutes from = 0;
    Minutes to = 0;
    friend bool operator==(const TimeInterval &, const TimeInterval &) = default;
};

// The segments of a task scheduled from `start` (its own start when kNoDate),
// clipped to its finish. Empty for a summary or an unscheduled task; one
// [start, start] interval for a milestone.
[[nodiscard]] QList<TimeInterval> taskSegments(const Task &task, const WorkCalendar &calendar, Minutes start = kNoDate);
// The first `span` minutes (on the task's clock) of those segments.
[[nodiscard]] QList<TimeInterval> workingWindow(const Task &task, const WorkCalendar &calendar, Minutes span);
// Minutes of the task's clock inside [from, to): working minutes on the
// calendar, or wall-clock minutes for an elapsed task.
[[nodiscard]] Minutes taskMinutesBetween(const Task &task, const WorkCalendar &calendar, Minutes from, Minutes to);

} // namespace QindaOffice::Plan
