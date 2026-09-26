// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Baselines (up to kBaselineCount, numbered 0..2 — the app shows them as
// Baseline, Baseline 1, Baseline 2) and the variances against them.
//
// AGENT-CONTRACT (variance sign, as Project): positive = later / longer /
// more expensive than the baseline. Start and finish variances are
// working time on the project calendar.

#include "plan_types.h"

namespace QindaOffice::Plan {

// Copies the scheduled start, finish, duration, work and cost of the tasks
// (all when `taskUids` is empty) into baseline `number`. The project must
// be scheduled. Returns the number of tasks saved.
int setBaseline(Project &project, int number, const QList<int> &taskUids = {});
int clearBaseline(Project &project, int number, const QList<int> &taskUids = {});
[[nodiscard]] bool hasBaseline(const Project &project, int number);

struct Variance {
    bool valid = false; // the task has this baseline
    Minutes start = 0;
    Minutes finish = 0;
    Minutes duration = 0;
    Minutes work = 0;
    qint64 cost = 0;
};
[[nodiscard]] Variance variance(const Project &project, const Task &task, int number = 0);

} // namespace QindaOffice::Plan
