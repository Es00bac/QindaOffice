// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Resource leveling: a deliberately simple, documented heuristic (D-051,
// docs/plan.md "Leveling"). It only ever DELAYS tasks (it sets
// Task::levelingFloor); it never splits work, changes units or reorders.
//
//   repeat
//     find the resource's first overallocated day D;
//     the candidates are the auto-scheduled, not-started, not
//     hard-constrained (Must Start/Finish On) leaf tasks it works on that day;
//     keep the one with the least total slack (ties: earlier start, lower
//     ID) and delay another to the next working day after D:
//       pass 1 picks only a task that can move that far WITHIN its total
//       slack (the project finish does not move), the one with the most slack;
//       pass 2, only when pass 1 finds none, picks the task with the most
//       slack even if the finish moves;
//     reschedule
//   until the resource is not overallocated, nothing can be delayed on D
//   (D is skipped), or 2,000 steps.
//
// clearLeveling() removes every leveling delay.

#include "plan_types.h"

namespace QindaOffice::Plan {

struct LevelingResult {
    int delayedTasks = 0;     // distinct tasks that were delayed
    int steps = 0;
    bool resolved = false;    // no overallocation left on the leveled resources
    bool finishMoved = false; // some delay went beyond total slack
    QStringList unresolvedDays; // ISO dates nothing could fix
};

// Levels one resource (resourceUid > 0) or every work resource (0).
// `project` is rescheduled on return.
LevelingResult levelResources(Project &project, int resourceUid = 0);
// Returns how many tasks had a leveling delay; reschedules.
int clearLeveling(Project &project, const QList<int> &taskUids = {});

} // namespace QindaOffice::Plan
