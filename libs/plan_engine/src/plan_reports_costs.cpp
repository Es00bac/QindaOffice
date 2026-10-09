// SPDX-License-Identifier: GPL-3.0-or-later
// Project's Cost Overview and Work Overview dashboards (M79, D-088): the
// headline figures and, per top-level task and per resource, what was
// planned, done and left — with the baseline and its variance once one is
// set. Actuals follow each task's % complete (QindaPlan spreads work and
// cost evenly over a task's duration); a summary adds up its subtasks'.
#include "plan_reports_p.h"

#include "baselines.h"
#include "plan_edit.h"
#include "plan_time.h"

namespace QindaOffice::Plan::Reports {

namespace {

qint64 share(qint64 total, int percent)
{
    return divRound(total * percent, 100);
}

struct Sums {
    qint64 cost = 0;
    qint64 actualCost = 0;
    qint64 baselineCost = 0;
    Minutes work = 0;
    Minutes actualWork = 0;
    Minutes baselineWork = 0;
    bool baseline = false;
};

// What a task's subtree has spent and done: each task's own cost (a
// summary's is its fixed cost) and work times its % complete.
void actualsOf(const Project &project, int index, qint64 *cost, Minutes *work)
{
    *cost = 0;
    *work = 0;
    const int level = project.tasks.at(index).outlineLevel;
    for (int k = index; k < project.tasks.size() && (k == index || project.tasks.at(k).outlineLevel > level); ++k) {
        const Task &t = project.tasks.at(k);
        if (!t.effectiveActive) {
            continue;
        }
        *cost += share(t.summary ? t.fixedCost : t.cost, t.percentComplete);
        *work += t.summary ? 0 : share(t.work, t.percentComplete);
    }
}

Sums sumsOf(const Project &project)
{
    Sums s;
    for (int i = 0; i < project.tasks.size(); ++i) {
        const Task &t = project.tasks.at(i);
        if (t.parentIndex >= 0 || !t.effectiveActive) {
            continue;
        }
        s.cost += t.cost;
        s.work += t.work;
        qint64 cost = 0;
        Minutes work = 0;
        actualsOf(project, i, &cost, &work);
        s.actualCost += cost;
        s.actualWork += work;
        if (t.baselines[0].set) {
            s.baseline = true;
            s.baselineCost += t.baselines[0].cost;
            s.baselineWork += t.baselines[0].work;
        }
    }
    return s;
}

// Actual work and cost of each resource (its assignments × % complete).
void resourceActuals(const Project &project, int resourceUid, Minutes *work, qint64 *cost)
{
    *work = 0;
    *cost = 0;
    for (const Assignment &a : project.assignments) {
        const int ti = project.taskIndex(a.taskUid);
        if (a.resourceUid != resourceUid || ti < 0) {
            continue;
        }
        const int pct = project.tasks.at(ti).percentComplete;
        const int ri = project.resourceIndex(resourceUid);
        if (ri >= 0 && project.resources.at(ri).type == ResourceType::Work) {
            *work += share(a.work, pct);
        }
        *cost += share(a.cost, pct);
    }
}

QString tone(qint64 variance)
{
    return variance > 0 ? QStringLiteral("warning") : variance < 0 ? QStringLiteral("good") : QString();
}

} // namespace

PlanReport costOverview(const Project &project, const QLocale &locale)
{
    PlanReport r;
    const QString &cur = project.info.currency;
    const Sums s = sumsOf(project);
    const int pct = s.cost > 0 ? int(divRound(s.actualCost * 100, s.cost)) : 0;
    r.kpis.append({QStringLiteral("cost"), QStringLiteral("Cost"), formatMoney(s.cost, cur, locale),
                   s.baseline ? QStringLiteral("Baseline %1").arg(formatMoney(s.baselineCost, cur, locale)) : QString(), -1, {}});
    r.kpis.append({QStringLiteral("actualCost"), QStringLiteral("Actual cost"), formatMoney(s.actualCost, cur, locale),
                   QStringLiteral("%1% of the cost spent").arg(pct), pct, {}});
    r.kpis.append({QStringLiteral("remainingCost"), QStringLiteral("Remaining cost"),
                   formatMoney(s.cost - s.actualCost, cur, locale), QString(), -1, {}});
    if (s.baseline) {
        const qint64 v = s.cost - s.baselineCost;
        r.kpis.append({QStringLiteral("costVariance"), QStringLiteral("Cost variance"), formatMoney(v, cur, locale),
                       v > 0 ? QStringLiteral("over the baseline") : v < 0 ? QStringLiteral("under the baseline")
                                                                        : QStringLiteral("on the baseline"),
                       -1, tone(v)});
    }
    ReportTable tasks;
    tasks.id = QStringLiteral("costStatus");
    tasks.title = QStringLiteral("Cost status");
    tasks.columns = {QStringLiteral("Name"), QStringLiteral("Actual Cost"), QStringLiteral("Remaining Cost"),
                     QStringLiteral("Baseline Cost"), QStringLiteral("Cost"), QStringLiteral("Cost Variance")};
    tasks.numericColumns = {1, 2, 3, 4, 5};
    tasks.emptyText = QStringLiteral("The plan has no tasks.");
    for (int i = 0; i < project.tasks.size(); ++i) {
        const Task &t = project.tasks.at(i);
        if (t.parentIndex >= 0 || !t.effectiveActive) {
            continue;
        }
        const Variance v = variance(project, t, 0);
        qint64 actual = 0;
        Minutes done = 0;
        actualsOf(project, i, &actual, &done);
        tasks.rows.append({t.name, formatMoney(actual, cur, locale), formatMoney(t.cost - actual, cur, locale),
                           t.baselines[0].set ? formatMoney(t.baselines[0].cost, cur, locale) : QStringLiteral("NA"),
                           formatMoney(t.cost, cur, locale), v.valid ? formatMoney(v.cost, cur, locale) : QString()});
        tasks.rowTaskUids.append(t.uid);
        tasks.rowTones.append(v.valid ? tone(v.cost) : QString());
    }
    r.tables.append(tasks);
    ReportTable resources;
    resources.id = QStringLiteral("resourceCost");
    resources.title = QStringLiteral("Cost by resource");
    resources.columns = {QStringLiteral("Resource"), QStringLiteral("Actual Cost"), QStringLiteral("Remaining Cost"),
                         QStringLiteral("Cost")};
    resources.numericColumns = {1, 2, 3};
    resources.emptyText = QStringLiteral("The plan has no resources.");
    for (const Resource &res : project.resources) {
        Minutes work = 0;
        qint64 actual = 0;
        resourceActuals(project, res.uid, &work, &actual);
        resources.rows.append({res.name, formatMoney(actual, cur, locale), formatMoney(res.cost - actual, cur, locale),
                               formatMoney(res.cost, cur, locale)});
        resources.rowTaskUids.append(0);
        resources.rowTones.append(QString());
    }
    r.tables.append(resources);
    return r;
}

PlanReport workOverview(const Project &project, const QLocale &locale)
{
    PlanReport r;
    const Sums s = sumsOf(project);
    const int pct = s.work > 0 ? int(divRound(s.actualWork * 100, s.work)) : 0;
    r.kpis.append({QStringLiteral("work"), QStringLiteral("Work"), formatWork(s.work, locale),
                   s.baseline ? QStringLiteral("Baseline %1").arg(formatWork(s.baselineWork, locale)) : QString(), -1, {}});
    r.kpis.append({QStringLiteral("actualWork"), QStringLiteral("Actual work"), formatWork(s.actualWork, locale),
                   QStringLiteral("%1% of the work done").arg(pct), pct, {}});
    r.kpis.append({QStringLiteral("remainingWork"), QStringLiteral("Remaining work"),
                   formatWork(s.work - s.actualWork, locale), QString(), -1, {}});
    if (s.baseline) {
        const Minutes v = s.work - s.baselineWork;
        r.kpis.append({QStringLiteral("workVariance"), QStringLiteral("Work variance"), formatWork(v, locale),
                       v > 0 ? QStringLiteral("more than the baseline") : v < 0 ? QStringLiteral("less than the baseline")
                                                                          : QStringLiteral("as the baseline"),
                       -1, tone(v)});
    }
    ReportTable tasks;
    tasks.id = QStringLiteral("workStats");
    tasks.title = QStringLiteral("Work stats");
    tasks.columns = {QStringLiteral("Name"), QStringLiteral("Work"), QStringLiteral("Baseline Work"),
                     QStringLiteral("Actual Work"), QStringLiteral("Remaining Work"), QStringLiteral("Work Variance")};
    tasks.numericColumns = {1, 2, 3, 4, 5};
    tasks.emptyText = QStringLiteral("The plan has no tasks.");
    for (int i = 0; i < project.tasks.size(); ++i) {
        const Task &t = project.tasks.at(i);
        if (t.parentIndex >= 0 || !t.effectiveActive) {
            continue;
        }
        const Variance v = variance(project, t, 0);
        qint64 spent = 0;
        Minutes actual = 0;
        actualsOf(project, i, &spent, &actual);
        tasks.rows.append({t.name, formatWork(t.work, locale),
                           t.baselines[0].set ? formatWork(t.baselines[0].work, locale) : QStringLiteral("NA"),
                           formatWork(actual, locale), formatWork(t.work - actual, locale),
                           v.valid ? formatWork(v.work, locale) : QString()});
        tasks.rowTaskUids.append(t.uid);
        tasks.rowTones.append(v.valid ? tone(v.work) : QString());
    }
    r.tables.append(tasks);
    ReportTable resources;
    resources.id = QStringLiteral("resourceWork");
    resources.title = QStringLiteral("Resource stats");
    resources.columns = {QStringLiteral("Resource"), QStringLiteral("Work"), QStringLiteral("Actual Work"),
                         QStringLiteral("Remaining Work"), QStringLiteral("Status")};
    resources.numericColumns = {1, 2, 3};
    resources.emptyText = QStringLiteral("The plan has no work resources.");
    for (const Resource &res : project.resources) {
        if (res.type != ResourceType::Work) {
            continue;
        }
        Minutes actual = 0;
        qint64 cost = 0;
        resourceActuals(project, res.uid, &actual, &cost);
        resources.rows.append({res.name, formatWork(res.work, locale), formatWork(actual, locale),
                               formatWork(res.work - actual, locale),
                               res.overallocated ? QStringLiteral("Overallocated") : QStringLiteral("OK")});
        resources.rowTaskUids.append(0);
        resources.rowTones.append(res.overallocated ? QStringLiteral("danger") : QString());
    }
    r.tables.append(resources);
    return r;
}

} // namespace QindaOffice::Plan::Reports
