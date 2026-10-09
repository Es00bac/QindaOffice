// SPDX-License-Identifier: GPL-3.0-or-later
// The scheduler after the passes: free slack and As Late As Possible,
// inactive tasks, summary roll-ups, work and cost; the critical-path option
// helpers of the backward pass (M79); and the link check the editing
// operations ask before creating a link (scheduler.h).
#include "outline.h"
#include "plan_time.h"
#include "scheduler.h"
#include "scheduler_p.h"

#include <algorithm>

namespace QindaOffice::Plan {

namespace Detail {

Minutes SchedulePass::criticalLimit() const
{
    // M79: Project's "tasks are critical if slack is less than or equal to
    // N days" (MSPDI CriticalSlackLimit), in this project's working days.
    return Minutes(std::max(0, m_project.info.criticalSlackDays)) * m_project.info.minutesPerDay;
}

bool SchedulePass::drivesSuccessor(int i) const
{
    // M79: multiple critical paths end at tasks that drive nothing — no
    // successor link of their own or of a summary above them.
    for (int k = i; k >= 0; k = m_project.tasks.at(k).parentIndex) {
        if (!m_outgoing.at(k).isEmpty()) {
            return true;
        }
    }
    return false;
}

void SchedulePass::freeSlackAndAlap()
{
    QList<Task> &tasks = m_project.tasks;
    for (int i = 0; i < m_n; ++i) {
        Task &t = tasks[i];
        if (!t.effectiveActive || t.summary) {
            continue;
        }
        const WorkCalendar &c = cal(i);
        Minutes room = -1;
        bool any = false;
        for (const LinkRef &ref : m_outgoing.at(i)) {
            const Task &s = tasks.at(ref.successor);
            const Minutes lag = lagOf(ref);
            const auto lagSub = [&](Minutes x) { return lag == 0 ? x : m_projectCal.add(x, -lag); };
            Minutes r = 0;
            switch (s.predecessors.at(ref.link).type) {
            case LinkType::FinishToStart:
                r = c.workBetween(t.earlyFinish, lagSub(s.earlyStart));
                break;
            case LinkType::StartToStart:
                r = c.workBetween(t.earlyStart, lagSub(s.earlyStart));
                break;
            case LinkType::FinishToFinish:
                r = c.workBetween(t.earlyFinish, lagSub(s.earlyFinish));
                break;
            case LinkType::StartToFinish:
                r = c.workBetween(t.earlyStart, lagSub(s.earlyFinish));
                break;
            }
            room = any ? std::min(room, r) : r;
            any = true;
        }
        Minutes free = any ? std::max<Minutes>(0, room) : std::max<Minutes>(0, t.totalSlack);
        free = t.totalSlack >= 0 ? std::min(free, t.totalSlack) : 0;
        t.freeSlack = free;
        // AGENT-NOTE: As Late As Possible = delayed by its free slack, i.e.
        // as late as it can go without moving any successor (Project's
        // meaning), so successors computed from early dates stay valid.
        const bool started = t.actualStart != kNoDate || t.percentComplete > 0;
        if (t.constraint == ConstraintType::AsLateAsPossible && !t.manual && !started && free > 0) {
            const Minutes d = t.duration;
            const Minutes ef = c.snapBackward(c.add(t.earlyFinish, free));
            t.finish = ef;
            t.start = d > 0 ? c.subtract(ef, d) : ef;
            t.freeSlack = 0;
        }
    }
}

void SchedulePass::inactiveDates()
{
    for (int i = 0; i < m_n; ++i) {
        Task &t = m_project.tasks[i];
        if (t.effectiveActive || t.summary) {
            continue;
        }
        // Inactive tasks keep dates of their own (constraints, actuals,
        // manual dates) but drive nothing and are never critical.
        forwardLeaf(i, m_projectStart);
        t.lateStart = t.earlyStart;
        t.lateFinish = t.earlyFinish;
        t.totalSlack = t.freeSlack = 0;
        t.critical = false;
    }
}

void SchedulePass::rollUp()
{
    QList<Task> &tasks = m_project.tasks;
    // Deeper tasks come later in the list, so a reverse walk finishes every
    // child before its summary.
    for (int i = m_n - 1; i >= 0; --i) {
        Task &s = tasks[i];
        if (!s.summary) {
            continue;
        }
        bool any = false;
        Minutes weighted = 0;
        Minutes durations = 0;
        int pctSum = 0;
        int count = 0;
        Minutes start = kNoDate;
        Minutes finish = kNoDate;
        Minutes es = kNoDate;
        Minutes ef = kNoDate;
        Minutes ls = kNoDate;
        Minutes lf = kNoDate;
        Minutes slack = 0;
        bool critical = false;
        for (int c = i + 1; c < m_n && tasks.at(c).outlineLevel > s.outlineLevel; ++c) {
            const Task &child = tasks.at(c);
            if (child.parentIndex != i || (s.effectiveActive && !child.effectiveActive)) {
                continue;
            }
            const auto lo = [&](Minutes a, Minutes b) { return a == kNoDate ? b : std::min(a, b); };
            const auto hi = [&](Minutes a, Minutes b) { return a == kNoDate ? b : std::max(a, b); };
            start = lo(start, child.start);
            finish = hi(finish, child.finish);
            es = lo(es, child.earlyStart);
            ef = hi(ef, child.earlyFinish);
            ls = lo(ls, child.lateStart);
            lf = hi(lf, child.lateFinish);
            slack = any ? std::min(slack, child.totalSlack) : child.totalSlack;
            critical = critical || child.critical;
            weighted += child.duration * child.percentComplete;
            durations += child.duration;
            pctSum += child.percentComplete;
            ++count;
            any = true;
        }
        if (!any) {
            const Minutes floor = m_entryFloor.at(i) != kNoDate ? m_entryFloor.at(i) : m_projectStart;
            s.start = s.finish = s.earlyStart = s.earlyFinish = s.lateStart = s.lateFinish = floor;
            s.duration = 0;
            s.totalSlack = 0;
            s.critical = false;
            continue;
        }
        s.start = start;
        s.finish = finish;
        s.earlyStart = es;
        s.earlyFinish = ef;
        s.lateStart = ls;
        s.lateFinish = lf;
        s.duration = m_projectCal.workBetween(start, finish);
        s.totalSlack = slack;
        s.freeSlack = 0;
        s.critical = s.effectiveActive && critical;
        s.percentComplete = durations > 0 ? int(divRound(weighted, durations)) : (count > 0 ? pctSum / count : 0);
    }
    Minutes finish = kNoDate;
    for (const Task &t : std::as_const(tasks)) {
        if (t.effectiveActive && !t.summary) {
            finish = std::max(finish, t.finish);
        }
    }
    m_project.finish = finish == kNoDate ? m_projectStart : finish;
}

void SchedulePass::costs()
{
    QList<Task> &tasks = m_project.tasks;
    QHash<int, int> resourceByUid;
    for (int r = 0; r < m_project.resources.size(); ++r) {
        Resource &res = m_project.resources[r];
        res.work = 0;
        res.cost = 0;
        resourceByUid.insert(res.uid, r);
    }
    for (Task &t : tasks) {
        t.work = 0;
        t.cost = t.fixedCost;
    }
    for (Assignment &a : m_project.assignments) {
        a.work = 0;
        a.cost = 0;
        const int ti = m_uidIndex.value(a.taskUid, -1);
        const int ri = resourceByUid.value(a.resourceUid, -1);
        if (ti < 0 || ri < 0) {
            continue;
        }
        Task &t = tasks[ti];
        Resource &r = m_project.resources[ri];
        if (!t.effectiveActive || t.summary) {
            continue;
        }
        if (r.type == ResourceType::Work) {
            a.work = t.isMilestone() ? 0 : divRound(t.duration * a.units, 100);
            a.cost = divRound(a.work * r.standardRate, 60) + r.costPerUse;
            t.work += a.work;
        } else if (r.type == ResourceType::Cost) {
            a.work = 0; // M79: a cost resource costs what its assignment says
            a.cost = a.costInput;
        } else {
            a.work = a.units;
            a.cost = divRound(qint64(a.units) * r.standardRate, 100) + r.costPerUse;
        }
        t.cost += a.cost;
        r.work += a.work;
        r.cost += a.cost;
    }
    for (int i = m_n - 1; i >= 0; --i) {
        const Task &t = tasks.at(i);
        if (t.parentIndex >= 0 && (t.effectiveActive || !tasks.at(t.parentIndex).effectiveActive)) {
            tasks[t.parentIndex].work += t.work;
            tasks[t.parentIndex].cost += t.cost;
        }
    }
}

bool SchedulePass::hasCycle()
{
    prepare();
    collectLinks();
    QList<int> order;
    QList<int> stuck;
    return !topologicalOrder(&order, &stuck);
}

} // namespace Detail

QString linkProblem(const Project &project, int predecessorIndex, int successorIndex, LinkType type)
{
    const int n = int(project.tasks.size());
    if (predecessorIndex < 0 || successorIndex < 0 || predecessorIndex >= n || successorIndex >= n) {
        return QStringLiteral("There is no such task.");
    }
    if (predecessorIndex == successorIndex) {
        return QStringLiteral("A task cannot be linked to itself.");
    }
    Project copy = project;
    computeStructure(copy);
    if (isAncestor(copy, predecessorIndex, successorIndex) || isAncestor(copy, successorIndex, predecessorIndex)) {
        return QStringLiteral("A summary task cannot be linked to its own subtasks.");
    }
    const Task &succ = copy.tasks.at(successorIndex);
    if (succ.summary && (type == LinkType::FinishToFinish || type == LinkType::StartToFinish)) {
        return QStringLiteral("A summary task cannot have a finish-to-finish or start-to-finish predecessor.");
    }
    const int predUid = copy.tasks.at(predecessorIndex).uid;
    for (const Link &l : succ.predecessors) {
        if (l.predecessorUid == predUid) {
            return QStringLiteral("These tasks are already linked.");
        }
    }
    Link link;
    link.predecessorUid = predUid;
    link.type = type;
    copy.tasks[successorIndex].predecessors.append(link);
    if (Detail::SchedulePass(copy).hasCycle()) {
        return QStringLiteral("This link would make a circular chain of links.");
    }
    return {};
}

} // namespace QindaOffice::Plan
