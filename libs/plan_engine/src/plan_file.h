// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The .qplan file (D-051): UTF-8 JSON, `{"format": "qindaplan",
// "version": 1, …}`, written atomically (QSaveFile).
//
// AGENT-CONTRACT (compatibility, docs/plan.md "File format"):
//  * `version` is the format's major number; a file with a higher one is
//    refused with a message naming it, never half-read.
//  * Every object keeps the keys this version does not know (Task::extra,
//    Resource::extra, …, Project::extra) and writes them back unchanged, so
//    a file from a newer minor revision survives a round trip.
//  * Only inputs are stored; computed fields (dates, slack, cost, …) are
//    recalculated on load. Instants are wall-clock ISO strings
//    ("2026-03-02T08:00"), spans are working minutes, money is minor units.
//  * The copy/paste format for tasks is the same task JSON inside
//    `{"format": "qindaplan-tasks", …}`.

#include "plan_types.h"

#include <QByteArray>
#include <QLocale>
#include <QStringList>

namespace QindaOffice::Plan {

inline constexpr int kPlanFormatVersion = 1;

struct LoadResult {
    bool ok = false;
    QString error;
    QStringList warnings;
};

[[nodiscard]] QByteArray writePlanJson(const Project &project);
// Replaces *out and schedules it.
LoadResult readPlanJson(const QByteArray &json, Project *out);
bool savePlanFile(const QString &path, const Project &project, QString *error = nullptr);
LoadResult loadPlanFile(const QString &path, Project *out);

// ---- clipboard ----
inline constexpr const char *kTasksMimeType = "application/x-qindaplan-tasks";
// The tasks and their subtrees, the links among them and their
// assignments (by resource name, so a paste into another plan finds or
// creates the resources).
[[nodiscard]] QByteArray copyTasks(const Project &project, const QList<int> &indices);
// Tab-separated text for other applications: ID, name, duration, start,
// finish, predecessors, resource names — with a header row.
[[nodiscard]] QString copyTasksText(const Project &project, const QList<int> &indices, const QLocale &locale = QLocale());
// Inserts the copied tasks at row `at` (fresh uids/guids, links remapped,
// levels shifted to fit there). Returns the new rows; empty when the data
// is not a task clipboard.
QList<int> pasteTasks(Project &project, int at, const QByteArray &data);

// CSV of the task sheet with the given columns (plan_edit.h field ids),
// RFC 4180 quoting, UTF-8 with a header row of the column titles.
[[nodiscard]] QByteArray taskSheetCsv(const Project &project, const QStringList &fields, const QLocale &locale = QLocale());

} // namespace QindaOffice::Plan
