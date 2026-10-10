// SPDX-License-Identifier: GPL-3.0-or-later
// The ribbon's task and resource operations (plan_edit.h).
#include "outline.h"
#include "plan_edit.h"
#include "plan_time.h"
#include "scheduler.h"
#include "task_types.h"
#include "work_calendar.h"

#include <QSet>

#include <algorithm>

namespace QindaOffice::Plan {

namespace {

// The leaf tasks under the given rows (a summary stands for its subtasks).
QList<int> leavesOf(const Project &project, const QList<int> &indices)
{
    QList<int> out;
    for (int i : indices) {
        if (i < 0 || i >= project.tasks.size()) {
            continue;
        }
        const int end = subtreeEnd(project, i);
        for (int k = i; k < end; ++k) {
            if (!project.tasks.at(k).summary && !out.contains(k)) {
                out.append(k);
            }
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

QString uniqueResourceName(const Project &project, const QString &name)
{
    QString candidate = name.trimmed().isEmpty() ? QStringLiteral("Resource") : name.trimmed();
    const QString base = candidate;
    for (int n = 2; project.resourceIndexByName(candidate) >= 0; ++n) {
        candidate = QStringLiteral("%1 %2").arg(base).arg(n);
    }
    return candidate;
}

} // namespace

int addTask(Project &project, int at, NewTaskKind kind, const QString &name)
{
    Task t;
    t.name = name;
    t.durationUnit = DurationUnit::Days;
    t.duration = kind == NewTaskKind::Milestone ? 0 : project.info.minutesPerDay;
    t.estimated = kind == NewTaskKind::Task;
    t.milestone = kind == NewTaskKind::Milestone;
    // M83: Project Options' default task type and effort driving.
    t.type = project.info.defaultTaskType;
    t.effortDriven = project.info.newTasksEffortDriven || t.type == TaskType::FixedWork;
    const int index = insertTask(project, at, t);
    if (kind == NewTaskKind::Summary) {
        Task child;
        child.name = QStringLiteral("New subtask");
        child.duration = project.info.minutesPerDay;
        child.estimated = true;
        const int c = insertTask(project, index + 1, child);
        project.tasks[c].outlineLevel = project.tasks.at(index).outlineLevel + 1;
        computeStructure(project);
    }
    return index;
}

int insertSummaryAbove(Project &project, const QList<int> &indices, const QString &name)
{
    const QList<int> roots = selectionRoots(project, indices);
    if (roots.isEmpty()) {
        return -1;
    }
    Project copy = project;
    Task summary;
    summary.name = name;
    const int at = insertTask(copy, roots.first(), summary);
    QList<int> shifted;
    for (int r : roots) {
        shifted.append(r + 1);
    }
    if (!indentTasks(copy, shifted)) {
        return -1;
    }
    project = copy;
    return at;
}

EditResult linkTasks(Project &project, QList<int> indices, LinkType type)
{
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    if (indices.size() < 2) {
        return EditResult::fail(QStringLiteral("Select two or more tasks to link."));
    }
    Project copy = project;
    for (int k = 1; k < indices.size(); ++k) {
        const QString problem = linkProblem(copy, indices.at(k - 1), indices.at(k), type);
        if (problem.contains(QStringLiteral("already linked"))) {
            continue;
        }
        if (!problem.isEmpty()) {
            return EditResult::fail(QStringLiteral("Tasks %1 and %2: %3")
                                        .arg(copy.tasks.at(indices.at(k - 1)).id)
                                        .arg(copy.tasks.at(indices.at(k)).id)
                                        .arg(problem));
        }
        Link l;
        l.predecessorUid = copy.tasks.at(indices.at(k - 1)).uid;
        l.type = type;
        copy.tasks[indices.at(k)].predecessors.append(l);
    }
    project = copy;
    return {};
}

int unlinkTasks(Project &project, const QList<int> &indices)
{
    QSet<int> uids;
    for (int i : indices) {
        if (i >= 0 && i < project.tasks.size()) {
            uids.insert(project.tasks.at(i).uid);
        }
    }
    int removed = 0;
    const bool single = uids.size() == 1;
    for (Task &t : project.tasks) {
        const bool inside = uids.contains(t.uid);
        removed += int(t.predecessors.removeIf([&](const Link &l) {
            const bool fromInside = uids.contains(l.predecessorUid);
            return single ? (inside || fromInside) : (inside && fromInside);
        }));
    }
    return removed;
}

void setPercentComplete(Project &project, const QList<int> &indices, int percent)
{
    percent = std::clamp(percent, 0, 100);
    for (int i : leavesOf(project, indices)) {
        Task &t = project.tasks[i];
        t.percentComplete = percent;
        if (percent == 0) {
            t.actualStart = kNoDate;
            t.actualFinish = kNoDate;
            continue;
        }
        if (t.actualStart == kNoDate) {
            t.actualStart = t.start;
        }
        if (percent == 100) {
            if (t.actualFinish == kNoDate) {
                t.actualFinish = t.finish;
            }
        } else {
            t.actualFinish = kNoDate;
        }
    }
}

void setManual(Project &project, const QList<int> &indices, bool manual)
{
    for (int i : indices) {
        if (i < 0 || i >= project.tasks.size() || project.tasks.at(i).summary) {
            continue;
        }
        Task &t = project.tasks[i];
        if (t.manual == manual) {
            continue;
        }
        t.manual = manual;
        t.manualStart = manual ? t.start : kNoDate;
        t.manualFinish = manual ? t.finish : kNoDate;
    }
}

void setActive(Project &project, const QList<int> &indices, bool active)
{
    for (int i : indices) {
        if (i >= 0 && i < project.tasks.size()) {
            project.tasks[i].active = active;
        }
    }
}

void respectLinks(Project &project, const QList<int> &indices)
{
    Project automatic = project;
    for (int i : indices) {
        if (i < 0 || i >= project.tasks.size()) {
            continue;
        }
        Task &t = automatic.tasks[i];
        t.manual = false;
        if (t.constraint == ConstraintType::StartNoEarlierThan || t.constraint == ConstraintType::FinishNoEarlierThan) {
            t.constraint = ConstraintType::AsSoonAsPossible;
            t.constraintDate = kNoDate;
        }
    }
    schedule(automatic);
    for (int i : indices) {
        if (i < 0 || i >= project.tasks.size()) {
            continue;
        }
        Task &t = project.tasks[i];
        if (t.manual) {
            t.manualStart = automatic.tasks.at(i).start;
            t.manualFinish = automatic.tasks.at(i).finish;
        } else {
            t.constraint = automatic.tasks.at(i).constraint;
            t.constraintDate = automatic.tasks.at(i).constraintDate;
        }
    }
}

EditResult moveTaskStart(Project &project, int index, Minutes start)
{
    if (index < 0 || index >= project.tasks.size() || start == kNoDate) {
        return EditResult::fail(QStringLiteral("There is no such task."));
    }
    Task &t = project.tasks[index];
    if (t.summary) {
        return EditResult::fail(QStringLiteral("Move a summary task by moving its subtasks."));
    }
    if (t.actualStart != kNoDate) {
        return EditResult::fail(QStringLiteral("“%1” has started; change its actual start instead.").arg(t.name));
    }
    const WorkCalendar c(project, effectiveCalendarUid(project, index));
    const Minutes s = t.duration > 0 ? c.snapForward(start) : start;
    if (t.manual) {
        t.manualStart = s;
        t.manualFinish = t.duration > 0 ? c.add(s, t.duration) : s;
    } else {
        t.constraint = ConstraintType::StartNoEarlierThan;
        t.constraintDate = s;
    }
    return {};
}

void updateProgress(Project &project, Minutes statusDate, const QList<int> &indices)
{
    QList<int> rows = indices;
    if (rows.isEmpty()) {
        for (int i = 0; i < project.tasks.size(); ++i) {
            rows.append(i);
        }
    }
    for (int i : leavesOf(project, rows)) {
        const Task &t = project.tasks.at(i);
        if (!t.effectiveActive || t.percentComplete >= 100 || t.start == kNoDate || t.start >= statusDate) {
            continue;
        }
        if (t.finish <= statusDate) {
            setPercentComplete(project, {i}, 100);
            continue;
        }
        const WorkCalendar c(project, effectiveCalendarUid(project, i));
        const Minutes done = c.workBetween(t.start, statusDate);
        const int pct = t.duration > 0 ? int(std::clamp<qint64>(done * 100 / t.duration, 0, 99)) : 0;
        if (pct > t.percentComplete) {
            setPercentComplete(project, {i}, pct);
        }
    }
}

EditResult assignResource(Project &project, int taskIndex, int resourceUid, int units)
{
    if (taskIndex < 0 || taskIndex >= project.tasks.size() || project.resourceIndex(resourceUid) < 0) {
        return EditResult::fail(QStringLiteral("There is no such task or resource."));
    }
    if (project.tasks.at(taskIndex).summary) {
        return EditResult::fail(QStringLiteral("Assign resources to subtasks, not to their summary task."));
    }
    if (units <= 0 || units > 1000000) {
        return EditResult::fail(QStringLiteral("Units must be more than zero."));
    }
    // M83: Project's task types decide what new units or a new resource
    // change (task_types.h).
    const int existing = project.assignmentIndex(project.tasks.at(taskIndex).uid, resourceUid);
    if (existing >= 0) {
        return setAssignmentUnits(project, existing, units);
    }
    return changeAssignments(project, taskIndex, {{resourceUid, units}}, {});
}

EditResult unassignResource(Project &project, int taskIndex, int resourceUid)
{
    if (taskIndex < 0 || taskIndex >= project.tasks.size()) {
        return EditResult::fail(QStringLiteral("There is no such task."));
    }
    if (project.assignmentIndex(project.tasks.at(taskIndex).uid, resourceUid) < 0) {
        return EditResult::fail(QStringLiteral("That resource is not assigned to this task."));
    }
    return changeAssignments(project, taskIndex, {}, {resourceUid});
}

int addResource(Project &project, int at, const QString &name, ResourceType type)
{
    Resource r;
    r.uid = project.nextResourceUid();
    r.name = uniqueResourceName(project, name);
    r.type = type;
    for (const QString &word : r.name.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        r.initials.append(word.at(0).toUpper());
    }
    r.initials = r.initials.left(3);
    at = std::clamp(at, 0, int(project.resources.size()));
    project.resources.insert(at, r);
    return at;
}

void removeResources(Project &project, const QList<int> &indices)
{
    QSet<int> uids;
    for (int i : indices) {
        if (i >= 0 && i < project.resources.size()) {
            uids.insert(project.resources.at(i).uid);
        }
    }
    project.resources.removeIf([&](const Resource &r) { return uids.contains(r.uid); });
    project.assignments.removeIf([&](const Assignment &a) { return uids.contains(a.resourceUid); });
}

int ensureResourceCalendar(Project &project, int resourceIndex)
{
    if (resourceIndex < 0 || resourceIndex >= project.resources.size()) {
        return -1;
    }
    Resource &r = project.resources[resourceIndex];
    const int existing = project.calendarIndex(r.calendarUid);
    if (existing >= 0 && project.calendars.at(existing).baseUid >= 0) {
        return r.calendarUid;
    }
    Calendar c;
    c.uid = project.nextCalendarUid();
    c.name = r.name;
    c.baseUid = existing >= 0 ? r.calendarUid : project.info.calendarUid;
    project.calendars.append(c);
    r.calendarUid = c.uid;
    return c.uid;
}

} // namespace QindaOffice::Plan
