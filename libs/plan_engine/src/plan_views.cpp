// SPDX-License-Identifier: GPL-3.0-or-later
// Project's task filters and sorts (plan_views.h).
#include "plan_views.h"

#include "outline.h"
#include "plan_reports.h"
#include "plan_time.h"

#include <QSet>

#include <algorithm>

namespace QindaOffice::Plan {

namespace {

struct Named {
    const char *id;
    const char *title;
};

constexpr Named kFilters[] = {
    {"all", "All Tasks"},
    {"critical", "Critical"},
    {"incomplete", "Incomplete Tasks"},
    {"completed", "Completed Tasks"},
    {"late", "Late Tasks"},
    {"slipping", "Slipping Tasks"},
    {"milestones", "Milestones"},
    {"summary", "Summary Tasks"},
    {"deadlines", "Tasks With Deadlines"},
    {"resource", "Using Resource…"},
    {"dateRange", "Date Range…"},
};

constexpr Named kSorts[] = {
    {"id", "by ID"}, {"start", "by Start Date"}, {"finish", "by Finish Date"}, {"priority", "by Priority"}, {"cost", "by Cost"},
};

template<std::size_t N>
QString titleOf(const Named (&list)[N], const QString &id)
{
    for (const Named &n : list) {
        if (id == QLatin1String(n.id)) {
            return QString::fromUtf8(n.title);
        }
    }
    return id;
}

template<std::size_t N>
QStringList idsOf(const Named (&list)[N])
{
    QStringList out;
    for (const Named &n : list) {
        out.append(QLatin1String(n.id));
    }
    return out;
}

bool usesResource(const Project &project, const Task &t, int resourceUid)
{
    for (const Assignment &a : project.assignments) {
        if (a.taskUid == t.uid && a.resourceUid == resourceUid) {
            return true;
        }
    }
    return false;
}

} // namespace

QStringList taskFilterIds()
{
    return idsOf(kFilters);
}

QString taskFilterTitle(const QString &id)
{
    return titleOf(kFilters, id);
}

bool taskMatches(const Project &project, int index, const TaskFilter &filter, Minutes reference)
{
    if (index < 0 || index >= project.tasks.size()) {
        return false;
    }
    const Task &t = project.tasks.at(index);
    const QString &id = filter.id;
    if (id == QLatin1String("critical")) {
        return t.critical;
    }
    if (id == QLatin1String("incomplete")) {
        return t.percentComplete < 100;
    }
    if (id == QLatin1String("completed")) {
        return t.percentComplete >= 100;
    }
    if (id == QLatin1String("late")) {
        return isLate(project, t, reference);
    }
    if (id == QLatin1String("slipping")) {
        return isSlipped(project, t);
    }
    if (id == QLatin1String("milestones")) {
        return !t.summary && t.isMilestone();
    }
    if (id == QLatin1String("summary")) {
        return t.summary;
    }
    if (id == QLatin1String("deadlines")) {
        return t.deadline != kNoDate;
    }
    if (id == QLatin1String("resource")) {
        return !t.summary && usesResource(project, t, filter.resourceUid);
    }
    if (id == QLatin1String("dateRange")) {
        if (!filter.from.isValid() || !filter.to.isValid() || t.start == kNoDate) {
            return false;
        }
        // Working on any day of the range: starts before it ends and
        // finishes after it begins (a milestone on its first day counts).
        return t.start < toMinutes(filter.to.addDays(1), 0) && t.finish >= toMinutes(filter.from, 0);
    }
    return true; // "all" and anything unknown
}

QList<int> filterRows(const Project &project, const QList<int> &rows, const TaskFilter &filter, Minutes reference,
                      bool relatedSummaries)
{
    if (filter.id == QLatin1String("all") || filter.id.isEmpty()) {
        return rows;
    }
    QSet<int> keep;
    for (int i : rows) {
        if (!taskMatches(project, i, filter, reference)) {
            continue;
        }
        keep.insert(i);
        if (relatedSummaries) {
            for (int a = project.tasks.at(i).parentIndex; a >= 0; a = project.tasks.at(a).parentIndex) {
                keep.insert(a);
            }
        }
    }
    QList<int> out;
    for (int i : rows) {
        if (keep.contains(i)) {
            out.append(i);
        }
    }
    return out;
}

QStringList taskSortIds()
{
    return idsOf(kSorts);
}

QString taskSortTitle(const QString &id)
{
    return titleOf(kSorts, id);
}

QList<int> sortedOutline(const Project &project, const QString &key)
{
    const int n = int(project.tasks.size());
    QList<int> roots;
    QList<QList<int>> children(n);
    for (int i = 0; i < n; ++i) {
        const int parent = project.tasks.at(i).parentIndex;
        (parent >= 0 && parent < n ? children[parent] : roots).append(i);
    }
    const auto less = [&](int a, int b) {
        const Task &x = project.tasks.at(a);
        const Task &y = project.tasks.at(b);
        if (key == QLatin1String("start") && x.start != y.start) {
            return x.start < y.start;
        }
        if (key == QLatin1String("finish") && x.finish != y.finish) {
            return x.finish < y.finish;
        }
        if (key == QLatin1String("priority") && x.priority != y.priority) {
            return x.priority > y.priority;
        }
        if (key == QLatin1String("cost") && x.cost != y.cost) {
            return x.cost > y.cost;
        }
        return a < b;
    };
    QList<int> out;
    out.reserve(n);
    // Depth-first with an explicit stack (outlines can be deep).
    std::stable_sort(roots.begin(), roots.end(), less);
    QList<int> stack(roots.crbegin(), roots.crend());
    while (!stack.isEmpty()) {
        const int i = stack.takeLast();
        out.append(i);
        QList<int> kids = children.at(i);
        std::stable_sort(kids.begin(), kids.end(), less);
        for (auto it = kids.crbegin(); it != kids.crend(); ++it) {
            stack.append(*it);
        }
    }
    return out;
}

} // namespace QindaOffice::Plan
