// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Which rows a task view shows, and in what order (M79, D-088): Project's
// built-in task filters (View ▸ Filter) and sorts (View ▸ Sort). Nothing
// here changes the project; the sheet, the Gantt, the Network Diagram and
// the printout all take the same rows from PlanService.
//
// AGENT-CONTRACT (filter ids, the app's planFilter…Action commands map onto
// them one to one): "all", "critical", "incomplete", "completed", "late",
// "slipping", "milestones", "summary", "deadlines", "resource" (Using
// Resource…, needs resourceUid), "dateRange" (Date Range…, needs from/to).
// "late" and "slipping" are plan_reports.h's isLate/isSlipped against the
// status date (or today). With related summary rows on, every match keeps
// its summary ancestors (Project's "Show related summary rows").
//
// AGENT-CONTRACT (sort ids, planSort…Action): "id", "start", "finish",
// "priority", "cost". Siblings are sorted under their summary (Project's
// "Keep outline structure"); ties keep the outline order. Start and finish
// ascend; priority and cost descend, as Project's built-in sorts do.

#include "plan_types.h"

#include <QDate>
#include <QStringList>

namespace QindaOffice::Plan {

struct TaskFilter {
    QString id = QStringLiteral("all");
    int resourceUid = 0; // "resource"
    QDate from;          // "dateRange": tasks working on any day in [from, to]
    QDate to;
};

[[nodiscard]] QStringList taskFilterIds();
[[nodiscard]] QString taskFilterTitle(const QString &id);
[[nodiscard]] bool taskMatches(const Project &project, int index, const TaskFilter &filter, Minutes reference);
// `rows` (task indices, display order) that match, in the same order.
[[nodiscard]] QList<int> filterRows(const Project &project, const QList<int> &rows, const TaskFilter &filter,
                                    Minutes reference, bool relatedSummaries = true);

[[nodiscard]] QStringList taskSortIds();
[[nodiscard]] QString taskSortTitle(const QString &id);
// Every task index in outline order with siblings sorted by `key`.
[[nodiscard]] QList<int> sortedOutline(const Project &project, const QString &key);

} // namespace QindaOffice::Plan
