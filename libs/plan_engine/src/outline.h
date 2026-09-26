// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The task outline: Project::tasks is in outline order and each task's
// outlineLevel places it (Microsoft Project's model). A task is a summary
// when the next task is deeper; its subtree is the run of deeper tasks
// after it. These helpers derive structure and apply the structural edits
// (indent, outdent, move, insert, delete); none of them reschedules.

#include "plan_types.h"

namespace QindaOffice::Plan {

// Fills Task::id, parentIndex, summary, wbs and effectiveActive and clamps
// outline levels (the first task is level 1, a task is at most one level
// deeper than the one before it).
void computeStructure(Project &project);

// Exclusive end of the subtree rooted at `index` (index + 1 for a leaf).
[[nodiscard]] int subtreeEnd(const Project &project, int index);
[[nodiscard]] QList<int> childrenOf(const Project &project, int index);
[[nodiscard]] bool isAncestor(const Project &project, int ancestor, int index);
// Roots of `indices`: drops every index whose ancestor is also listed, sorted.
[[nodiscard]] QList<int> selectionRoots(const Project &project, QList<int> indices);

// Structural edits. Each returns false (and leaves the project untouched)
// when the edit is impossible, with a reason in `error`.
bool indentTasks(Project &project, const QList<int> &indices, QString *error = nullptr);
bool outdentTasks(Project &project, const QList<int> &indices, QString *error = nullptr);
// Moves the task and its subtree above the previous sibling (up) or below
// the next sibling (down); returns the task's new index, -1 when it cannot.
int moveTask(Project &project, int index, bool up);
// Inserts `task` at `index` (clamped); its level is taken from the task
// that was at `index` (or the previous task when appending). A fresh uid
// and guid are assigned when unset/taken. Returns the index.
int insertTask(Project &project, int index, Task task);
// Removes tasks and their subtrees, their assignments and every link that
// pointed at them. Returns the uids removed.
QList<int> removeTasks(Project &project, const QList<int> &indices);

} // namespace QindaOffice::Plan
