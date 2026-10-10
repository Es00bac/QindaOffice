// SPDX-License-Identifier: GPL-3.0-or-later
// The scheduler's forward and backward passes (scheduler.h has the method;
// scheduler_graph.cpp the node graph and cycle handling; scheduler_rollup.cpp
// the slack, ALAP, roll-ups and costs).
#include "scheduler.h"

#include "outline.h"
#include "plan_time.h"
#include "resources.h"
#include "scheduler_p.h"

#include <QSet>

#include <algorithm>

namespace QindaOffice::Plan {

namespace Detail {

namespace {
quint64 linkKey(int successor, int link)
{
    return (quint64(quint32(successor)) << 32) | quint32(link);
}
} // namespace

SchedulePass::SchedulePass(Project &project)
    : m_project(project)
{
}

void SchedulePass::issue(int taskIndex, const QString &message)
{
    if (taskIndex >= 0) {
        Task &t = m_project.tasks[taskIndex];
        m_project.issues.append({t.uid, message});
        t.warning = t.warning.isEmpty() ? message : t.warning + QLatin1Char('\n') + message;
    } else {
        m_project.issues.append({0, message});
    }
}

const WorkCalendar &SchedulePass::cal(int taskIndex) const
{
    return *m_calendars.constFind(m_taskCal.at(taskIndex));
}

void SchedulePass::run()
{
    prepare();
    QList<int> order;
    QList<int> stuck;
    for (int attempt = 0; attempt < 2; ++attempt) {
        collectLinks();
        stuck.clear();
        if (topologicalOrder(&order, &stuck)) {
            break;
        }
        // AGENT-NOTE: a cycle can only come from a file (plan_edit refuses
        // to create one). Every cycle lies inside one strongly connected
        // component of the node graph; ignoring the links inside each
        // component leaves only parent/child edges there, which are acyclic,
        // so the second attempt always succeeds — and links that merely
        // hang off a cycle (B → C after A ⇄ B) still hold.
        const QList<int> component = components(stuck);
        for (int i = 0; i < m_n; ++i) {
            const QList<Link> &links = m_project.tasks.at(i).predecessors;
            for (int l = 0; l < links.size(); ++l) {
                const int j = m_uidIndex.value(links.at(l).predecessorUid, -1);
                if (j < 0 || component.at(entry(i)) < 0 || component.at(entry(i)) != component.at(exit(j))) {
                    continue;
                }
                m_cyclic.insert(linkKey(i, l), true);
                issue(i, QStringLiteral("The link from task %1 is part of a circular chain of links and is ignored.")
                             .arg(m_project.tasks.at(j).id));
            }
        }
    }
    forward(order);
    backward(order);
    freeSlackAndAlap();
    inactiveDates();
    rollUp();
    costs();
    computeOverallocation(m_project);
}

void SchedulePass::prepare()
{
    m_project.issues.clear();
    computeStructure(m_project);
    QList<Task> &tasks = m_project.tasks;
    m_n = int(tasks.size());
    m_uidIndex.clear();
    for (int i = 0; i < m_n; ++i) {
        m_uidIndex.insert(tasks.at(i).uid, i);
    }
    m_projectCal = WorkCalendar(m_project, m_project.info.calendarUid);
    m_calendars.clear();
    m_calendars.insert(m_project.info.calendarUid, m_projectCal);
    // The first assigned work resource per task (for its calendar).
    QHash<int, int> resourceCalendar;
    for (const Resource &r : std::as_const(m_project.resources)) {
        if (r.type == ResourceType::Work) {
            resourceCalendar.insert(r.uid, r.calendarUid >= 0 && m_project.calendarIndex(r.calendarUid) >= 0
                                               ? r.calendarUid
                                               : m_project.info.calendarUid);
        }
    }
    QHash<int, int> taskResourceCal;
    for (const Assignment &a : std::as_const(m_project.assignments)) {
        if (!taskResourceCal.contains(a.taskUid) && resourceCalendar.contains(a.resourceUid)) {
            taskResourceCal.insert(a.taskUid, resourceCalendar.value(a.resourceUid));
        }
    }
    m_taskCal.resize(m_n);
    for (int i = 0; i < m_n; ++i) {
        Task &t = tasks[i];
        int uid = m_project.info.calendarUid;
        if (t.calendarUid >= 0 && m_project.calendarIndex(t.calendarUid) >= 0) {
            uid = t.calendarUid;
        } else if (taskResourceCal.contains(t.uid)) {
            uid = taskResourceCal.value(t.uid);
        }
        m_taskCal[i] = uid;
        if (!m_calendars.contains(uid)) {
            m_calendars.insert(uid, WorkCalendar(m_project, uid));
        }
        t.warning.clear();
        t.constraintConflict = false;
        t.critical = false;
        t.overallocated = false;
        t.start = t.finish = t.earlyStart = t.earlyFinish = t.lateStart = t.lateFinish = kNoDate;
        t.totalSlack = t.freeSlack = 0;
        t.duration = std::max<Minutes>(0, t.duration);
        t.percentComplete = std::clamp(t.percentComplete, 0, 100);
    }
    m_projectStart = m_project.info.start;
    if (m_projectStart == kNoDate) {
        // AGENT-NOTE: files always carry a start (plan_file/mspdi default
        // it); this only guards a hand-built Project.
        m_projectStart = toMinutes(QDate(2026, 1, 5), m_project.info.defaultStartMinute);
        issue(-1, QStringLiteral("The project has no start date; 5 January 2026 is used."));
    }
    m_entryFloor.fill(kNoDate, m_n);
    m_exitCeiling.fill(kNoDate, m_n);
    m_entryLateStart.fill(kNoDate, m_n);
    m_cyclic.clear();
}

void SchedulePass::collectLinks()
{
    QList<Task> &tasks = m_project.tasks;
    m_incoming = QList<QList<LinkRef>>(m_n);
    m_outgoing = QList<QList<LinkRef>>(m_n);
    // Subtree ends, O(n), for the ancestor test.
    QList<int> end(m_n);
    for (int i = m_n - 1; i >= 0; --i) {
        int k = i + 1;
        while (k < m_n && tasks.at(k).outlineLevel > tasks.at(i).outlineLevel) {
            k = end.at(k);
        }
        end[i] = k;
    }
    const bool firstCollect = m_cyclic.isEmpty(); // report once, not again on the cycle retry
    for (int i = 0; i < m_n; ++i) {
        tasks[i].ignoredLinks.clear();
        const Task &t = tasks.at(i);
        if (!t.effectiveActive) {
            continue;
        }
        for (int l = 0; l < t.predecessors.size(); ++l) {
            const Link &link = t.predecessors.at(l);
            const int j = m_uidIndex.value(link.predecessorUid, -1);
            if (m_cyclic.contains(linkKey(i, l))) {
                tasks[i].ignoredLinks.append(l);
                continue;
            }
            QString problem;
            if (j < 0) {
                problem = QStringLiteral("A predecessor (UID %1) no longer exists; the link is ignored.").arg(link.predecessorUid);
            } else if (j == i) {
                problem = QStringLiteral("A task cannot be its own predecessor; the link is ignored.");
            } else if ((j < i && i < end.at(j)) || (i < j && j < end.at(i))) {
                problem = QStringLiteral("A task cannot be linked to its own summary task or subtask (task %1); the link is ignored.")
                              .arg(tasks.at(j).id);
            } else if (t.summary
                       && (link.type == LinkType::FinishToFinish || link.type == LinkType::StartToFinish)) {
                problem = QStringLiteral("A summary task cannot have a finish-to-finish or start-to-finish predecessor (task %1); "
                                         "the link is ignored.")
                              .arg(tasks.at(j).id);
            }
            if (!problem.isEmpty()) {
                if (firstCollect) {
                    issue(i, problem);
                }
                tasks[i].ignoredLinks.append(l);
                continue;
            }
            if (!tasks.at(j).effectiveActive) {
                continue; // an inactive predecessor drives nothing
            }
            const LinkRef ref{j, i, l};
            m_incoming[i].append(ref);
            m_outgoing[j].append(ref);
        }
    }
}

Minutes SchedulePass::lagOf(const LinkRef &ref) const
{
    const Link &link = m_project.tasks.at(ref.successor).predecessors.at(ref.link);
    if (link.lagPercent) {
        return divRound(m_project.tasks.at(ref.predecessor).duration * *link.lagPercent, 100);
    }
    return link.lag;
}

void SchedulePass::forward(const QList<int> &order)
{
    QList<Task> &tasks = m_project.tasks;
    for (int u : order) {
        if (u >= m_n) {
            // A summary's exit: the roll-up of its active children.
            const int i = u - m_n;
            Minutes es = kNoDate;
            Minutes ef = kNoDate;
            for (int c = i + 1; c < m_n && tasks.at(c).outlineLevel > tasks.at(i).outlineLevel; ++c) {
                const Task &child = tasks.at(c);
                if (child.parentIndex != i || !child.effectiveActive) {
                    continue;
                }
                es = es == kNoDate ? child.earlyStart : std::min(es, child.earlyStart);
                ef = ef == kNoDate ? child.earlyFinish : std::max(ef, child.earlyFinish);
            }
            Task &s = tasks[i];
            s.earlyStart = es == kNoDate ? m_entryFloor.at(i) : es;
            s.earlyFinish = ef == kNoDate ? m_entryFloor.at(i) : ef;
            s.start = s.earlyStart;
            s.finish = s.earlyFinish;
            s.duration = m_projectCal.workBetween(s.earlyStart, s.earlyFinish);
            continue;
        }
        const int i = u;
        const Task &t = tasks.at(i);
        Minutes floor = m_projectStart;
        if (t.parentIndex >= 0) {
            floor = std::max(floor, m_entryFloor.at(t.parentIndex));
        }
        const Minutes d = t.summary ? 0 : t.duration;
        for (const LinkRef &ref : m_incoming.at(i)) {
            const Task &p = tasks.at(ref.predecessor);
            Minutes req = 0;
            switch (t.predecessors.at(ref.link).type) {
            case LinkType::FinishToStart:
                req = lagged(ref, p.earlyFinish);
                break;
            case LinkType::StartToStart:
                req = lagged(ref, p.earlyStart);
                break;
            case LinkType::FinishToFinish:
                req = lagged(ref, p.earlyFinish);
                req = d > 0 ? before(i, req, length(i)) : req;
                break;
            case LinkType::StartToFinish:
                req = lagged(ref, p.earlyStart);
                req = d > 0 ? before(i, req, length(i)) : req;
                break;
            }
            floor = std::max(floor, req);
        }
        if (t.summary) {
            if (t.constraint == ConstraintType::StartNoEarlierThan && t.constraintDate != kNoDate) {
                floor = std::max(floor, t.constraintDate);
            }
            m_entryFloor[i] = floor;
            continue;
        }
        forwardLeaf(i, floor);
    }
}

void SchedulePass::forwardLeaf(int i, Minutes floor)
{
    Task &t = m_project.tasks[i];
    const WorkCalendar &c = cal(i);
    const Minutes d = t.duration;
    const Minutes len = length(i); // M83: with its split gaps, elapsed or working
    Minutes es = floor;
    bool conflict = false;
    if (t.manual) {
        if (t.manualStart != kNoDate) {
            es = t.manualStart;
        }
    } else if (t.constraintDate != kNoDate) {
        const Minutes date = t.constraintDate;
        const Minutes startForFinish = d > 0 ? before(i, date, len) : date;
        switch (t.constraint) {
        case ConstraintType::AsSoonAsPossible:
        case ConstraintType::AsLateAsPossible:
            break;
        case ConstraintType::StartNoEarlierThan:
            es = std::max(es, date);
            break;
        case ConstraintType::FinishNoEarlierThan:
            es = std::max(es, startForFinish);
            break;
        case ConstraintType::MustStartOn:
            conflict = es > date;
            es = date;
            break;
        case ConstraintType::MustFinishOn:
            conflict = es > startForFinish;
            es = startForFinish;
            break;
        case ConstraintType::StartNoLaterThan:
            if (es > date) {
                conflict = true;
                es = date;
            }
            break;
        case ConstraintType::FinishNoLaterThan:
            if (es > startForFinish) {
                conflict = true;
                es = startForFinish;
            }
            break;
        }
    }
    t.unleveledStart = d > 0 ? snap(i, es) : es;
    // M79: Project's Leveling Delay holds the task back from this early
    // start (elapsed or working time, leveling.h). A hard constraint pins the
    // task, so it is never delayed (leveling refuses such tasks too).
    const bool pinned = t.constraint == ConstraintType::MustStartOn || t.constraint == ConstraintType::MustFinishOn;
    if (!t.manual && !pinned && t.levelingDelay > 0) {
        es = t.levelingDelayElapsed ? t.unleveledStart + t.levelingDelay
                                    : c.add(t.unleveledStart, t.levelingDelay);
    }
    if (!t.manual && t.levelingFloor != kNoDate) {
        es = std::max(es, t.levelingFloor); // legacy floor (plan_types.h)
    }
    if (d > 0) {
        es = snap(i, es);
    }
    Minutes ef = d > 0 ? after(i, es, len) : es;
    if (t.manual && t.manualFinish != kNoDate) {
        ef = std::max(es, t.manualFinish);
    }
    if (t.actualStart != kNoDate) {
        es = t.actualStart;
        ef = d > 0 ? after(i, es, len) : es;
    }
    if (t.actualFinish != kNoDate) {
        ef = std::max(t.actualFinish, t.actualStart != kNoDate ? t.actualStart : kNoDate);
        if (t.actualStart == kNoDate) {
            es = d > 0 ? before(i, ef, len) : ef;
        }
    }
    t.earlyStart = es;
    t.earlyFinish = std::max(es, ef);
    t.start = t.earlyStart;
    t.finish = t.earlyFinish;
    if (conflict) {
        t.constraintConflict = true;
        issue(i, QStringLiteral("Its %1 constraint on %2 conflicts with its links; the constraint is honoured.")
                     .arg(constraintCode(t.constraint), formatDate(t.constraintDate, QLocale::c())));
    }
}

void SchedulePass::backward(const QList<int> &order)
{
    QList<Task> &tasks = m_project.tasks;
    m_projectFinish = kNoDate;
    for (int i = 0; i < m_n; ++i) {
        if (tasks.at(i).effectiveActive && !tasks.at(i).summary) {
            m_projectFinish = std::max(m_projectFinish, tasks.at(i).earlyFinish);
        }
    }
    if (m_projectFinish == kNoDate) {
        m_projectFinish = m_projectStart;
    }
    const auto backRequirement = [&](const LinkRef &ref) {
        const Task &p = tasks.at(ref.predecessor);
        const Task &s = tasks.at(ref.successor);
        const auto lagSub = [&](Minutes x) { return lagged(ref, x, true); };
        const Minutes sLS = s.summary ? m_entryLateStart.at(ref.successor) : s.lateStart;
        const Minutes sLF = s.summary ? m_exitCeiling.at(ref.successor) : s.lateFinish;
        const Minutes d = p.duration;
        // A summary's span is on the project calendar; a leaf's on its own (M83).
        const auto plus = [&](Minutes x) {
            return d <= 0 ? x : p.summary ? m_projectCal.add(x, d) : after(ref.predecessor, x, length(ref.predecessor));
        };
        switch (s.predecessors.at(ref.link).type) {
        case LinkType::FinishToStart:
            return lagSub(sLS);
        case LinkType::StartToStart:
            return plus(lagSub(sLS));
        case LinkType::FinishToFinish:
            return lagSub(sLF);
        case LinkType::StartToFinish:
            return plus(lagSub(sLF));
        }
        return lagSub(sLS);
    };
    for (auto it = order.crbegin(); it != order.crend(); ++it) {
        const int u = *it;
        const int i = u >= m_n ? u - m_n : u;
        const Task &t = tasks.at(i);
        if (u >= m_n || !t.summary) {
            Minutes ceiling = m_projectFinish;
            if (t.parentIndex >= 0) {
                ceiling = std::min(ceiling, m_exitCeiling.at(t.parentIndex));
            }
            for (const LinkRef &ref : m_outgoing.at(i)) {
                ceiling = std::min(ceiling, backRequirement(ref));
            }
            // M79: "Calculate multiple critical paths" — a task that drives
            // nothing (no successor of its own or above it) ends a path of
            // its own: its late finish is its early finish, as in Project.
            if (m_project.info.multipleCriticalPaths && t.effectiveActive && !drivesSuccessor(i)) {
                ceiling = std::min(ceiling, t.earlyFinish);
            }
            // M79: a deadline is a late finish the task must keep (Project:
            // it moves no date, but slack is measured to it, so a task that
            // finishes after its deadline has negative slack and is critical).
            if (t.deadline != kNoDate && t.effectiveActive) {
                ceiling = std::min(ceiling, t.deadline);
            }
            if (u >= m_n) {
                m_exitCeiling[i] = ceiling;
            } else {
                backwardLeaf(i, ceiling);
            }
            continue;
        }
        // A summary's entry: the earliest late start of its children.
        Minutes ls = kNoDate;
        for (int c = i + 1; c < m_n && tasks.at(c).outlineLevel > t.outlineLevel; ++c) {
            const Task &child = tasks.at(c);
            if (child.parentIndex != i || !child.effectiveActive) {
                continue;
            }
            const Minutes childLs = child.summary ? m_entryLateStart.at(c) : child.lateStart;
            ls = ls == kNoDate ? childLs : std::min(ls, childLs);
        }
        m_entryLateStart[i] = ls == kNoDate ? m_exitCeiling.at(i) : ls;
    }
}

void SchedulePass::backwardLeaf(int i, Minutes ceiling)
{
    Task &t = m_project.tasks[i];
    const WorkCalendar &c = cal(i);
    const Minutes d = t.duration;
    const bool complete = t.percentComplete >= 100 || t.actualFinish != kNoDate;
    Minutes lf = ceiling;
    if (complete) {
        lf = t.earlyFinish;
    } else if (!t.manual && t.constraintDate != kNoDate) {
        switch (t.constraint) {
        case ConstraintType::FinishNoLaterThan:
            lf = std::min(lf, t.constraintDate);
            break;
        case ConstraintType::StartNoLaterThan:
            lf = std::min(lf, d > 0 ? after(i, t.constraintDate, length(i)) : t.constraintDate);
            break;
        case ConstraintType::MustStartOn:
        case ConstraintType::MustFinishOn:
            lf = std::min(lf, t.earlyFinish);
            break;
        default:
            break;
        }
    }
    if (d > 0 && !complete && !t.durationElapsed) {
        lf = c.snapBackward(lf);
    }
    t.lateFinish = lf;
    t.lateStart = d > 0 ? before(i, lf, length(i)) : lf;
    if (complete) {
        t.lateStart = t.earlyStart;
    }
    t.totalSlack = c.workBetween(t.earlyFinish, lf);
    // M79: Project's "tasks are critical if slack is less than or equal to
    // N days" (MSPDI CriticalSlackLimit); 0 by default.
    t.critical = !complete && t.totalSlack <= criticalLimit();
}


} // namespace Detail

void schedule(Project &project)
{
    Detail::SchedulePass(project).run();
}

int effectiveCalendarUid(const Project &project, int taskIndex)
{
    const Task &t = project.tasks.at(taskIndex);
    if (t.calendarUid >= 0 && project.calendarIndex(t.calendarUid) >= 0) {
        return t.calendarUid;
    }
    for (int a : project.assignmentsOfTask(t.uid)) {
        const int r = project.resourceIndex(project.assignments.at(a).resourceUid);
        if (r >= 0 && project.resources.at(r).type == ResourceType::Work) {
            const int uid = project.resources.at(r).calendarUid;
            return uid >= 0 && project.calendarIndex(uid) >= 0 ? uid : project.info.calendarUid;
        }
    }
    return project.info.calendarUid;
}

} // namespace QindaOffice::Plan
