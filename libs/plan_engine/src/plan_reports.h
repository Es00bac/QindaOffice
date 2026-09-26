// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The reports (Report tab): project overview, critical tasks, resource
// overview, late tasks — built once as data (figures as text, already
// formatted), shown by the app's report view and painted for print and PDF
// by plan_output's report painter, so screen and paper say the same thing.
//
// AGENT-CONTRACT ("late", docs/plan.md "Reports"): against the status date
// (or `today` when none is set), an active, unfinished leaf task is late
// when the work it should have done by then (working time from its start,
// as a share of its duration) exceeds its % complete; a task whose finish
// moved past its baseline finish is "slipped". Both are listed.

#include "plan_types.h"

#include <QLocale>
#include <QStringList>

namespace QindaOffice::Plan {

struct ReportKpi {
    QString id;
    QString label;
    QString value;
    QString detail;
    int percent = -1; // 0..100 when the figure is a share (drawn as a meter)
    QString tone;     // "" | "good" | "warning" | "danger"
};

struct ReportTable {
    QString id;
    QString title;
    QStringList columns;
    QList<int> numericColumns; // right-aligned
    QList<QStringList> rows;
    QList<int> rowTaskUids;    // 0 when a row is not a task
    QList<QString> rowTones;   // per row: "" | "warning" | "danger" | "good"
    QString emptyText;
};

struct PlanReport {
    QString id;
    QString title;
    QString subtitle;
    QList<ReportKpi> kpis;
    QList<ReportTable> tables;
};

[[nodiscard]] QStringList reportIds(); // "overview", "critical", "resources", "late"
[[nodiscard]] QString reportTitle(const QString &id);
// An unknown id builds the overview.
[[nodiscard]] PlanReport buildReport(const Project &project, const QString &id, const QDate &today,
                                     const QLocale &locale = QLocale());

// The late/slipped test above, for one task (the Gantt and the sheet use it too).
[[nodiscard]] bool isLate(const Project &project, const Task &task, Minutes reference);
[[nodiscard]] Minutes reportReference(const Project &project, const QDate &today);

} // namespace QindaOffice::Plan
