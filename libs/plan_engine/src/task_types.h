// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Project's work formula (M83, D-092): per assignment Work = Duration ×
// Units, and which of the three a change recalculates — Microsoft Project's
// task types — with effort-driven scheduling when resources are added to or
// removed from a task. Nothing here schedules; the caller runs schedule().
//
// AGENT-CONTRACT (Project's table, docs/plan.md "Task types"):
//                   units changed   duration changed   work changed
//   Fixed Units     duration        work               duration
//   Fixed Work      duration        units              duration
//   Fixed Duration  work            work               units
// Effort driven (always for Fixed Work): resources added to or removed from
// a task that already has work assignments share its work in proportion to
// their units, so Fixed Units and Fixed Work tasks get shorter or longer and
// a Fixed Duration task's units change; the first resources assigned are
// never effort driven (they take the work typed on the task, if any, else
// duration × units). Without effort driving an added resource adds work.
//
// AGENT-NOTE (representation, plan_types.h): an assignment's work is
// `workInput`, or kWorkFollowsDuration (it works its units for the whole
// duration — what every assignment did before M83, and what an assignment
// keeps until a rule here sets its work). It works its work evenly over its
// SPAN: the first `workSpan` working minutes of the task, or the whole
// duration when workSpan is -1. A span shorter than the task only arises
// when one assignment of several has its units or work typed on a Fixed
// Units or Fixed Work task; the task's duration is then its longest
// assignment, as in Project. `units` stay whole percents: for a Fixed
// Duration or Fixed Work task they are the rounded work ÷ span, and the
// work, not the rounded units, is the truth the usage views spread.
// Material and cost resources take no part in any of this.

#include "plan_edit.h"
#include "plan_types.h"

namespace QindaOffice::Plan {

// "Fixed Units" | "Fixed Duration" | "Fixed Work".
[[nodiscard]] QString taskTypeName(TaskType type);
[[nodiscard]] std::optional<TaskType> taskTypeFromName(const QString &text);

// Is this assignment a work resource's (the only kind the formula covers)?
[[nodiscard]] bool isWorkAssignment(const Project &project, const Assignment &assignment);
// The working minutes of the task the assignment works (≤ its duration).
[[nodiscard]] Minutes assignmentSpan(const Task &task, const Assignment &assignment);
// Its work in minutes: workInput, or span × units (0 on a milestone).
[[nodiscard]] Minutes assignmentWork(const Task &task, const Assignment &assignment);

// Typing one of the three. Each refuses (project untouched) with a reason.
EditResult setTaskDuration(Project &project, int index, Minutes duration);
EditResult setTaskWork(Project &project, int index, Minutes work);
EditResult setAssignmentUnits(Project &project, int assignmentIndex, int units);
EditResult setAssignmentWork(Project &project, int assignmentIndex, Minutes work);
// Fixed Work turns effort driving on; turning it off on Fixed Work refuses.
EditResult setTaskType(Project &project, int index, TaskType type);
EditResult setEffortDriven(Project &project, int index, bool on);

// Resources added to (resource uid, units) and removed from (resource
// uids) one leaf task in ONE change, with effort driving as above. Units of
// a material resource are its quantity × 100; a cost resource's 100.
struct ResourceRequest {
    int resourceUid = 0;
    int units = 100;
};
EditResult changeAssignments(Project &project, int index, const QList<ResourceRequest> &added,
                             const QList<int> &removedResourceUids);

} // namespace QindaOffice::Plan
