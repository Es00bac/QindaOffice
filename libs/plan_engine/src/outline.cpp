// SPDX-License-Identifier: GPL-3.0-or-later
#include "outline.h"

#include <QSet>

#include <algorithm>

namespace QindaOffice::Plan {

void computeStructure(Project &project)
{
    QList<Task> &tasks = project.tasks;
    QList<int> stack;          // indices of the open ancestors
    QList<int> counters{0};    // WBS counter per depth
    int previousLevel = 0;
    for (int i = 0; i < tasks.size(); ++i) {
        Task &t = tasks[i];
        t.outlineLevel = std::clamp(t.outlineLevel, 1, previousLevel + 1);
        t.id = i + 1;
        while (stack.size() >= t.outlineLevel) {
            stack.removeLast();
        }
        t.parentIndex = stack.isEmpty() ? -1 : stack.last();
        while (counters.size() > t.outlineLevel) {
            counters.removeLast();
        }
        while (counters.size() < t.outlineLevel) {
            counters.append(0);
        }
        ++counters.last();
        QStringList parts;
        for (int c : std::as_const(counters)) {
            parts.append(QString::number(c));
        }
        t.wbs = parts.join(QLatin1Char('.'));
        t.effectiveActive = t.active && (t.parentIndex < 0 || tasks.at(t.parentIndex).effectiveActive);
        t.summary = false;
        if (t.parentIndex >= 0) {
            tasks[t.parentIndex].summary = true;
        }
        stack.append(i);
        counters.append(0);
        previousLevel = t.outlineLevel;
    }
}

int subtreeEnd(const Project &project, int index)
{
    const QList<Task> &tasks = project.tasks;
    if (index < 0 || index >= tasks.size()) {
        return index + 1;
    }
    const int level = tasks.at(index).outlineLevel;
    int end = index + 1;
    while (end < tasks.size() && tasks.at(end).outlineLevel > level) {
        ++end;
    }
    return end;
}

QList<int> childrenOf(const Project &project, int index)
{
    QList<int> out;
    const int end = subtreeEnd(project, index);
    const int level = index >= 0 ? project.tasks.at(index).outlineLevel : 0;
    for (int i = index + 1; i < end; ++i) {
        if (project.tasks.at(i).outlineLevel == level + 1) {
            out.append(i);
        }
    }
    return out;
}

bool isAncestor(const Project &project, int ancestor, int index)
{
    return ancestor >= 0 && index > ancestor && index < subtreeEnd(project, ancestor);
}

QList<int> selectionRoots(const Project &project, QList<int> indices)
{
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    QList<int> roots;
    for (int i : std::as_const(indices)) {
        if (i < 0 || i >= project.tasks.size()) {
            continue;
        }
        if (!roots.isEmpty() && isAncestor(project, roots.last(), i)) {
            continue;
        }
        roots.append(i);
    }
    return roots;
}

bool indentTasks(Project &project, const QList<int> &indices, QString *error)
{
    const QList<int> roots = selectionRoots(project, indices);
    if (roots.isEmpty()) {
        return false;
    }
    Project copy = project;
    for (int root : roots) {
        if (root == 0) {
            if (error) {
                *error = QStringLiteral("The first task cannot be indented.");
            }
            return false;
        }
        const int level = copy.tasks.at(root).outlineLevel;
        if (copy.tasks.at(root - 1).outlineLevel < level) {
            if (error) {
                *error = QStringLiteral("“%1” is already one level below the task above it.").arg(copy.tasks.at(root).name);
            }
            return false;
        }
        const int end = subtreeEnd(copy, root);
        for (int i = root; i < end; ++i) {
            ++copy.tasks[i].outlineLevel;
        }
    }
    project = copy;
    computeStructure(project);
    return true;
}

bool outdentTasks(Project &project, const QList<int> &indices, QString *error)
{
    const QList<int> roots = selectionRoots(project, indices);
    if (roots.isEmpty()) {
        return false;
    }
    Project copy = project;
    for (int root : roots) {
        if (copy.tasks.at(root).outlineLevel <= 1) {
            if (error) {
                *error = QStringLiteral("“%1” is already at the top level.").arg(copy.tasks.at(root).name);
            }
            return false;
        }
        const int end = subtreeEnd(copy, root);
        for (int i = root; i < end; ++i) {
            --copy.tasks[i].outlineLevel;
        }
    }
    project = copy;
    computeStructure(project);
    return true;
}

int moveTask(Project &project, int index, bool up)
{
    if (index < 0 || index >= project.tasks.size()) {
        return -1;
    }
    const int level = project.tasks.at(index).outlineLevel;
    const int end = subtreeEnd(project, index);
    if (up) {
        // The previous sibling: the nearest earlier task at the same level
        // with nothing shallower in between.
        int sibling = index - 1;
        while (sibling >= 0 && project.tasks.at(sibling).outlineLevel > level) {
            --sibling;
        }
        if (sibling < 0 || project.tasks.at(sibling).outlineLevel != level) {
            return -1;
        }
        const QList<Task> block = project.tasks.mid(index, end - index);
        project.tasks.remove(index, end - index);
        for (int k = 0; k < block.size(); ++k) {
            project.tasks.insert(sibling + k, block.at(k));
        }
        computeStructure(project);
        return sibling;
    }
    if (end >= project.tasks.size() || project.tasks.at(end).outlineLevel != level) {
        return -1;
    }
    const int siblingEnd = subtreeEnd(project, end);
    const QList<Task> block = project.tasks.mid(index, end - index);
    project.tasks.remove(index, end - index);
    const int insertAt = siblingEnd - block.size();
    for (int k = 0; k < block.size(); ++k) {
        project.tasks.insert(insertAt + k, block.at(k));
    }
    computeStructure(project);
    return insertAt;
}

int insertTask(Project &project, int index, Task task)
{
    index = std::clamp(index, 0, int(project.tasks.size()));
    if (task.uid <= 0 || project.taskIndex(task.uid) >= 0) {
        task.uid = project.nextTaskUid();
    }
    if (task.guid.isNull() || project.taskIndexByGuid(task.guid) >= 0) {
        task.guid = QUuid::createUuid();
    }
    if (index < project.tasks.size()) {
        task.outlineLevel = project.tasks.at(index).outlineLevel;
    } else if (!project.tasks.isEmpty()) {
        // Appending after a summary's last child keeps the child's level.
        task.outlineLevel = project.tasks.last().outlineLevel;
    } else {
        task.outlineLevel = 1;
    }
    project.tasks.insert(index, task);
    computeStructure(project);
    return index;
}

QList<int> removeTasks(Project &project, const QList<int> &indices)
{
    const QList<int> roots = selectionRoots(project, indices);
    QList<int> removed;
    for (auto it = roots.crbegin(); it != roots.crend(); ++it) {
        const int end = subtreeEnd(project, *it);
        for (int i = *it; i < end; ++i) {
            removed.append(project.tasks.at(i).uid);
        }
        project.tasks.remove(*it, end - *it);
    }
    const QSet<int> gone(removed.cbegin(), removed.cend());
    for (Task &t : project.tasks) {
        t.predecessors.removeIf([&gone](const Link &l) { return gone.contains(l.predecessorUid); });
    }
    project.assignments.removeIf([&gone](const Assignment &a) { return gone.contains(a.taskUid); });
    computeStructure(project);
    return removed;
}

} // namespace QindaOffice::Plan
