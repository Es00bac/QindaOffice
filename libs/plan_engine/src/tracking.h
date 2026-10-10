// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Project's tracking tools M83 adds (D-092): Split Task, Update Project's
// "Reschedule uncompleted work to start after" and the progress line. Like
// every edit (plan_edit.h) nothing here schedules: the caller runs
// schedule(); each function works on the schedule as it stands.
//
// AGENT-NOTE (Project's semantics): a split is working time the task does
// not work, measured on its calendar (Task::splits), so it moves with the
// task; rescheduling uncompleted work splits an in-progress task where its
// actual duration ends and resumes it at the date (Project's Stop / Resume),
// and gives a task that should have started but has not Start No Earlier
// Than the date; the progress line joins, at the status date, each task's
// progress point — start plus its actual duration — for tasks in progress
// or that should have started, so it bends left where work is behind and
// right where it is ahead.

#include "plan_edit.h"

namespace QindaOffice::Plan {

// The work stops at `stop` and resumes at `resume` (instants on the current
// schedule, inside the task). Refused on summaries, milestones, outside the
// task, inside an existing gap, or when no working time lies between.
EditResult splitTask(Project &project, int index, Minutes stop, Minutes resume);
// Takes the splits out of the tasks (their work closes up). Returns how many
// tasks had any.
int removeSplits(Project &project, const QList<int> &indices);

// Reschedule Uncompleted Work to start after `after` (all tasks when
// `indices` is empty; a summary stands for its subtasks). Returns how many
// tasks moved.
int rescheduleUncompletedWork(Project &project, Minutes after, const QList<int> &indices = {});

// The instant `minutes` of a scheduled task's work (on its clock, split
// gaps left out) are done: its start plus that much work.
[[nodiscard]] Minutes instantAfterWork(const Project &project, int index, Minutes minutes);

// The progress line at `statusDate` over `rows` (task indices, the view's
// order): one point per row, the status date itself where the task neither
// is in progress nor should have started.
struct ProgressPoint {
    int row = -1;
    Minutes at = kNoDate;
    bool bent = false; // the point is the task's progress, not the status date
};
[[nodiscard]] QList<ProgressPoint> progressLine(const Project &project, const QList<int> &rows, Minutes statusDate);

} // namespace QindaOffice::Plan
