// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The scheduling engine (D-051): a deterministic critical-path pass over the
// task outline, the links and the working calendars.
//
//  1. Structure (outline.h): ids, WBS, summaries, inherited inactivity.
//  2. Forward pass in topological order over a node graph where a summary
//     is two nodes — its ENTRY (predecessor links and the summary's
//     Start No Earlier Than become a floor every child inherits) and its
//     EXIT (the roll-up of its children, which successors link from). Early
//     dates honour FS/SS/FF/SF links with lag (working time on the project
//     calendar, or a percentage of the predecessor's duration), the eight
//     constraint types (hard constraints win over links and are flagged as
//     conflicts, as Project does by default), the leveling delay (M79:
//     Project's, relative to the unleveled early start), actuals and
//     manual scheduling.
//  3. Backward pass from the project finish: late dates, total slack
//     (working time between early and late finish), free slack, critical
//     (active, not complete, total slack <= the critical slack limit). M79:
//     a deadline caps the late finish (dates do not move; slack can go
//     negative); with multiple critical paths a task that drives nothing
//     has its early finish as its late finish.
//  4. As Late As Possible tasks move by their free slack.
//  5. Roll-ups: summary dates, duration, % complete (duration-weighted),
//     slack; assignment work and cost, task and resource totals; per-day
//     resource overallocation (resources.h).
//
// Circular links and links between a summary and its own subtasks are
// ignored with an issue (Project refuses to create them; plan_edit.h does
// too, so they only arrive from files). FF/SF links onto a summary are
// ignored the same way (Project refuses them).
//
// AGENT-NOTE (calendars): a task is scheduled on its own calendar when it
// has one, else on the calendar of its first assigned work resource, else
// on the project calendar. Lag is always project working time.
//
// Cost: one full pass is O(tasks + links) calendar walks with a per-day
// cache; 2,000 tasks recalculate in a few milliseconds (tst_plan_perf). The
// app only calls it after edits that can move dates.

#include "plan_types.h"

namespace QindaOffice::Plan {

class WorkCalendar;

// Recomputes every computed field of `project` and project.issues.
void schedule(Project &project);

// The calendar uid a task is scheduled on (see the note above).
[[nodiscard]] int effectiveCalendarUid(const Project &project, int taskIndex);

// Would a link pred → succ (task indices) be refused? Returns the reason,
// empty when the link is fine: self links, links inside one summary's
// subtree, duplicate links and cycles are refused.
[[nodiscard]] QString linkProblem(const Project &project, int predecessorIndex, int successorIndex,
                                  LinkType type = LinkType::FinishToStart);

} // namespace QindaOffice::Plan
