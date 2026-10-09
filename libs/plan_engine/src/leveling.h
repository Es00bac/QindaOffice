// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Resource leveling: a deliberately simple, documented heuristic (D-051,
// D-088, docs/plan.md "Leveling"). It only ever DELAYS tasks — it sets
// Project's Leveling Delay (Task::levelingDelay, elapsed time from the
// task's unleveled early start, as Project writes it to MSPDI); it never
// splits work, changes units or reorders.
//
//   [clear the delays of the tasks it may move, when levelClearFirst]
//   record every task's Pre-Leveled Start/Finish
//   repeat
//     find the resource's first overallocated day D (inside the range);
//     the candidates are the auto-scheduled, not-started, not
//     hard-constrained (Must Start/Finish On) leaf tasks it works on that
//     day whose priority is below 1000 (Project's "Do Not Level") — and,
//     for Level Selection, only the selected tasks;
//     keep the first task in leveling order and delay another to the next
//     working day after D (at least until the kept one finishes):
//       Standard          keep: least total slack, earlier start, higher
//                         priority, lower ID; delay: the most slack;
//       Priority, Standard keep: higher priority first, then Standard;
//                         delay: the lowest priority, then the most slack;
//       ID Only           keep: lower ID; delay: the highest ID;
//       pass 1 picks only a task that can move that far WITHIN its total
//       slack (the finish does not move); pass 2 — never when
//       levelWithinSlack — the same order even if the finish moves;
//     reschedule
//   until the resource is not overallocated, nothing can be delayed on D
//   (D is recorded and skipped), or 2,000 steps.
//
// clearLeveling() removes delays (all, or the listed tasks') and their
// pre-leveled dates.

#include "plan_types.h"

#include <QDate>

namespace QindaOffice::Plan {

struct LevelingOptions {
    int resourceUid = 0;         // 0 = every work resource
    QList<int> taskUids;         // Level Selection: only these move (empty = any)
    QDate from;                  // leveling range; invalid = from the start
    QDate to;                    //                 invalid = to the finish
    LevelingOrder order = LevelingOrder::Standard;
    bool withinSlackOnly = false;
    bool clearFirst = true;
};
// The project's own leveling options (ProjectInfo) for one resource or all.
[[nodiscard]] LevelingOptions projectLevelingOptions(const Project &project, int resourceUid = 0);

struct LevelingResult {
    int delayedTasks = 0;     // distinct tasks that were delayed
    int steps = 0;
    bool resolved = false;    // no overallocation left on the leveled resources (in range)
    bool finishMoved = false; // some delay went beyond total slack
    QStringList unresolvedDays; // ISO dates nothing could fix
};

// `project` is rescheduled on return.
LevelingResult levelResources(Project &project, const LevelingOptions &options);
// The project's options for one resource (resourceUid > 0) or every one (0).
LevelingResult levelResources(Project &project, int resourceUid = 0);
// Returns how many tasks had a leveling delay; reschedules.
int clearLeveling(Project &project, const QList<int> &taskUids = {});

// "2 edays", "1.5 edays", "3 days" — Project's Leveling Delay as text, and
// back (an "e" makes it elapsed; a plain duration is working time).
[[nodiscard]] QString formatLevelingDelay(const Task &task, const ProjectInfo &info, const QLocale &locale = QLocale());
// false (and nothing changed) when the text is not a delay.
bool parseLevelingDelay(const QString &text, const ProjectInfo &info, Minutes *minutes, bool *elapsed);

} // namespace QindaOffice::Plan
