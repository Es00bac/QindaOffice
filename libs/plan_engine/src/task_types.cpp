// SPDX-License-Identifier: GPL-3.0-or-later
// Project's task types and effort-driven scheduling (task_types.h has the
// contract and the representation).
#include "task_types.h"

#include "plan_time.h"

#include <algorithm>

namespace QindaOffice::Plan {

namespace {

constexpr int kMaxUnits = 1000000; // percent, as assignResource allows

int clampUnits(qint64 units)
{
    return int(std::clamp<qint64>(units, 1, kMaxUnits));
}

// `total` split in proportion to `weights` (sum > 0), adding up exactly
// (largest remainder; ties to the earlier entry).
QList<qint64> apportion(qint64 total, const QList<qint64> &weights)
{
    QList<qint64> out(weights.size(), 0);
    qint64 sum = 0;
    for (qint64 w : weights) {
        sum += std::max<qint64>(0, w);
    }
    if (sum <= 0 || weights.isEmpty()) {
        return out;
    }
    qint64 given = 0;
    QList<std::pair<qint64, int>> rest;
    for (int i = 0; i < weights.size(); ++i) {
        const qint64 exact = total * std::max<qint64>(0, weights.at(i));
        out[i] = exact / sum;
        given += out.at(i);
        rest.append({exact % sum, i});
    }
    std::stable_sort(rest.begin(), rest.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    for (int k = 0; given < total && k < rest.size(); ++k, ++given) {
        ++out[rest.at(k).second];
    }
    return out;
}

bool isLeaf(const Project &p, int index)
{
    return index >= 0 && index < p.tasks.size() && !p.tasks.at(index).summary;
}

QList<int> workAssignmentsOf(const Project &p, int taskUid)
{
    QList<int> out;
    for (int i = 0; i < p.assignments.size(); ++i) {
        if (p.assignments.at(i).taskUid == taskUid && isWorkAssignment(p, p.assignments.at(i))) {
            out.append(i);
        }
    }
    return out;
}

// Before one assignment changes the task's duration, the others keep their
// own span (they do not stretch with it).
void pinSpans(Project &p, const Task &t, const QList<int> &assignments, int except)
{
    for (int ai : assignments) {
        Assignment &a = p.assignments[ai];
        if (ai != except && a.workSpan < 0) {
            a.workSpan = t.duration;
        }
        if (ai != except && a.workInput < 0) {
            a.workInput = assignmentWork(t, a);
        }
    }
}

// The task lasts as long as its longest assignment; spans that reach the
// end are "the whole duration" again.
void fitDuration(Project &p, Task &t, const QList<int> &assignments)
{
    Minutes longest = 0;
    for (int ai : assignments) {
        longest = std::max(longest, std::max<Minutes>(0, p.assignments.at(ai).workSpan));
    }
    t.duration = longest;
    if (longest > 0) {
        t.milestone = false;
    }
    for (int ai : assignments) {
        Assignment &a = p.assignments[ai];
        if (a.workSpan >= longest) {
            a.workSpan = -1;
        }
    }
}

// Effort driving: `work` shared by units among `assignments`; Fixed Units and
// Fixed Work tasks last work ÷ total units, Fixed Duration tasks keep their
// duration and get new units.
void share(Project &p, Task &t, const QList<int> &assignments, Minutes work)
{
    QList<qint64> units;
    qint64 total = 0;
    for (int ai : assignments) {
        units.append(p.assignments.at(ai).units);
        total += p.assignments.at(ai).units;
    }
    const QList<qint64> parts = apportion(work, units);
    if (t.type != TaskType::FixedDuration && total > 0) {
        t.duration = divRound(work * 100, total);
        if (t.duration > 0) {
            t.milestone = false;
        }
    }
    for (int k = 0; k < assignments.size(); ++k) {
        Assignment &a = p.assignments[assignments.at(k)];
        a.workInput = parts.at(k);
        a.workSpan = -1;
        if (t.type == TaskType::FixedDuration && t.duration > 0) {
            a.units = clampUnits(divRound(parts.at(k) * 100, t.duration));
        }
    }
}

} // namespace

QString taskTypeName(TaskType type)
{
    switch (type) {
    case TaskType::FixedUnits: return QStringLiteral("Fixed Units");
    case TaskType::FixedDuration: return QStringLiteral("Fixed Duration");
    case TaskType::FixedWork: return QStringLiteral("Fixed Work");
    }
    return QStringLiteral("Fixed Units");
}

std::optional<TaskType> taskTypeFromName(const QString &text)
{
    const QString t = text.trimmed().toLower().remove(QLatin1Char(' ')).remove(QLatin1Char('-'));
    if (t == QLatin1String("fixedunits") || t == QLatin1String("units") || t == QLatin1String("0")) {
        return TaskType::FixedUnits;
    }
    if (t == QLatin1String("fixedduration") || t == QLatin1String("duration") || t == QLatin1String("1")) {
        return TaskType::FixedDuration;
    }
    if (t == QLatin1String("fixedwork") || t == QLatin1String("work") || t == QLatin1String("2")) {
        return TaskType::FixedWork;
    }
    return std::nullopt;
}

bool isWorkAssignment(const Project &project, const Assignment &assignment)
{
    const int r = project.resourceIndex(assignment.resourceUid);
    return r >= 0 && project.resources.at(r).type == ResourceType::Work;
}

Minutes assignmentSpan(const Task &task, const Assignment &assignment)
{
    const Minutes d = std::max<Minutes>(0, task.duration);
    return assignment.workSpan >= 0 ? std::min(assignment.workSpan, d) : d;
}

Minutes assignmentWork(const Task &task, const Assignment &assignment)
{
    if (task.isMilestone()) {
        return 0;
    }
    if (assignment.workInput >= 0) {
        return assignment.workInput;
    }
    return divRound(assignmentSpan(task, assignment) * assignment.units, 100);
}

EditResult setTaskDuration(Project &project, int index, Minutes duration)
{
    if (!isLeaf(project, index)) {
        return EditResult::fail(QStringLiteral("A summary task's duration comes from its subtasks."));
    }
    Task &t = project.tasks[index];
    duration = std::max<Minutes>(0, duration);
    const Minutes old = t.duration;
    const QList<int> assignments = workAssignmentsOf(project, t.uid);
    if (assignments.isEmpty() || old == duration) {
        t.duration = duration;
        return {};
    }
    Minutes work = 0;
    for (int ai : assignments) {
        work += assignmentWork(t, project.assignments.at(ai));
    }
    if (t.type == TaskType::FixedWork && duration == 0 && work > 0) {
        return EditResult::fail(QStringLiteral("A fixed-work task needs time to do its work in. "
                                               "Make it Fixed Units or Fixed Duration first, or remove its work."));
    }
    for (int ai : assignments) {
        Assignment &a = project.assignments[ai];
        if (old <= 0) {
            // A milestone had no work: its resources now work the duration.
            a.workInput = kWorkFollowsDuration;
            a.workSpan = -1;
            continue;
        }
        const Minutes span = assignmentSpan(t, a);
        const Minutes aWork = assignmentWork(t, a);
        const Minutes newSpan = a.workSpan < 0 ? duration : std::min(duration, divRound(a.workSpan * duration, old));
        a.workSpan = a.workSpan < 0 || newSpan >= duration ? -1 : newSpan;
        if (t.type == TaskType::FixedWork) {
            // Units are recalculated; the work stays.
            a.workInput = aWork;
            if (newSpan > 0) {
                a.units = clampUnits(divRound(aWork * 100, newSpan));
            }
        } else if (a.workInput >= 0) {
            // Work is recalculated at the assignment's own rate.
            a.workInput = span > 0 ? divRound(aWork * newSpan, span) : divRound(newSpan * a.units, 100);
        }
    }
    t.duration = duration;
    return {};
}

EditResult setTaskWork(Project &project, int index, Minutes work)
{
    if (!isLeaf(project, index)) {
        return EditResult::fail(QStringLiteral("A summary task's work comes from its subtasks."));
    }
    if (work < 0) {
        return EditResult::fail(QStringLiteral("Work cannot be negative."));
    }
    Task &t = project.tasks[index];
    const QList<int> assignments = workAssignmentsOf(project, t.uid);
    if (assignments.isEmpty()) {
        // Project keeps work typed on a task without resources; the first
        // resources assigned take it over.
        t.unassignedWork = work;
        return {};
    }
    if (t.type == TaskType::FixedDuration && t.duration <= 0 && work > 0) {
        return EditResult::fail(QStringLiteral("A fixed-duration task without a duration has no time for work. "
                                               "Give it a duration first."));
    }
    QList<qint64> weights;
    bool anyWork = false;
    for (int ai : assignments) {
        const Minutes w = assignmentWork(t, project.assignments.at(ai));
        weights.append(w);
        anyWork = anyWork || w > 0;
    }
    if (!anyWork) {
        for (int k = 0; k < assignments.size(); ++k) {
            weights[k] = project.assignments.at(assignments.at(k)).units;
        }
    }
    const QList<qint64> parts = apportion(work, weights);
    if (t.type == TaskType::FixedDuration) {
        // Units are recalculated; the duration stays.
        for (int k = 0; k < assignments.size(); ++k) {
            Assignment &a = project.assignments[assignments.at(k)];
            const Minutes span = assignmentSpan(t, a);
            a.workInput = parts.at(k);
            if (span > 0) {
                a.units = clampUnits(divRound(parts.at(k) * 100, span));
            }
        }
        return {};
    }
    // Fixed Units and Fixed Work: the duration is recalculated.
    for (int k = 0; k < assignments.size(); ++k) {
        Assignment &a = project.assignments[assignments.at(k)];
        a.workInput = parts.at(k);
        a.workSpan = divRound(parts.at(k) * 100, std::max(1, a.units));
    }
    fitDuration(project, t, assignments);
    return {};
}

EditResult setAssignmentUnits(Project &project, int assignmentIndex, int units)
{
    if (assignmentIndex < 0 || assignmentIndex >= project.assignments.size()) {
        return EditResult::fail(QStringLiteral("There is no such assignment."));
    }
    if (units <= 0 || units > kMaxUnits) {
        return EditResult::fail(QStringLiteral("Units must be more than zero."));
    }
    Assignment &a = project.assignments[assignmentIndex];
    const int ti = project.taskIndex(a.taskUid);
    if (ti < 0 || !isWorkAssignment(project, a)) {
        a.units = units; // a material's quantity
        return {};
    }
    Task &t = project.tasks[ti];
    if (a.units == units) {
        return {};
    }
    if (t.type == TaskType::FixedDuration) {
        // Work is recalculated over the same span.
        if (a.workInput >= 0) {
            a.workInput = divRound(assignmentSpan(t, a) * units, 100);
        }
        a.units = units;
        return {};
    }
    // Fixed Units and Fixed Work: the work stays, so the assignment takes
    // longer or less long; the task lasts its longest assignment.
    const QList<int> assignments = workAssignmentsOf(project, t.uid);
    const Minutes work = assignmentWork(t, a);
    if (t.duration <= 0 || work <= 0) {
        a.units = units;
        return {};
    }
    pinSpans(project, t, assignments, assignmentIndex);
    Assignment &changed = project.assignments[assignmentIndex];
    changed.workInput = work;
    changed.units = units;
    changed.workSpan = divRound(work * 100, units);
    fitDuration(project, t, assignments);
    return {};
}

EditResult setAssignmentWork(Project &project, int assignmentIndex, Minutes work)
{
    if (assignmentIndex < 0 || assignmentIndex >= project.assignments.size()) {
        return EditResult::fail(QStringLiteral("There is no such assignment."));
    }
    if (work < 0) {
        return EditResult::fail(QStringLiteral("Work cannot be negative."));
    }
    const Assignment &a = project.assignments.at(assignmentIndex);
    const int ti = project.taskIndex(a.taskUid);
    if (ti < 0 || !isWorkAssignment(project, a)) {
        return EditResult::fail(QStringLiteral("Only a work resource's assignment has work; type a material's quantity "
                                               "as its units."));
    }
    Task &t = project.tasks[ti];
    if (t.type == TaskType::FixedDuration) {
        const Minutes span = assignmentSpan(t, a);
        if (span <= 0 && work > 0) {
            return EditResult::fail(QStringLiteral("A fixed-duration task without a duration has no time for work."));
        }
        Assignment &changed = project.assignments[assignmentIndex];
        changed.workInput = work;
        if (span > 0) {
            changed.units = clampUnits(divRound(work * 100, span)); // units are recalculated
        }
        return {};
    }
    const QList<int> assignments = workAssignmentsOf(project, t.uid);
    pinSpans(project, t, assignments, assignmentIndex);
    Assignment &changed = project.assignments[assignmentIndex];
    changed.workInput = work;
    changed.workSpan = divRound(work * 100, std::max(1, changed.units)); // the duration is recalculated
    fitDuration(project, t, assignments);
    return {};
}

EditResult setTaskType(Project &project, int index, TaskType type)
{
    if (!isLeaf(project, index)) {
        return EditResult::fail(QStringLiteral("A summary task's type cannot be changed (it is fixed duration)."));
    }
    Task &t = project.tasks[index];
    t.type = type;
    if (type == TaskType::FixedWork) {
        t.effortDriven = true; // Project: a fixed-work task is always effort driven
    }
    return {};
}

EditResult setEffortDriven(Project &project, int index, bool on)
{
    if (!isLeaf(project, index)) {
        return EditResult::fail(QStringLiteral("A summary task is not effort driven."));
    }
    Task &t = project.tasks[index];
    if (!on && t.type == TaskType::FixedWork) {
        return EditResult::fail(QStringLiteral("A fixed-work task is always effort driven."));
    }
    t.effortDriven = on;
    return {};
}

EditResult changeAssignments(Project &project, int index, const QList<ResourceRequest> &added,
                             const QList<int> &removedResourceUids)
{
    if (!isLeaf(project, index)) {
        return EditResult::fail(QStringLiteral("Assign resources to subtasks, not to their summary task."));
    }
    const int taskUid = project.tasks.at(index).uid;
    for (const ResourceRequest &r : added) {
        if (project.resourceIndex(r.resourceUid) < 0) {
            return EditResult::fail(QStringLiteral("There is no such resource."));
        }
        if (r.units <= 0 || r.units > kMaxUnits) {
            return EditResult::fail(QStringLiteral("Units must be more than zero."));
        }
        if (project.assignmentIndex(taskUid, r.resourceUid) >= 0 && !removedResourceUids.contains(r.resourceUid)) {
            return EditResult::fail(QStringLiteral("%1 is already assigned to this task.")
                                        .arg(project.resources.at(project.resourceIndex(r.resourceUid)).name));
        }
    }
    Task &t = project.tasks[index];
    const QList<int> before = workAssignmentsOf(project, taskUid);
    Minutes work = 0;
    bool removedWork = false;
    for (int ai : before) {
        work += assignmentWork(t, project.assignments.at(ai));
        removedWork = removedWork || removedResourceUids.contains(project.assignments.at(ai).resourceUid);
    }
    project.assignments.removeIf([&](const Assignment &a) {
        return a.taskUid == taskUid && removedResourceUids.contains(a.resourceUid);
    });
    bool addedWork = false;
    for (const ResourceRequest &r : added) {
        Assignment a;
        a.uid = project.nextAssignmentUid();
        a.taskUid = taskUid;
        a.resourceUid = r.resourceUid;
        a.units = r.units;
        project.assignments.append(a);
        addedWork = addedWork || isWorkAssignment(project, a);
    }
    const QList<int> after = workAssignmentsOf(project, taskUid);
    if ((!addedWork && !removedWork) || after.isEmpty()) {
        return {}; // nothing for the formula, or everything gone (the duration stays)
    }
    if (before.isEmpty()) {
        // The first resources: they take the work typed on the task, if
        // any, else they work its duration (never effort driven).
        if (t.unassignedWork > 0) {
            share(project, t, after, t.unassignedWork);
            t.unassignedWork = 0;
        }
        return {};
    }
    if (t.effortDriven || t.type == TaskType::FixedWork) {
        share(project, t, after, work); // the work stays; the resources share it
        return {};
    }
    // Not effort driven: an added resource adds its own work over the whole
    // duration; a removed one takes its work along, and a Fixed Units task
    // shrinks to its longest remaining assignment.
    if (removedWork && t.type == TaskType::FixedUnits) {
        bool shorter = true;
        for (int ai : after) {
            shorter = shorter && project.assignments.at(ai).workSpan >= 0;
        }
        if (shorter) {
            fitDuration(project, t, after);
        }
    }
    return {};
}

} // namespace QindaOffice::Plan
