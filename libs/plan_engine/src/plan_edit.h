// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Editing a Project the way a Project user does: through the fields of the
// task and resource sheets (text in, text out) and the ribbon's operations.
// Nothing here schedules; the caller runs schedule() after a successful
// edit (the app does it once per undo step). Every function leaves the
// project untouched when it refuses.
//
// AGENT-CONTRACT (field ids; the app's sheet columns use them verbatim,
// docs/plan.md lists them): task fields "id" "wbs" "indicators" "mode"
// "name" "duration" "start" "finish" "predecessors" "successors"
// "resources" "percent" "work" "cost" "fixedCost" "actualCost"
// "remainingCost" "notes" "constraint" "constraintDate" "actualStart"
// "actualFinish" "baselineStart" "baselineFinish" "baselineDuration"
// "baselineCost" "startVariance" "finishVariance" "durationVariance"
// "costVariance" "totalSlack" "freeSlack" "earlyStart" "earlyFinish"
// "lateStart" "lateFinish" "active" "milestone" "calendar" "critical";
// resource fields "id" "indicators" "name" "type" "materialLabel"
// "initials" "group" "maxUnits" "standardRate" "costPerUse" "calendar"
// "email" "work" "cost" "overallocated".
//
// AGENT-NOTE (Project semantics kept on purpose): typing a start on an
// auto-scheduled task sets Start No Earlier Than; typing a finish sets
// Finish No Earlier Than; a constraint date typed on an ASAP task makes it
// SNET; % complete > 0 records an actual start, 100% an actual finish;
// Resource Names creates resources it does not know.

#include "plan_types.h"

#include <QLocale>
#include <QStringList>

#include <optional>

namespace QindaOffice::Plan {

struct EditResult {
    bool ok = true;
    QString error;
    static EditResult fail(const QString &message) { return {false, message}; }
};

// ---- the task sheet ----
[[nodiscard]] QStringList taskFieldIds();
[[nodiscard]] bool isTaskFieldEditable(const QString &field);
[[nodiscard]] QString taskFieldTitle(const QString &field);
[[nodiscard]] QString taskFieldText(const Project &project, int index, const QString &field,
                                    const QLocale &locale = QLocale());
// What an editor starts from (notes in full, dates with their time when it
// is not the default start/finish time).
[[nodiscard]] QString taskFieldEditText(const Project &project, int index, const QString &field,
                                        const QLocale &locale = QLocale());
EditResult setTaskField(Project &project, int index, const QString &field, const QString &text,
                        const QLocale &locale = QLocale());

// "3, 4SS+2 days, 5FF-1 day, 6FS+50%" by task ID (row number).
[[nodiscard]] QString predecessorsText(const Project &project, int index, const QLocale &locale = QLocale());
[[nodiscard]] QString successorsText(const Project &project, int index, const QLocale &locale = QLocale());
[[nodiscard]] std::optional<QList<Link>> parsePredecessors(const Project &project, int index, const QString &text,
                                                           QString *error, const QLocale &locale = QLocale());
// "Ana[50%], Ben, Paper[200 sheets]".
[[nodiscard]] QString resourceNamesText(const Project &project, int index, const QLocale &locale = QLocale());
EditResult setResourceNames(Project &project, int index, const QString &text, const QLocale &locale = QLocale());

// ---- the resource sheet ----
[[nodiscard]] QStringList resourceFieldIds();
[[nodiscard]] bool isResourceFieldEditable(const QString &field);
[[nodiscard]] QString resourceFieldTitle(const QString &field);
[[nodiscard]] QString resourceFieldText(const Project &project, int index, const QString &field,
                                        const QLocale &locale = QLocale());
EditResult setResourceField(Project &project, int index, const QString &field, const QString &text,
                            const QLocale &locale = QLocale());
[[nodiscard]] QString rateText(const Project &project, const Resource &resource, const QLocale &locale = QLocale());

// ---- operations (task indices are rows of Project::tasks) ----
enum class NewTaskKind { Task, Milestone, Summary };
// A new task at `at` (a summary gets one subtask so it is a summary).
// Returns its index.
int addTask(Project &project, int at, NewTaskKind kind, const QString &name);
// Wraps the selected tasks in a new summary above the first of them.
int insertSummaryAbove(Project &project, const QList<int> &indices, const QString &name);
// Links the tasks in row order (1→2→3…); fails when a link is refused.
EditResult linkTasks(Project &project, QList<int> indices, LinkType type = LinkType::FinishToStart);
// Removes the links among the tasks; one task: all its links. Returns how many.
int unlinkTasks(Project &project, const QList<int> &indices);
// Summaries apply to their subtasks.
void setPercentComplete(Project &project, const QList<int> &indices, int percent);
void setManual(Project &project, const QList<int> &indices, bool manual);
void setActive(Project &project, const QList<int> &indices, bool active);
// Manual tasks move to where their links put them; auto tasks drop a
// Start/Finish No Earlier Than so the links drive them again.
void respectLinks(Project &project, const QList<int> &indices);
// A Gantt drag: auto → Start No Earlier Than at the (working-time) start;
// manual → both dates move.
EditResult moveTaskStart(Project &project, int index, Minutes start);
// "Update Project": work complete through the status date.
void updateProgress(Project &project, Minutes statusDate, const QList<int> &indices = {});

EditResult assignResource(Project &project, int taskIndex, int resourceUid, int units);
EditResult unassignResource(Project &project, int taskIndex, int resourceUid);
int addResource(Project &project, int at, const QString &name, ResourceType type = ResourceType::Work);
// Removes resources and their assignments.
void removeResources(Project &project, const QList<int> &indices);
// A calendar of the resource's own (based on the project calendar),
// created on first use. Returns its uid.
int ensureResourceCalendar(Project &project, int resourceIndex);

} // namespace QindaOffice::Plan
