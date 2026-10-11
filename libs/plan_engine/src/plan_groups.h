// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Project's Group By (M89, D-097): the rows a task view shows, banded by a
// field's value, each band with its rollup — Project's group summary row.
// Nothing here changes the project; PlanService draws the bands as QindaTK
// group header rows in the sheet and the Gantt (QindaTK D-312) and keeps
// which bands are collapsed (view state).
//
// AGENT-CONTRACT (group ids; the app's planGroup…Action commands map onto
// them one to one): "none", "complete" (Complete and Incomplete Tasks: not
// started, in progress, complete), "constraint" (Constraint Type),
// "critical", "duration", "milestones", "priority" (highest first),
// "resource" (the Resource Names text), "status" (Complete, On Schedule,
// Late, Future Task — against the status date or today, plan_reports.h's
// isLate), "mode" (Auto v. Manually Scheduled), "active" (Active v.
// Inactive) and "custom:<Field>" (a custom field's value, "Text1").
// Summary tasks are left out while a view is grouped (their tasks are
// grouped; a band's rollup would count them twice). Bands come in their
// value's order — numbers by value, text alphabetically, priority
// descending, a task without a value last — and keep the view's order
// (sort and filter, plan_views.h) inside a band.

#include "plan_types.h"

#include <QLocale>
#include <QStringList>

namespace QindaOffice::Plan {

struct TaskGroup {
    QString key;       // the value the band stands for, stable while it exists
    QString label;     // "Critical: Yes"
    QList<int> tasks;  // task indices, the view's order kept
    Minutes start = kNoDate;   // earliest start
    Minutes finish = kNoDate;  // latest finish
    Minutes duration = 0;      // working time from start to finish (project calendar)
    Minutes work = 0;
    qint64 cost = 0;
    int percentComplete = 0;   // duration-weighted, as a summary task's
};

[[nodiscard]] QStringList taskGroupIds(); // the built-in ones, "none" first
[[nodiscard]] bool isTaskGroupId(const QString &id);
[[nodiscard]] QString taskGroupTitle(const Project &project, const QString &id);
// The bands of `rows` (task indices, the view's order).
[[nodiscard]] QList<TaskGroup> groupTasks(const Project &project, const QList<int> &rows, const QString &id,
                                          Minutes reference, const QLocale &locale = QLocale());

} // namespace QindaOffice::Plan
