// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Private to scheduler*.cpp: the state of one schedule() run.

#include "plan_types.h"
#include "work_calendar.h"

#include <QHash>

namespace QindaOffice::Plan::Detail {

struct LinkRef {
    int predecessor = -1; // task index
    int successor = -1;   // task index
    int link = -1;        // index into successor.predecessors
};

class SchedulePass {
public:
    explicit SchedulePass(Project &project);
    void run();
    // True when the links (plus the outline) contain a cycle.
    [[nodiscard]] bool hasCycle();

private:
    // scheduler.cpp
    void prepare();
    void collectLinks();
    void buildGraph(QList<QList<int>> *adjacency, QList<bool> *present) const;
    // stuck: the graph nodes the sort could not place (a cycle runs through them).
    bool topologicalOrder(QList<int> *order, QList<int> *stuck) const;
    // Strongly connected component id per graph node (-1 outside `nodes`).
    [[nodiscard]] QList<int> components(const QList<int> &nodes) const;
    void forward(const QList<int> &order);
    void forwardLeaf(int i, Minutes floor);
    void backward(const QList<int> &order);
    void backwardLeaf(int i, Minutes ceiling);
    [[nodiscard]] Minutes lagOf(const LinkRef &ref) const;
    [[nodiscard]] Minutes lagAdd(Minutes at, Minutes lag) const;
    // scheduler_rollup.cpp
    void freeSlackAndAlap();
    void inactiveDates();
    void rollUp();
    void costs();

    [[nodiscard]] const WorkCalendar &cal(int taskIndex) const;
    [[nodiscard]] int entry(int i) const { return i; }
    [[nodiscard]] int exit(int i) const { return m_project.tasks.at(i).summary ? m_n + i : i; }
    void issue(int taskIndex, const QString &message);

    Project &m_project;
    int m_n = 0;
    QHash<int, int> m_uidIndex;
    QHash<int, WorkCalendar> m_calendars; // by calendar uid
    WorkCalendar m_projectCal;
    QList<int> m_taskCal;                  // calendar uid per task
    QList<QList<LinkRef>> m_incoming;      // valid links per successor
    QList<QList<LinkRef>> m_outgoing;      // valid links per predecessor
    QList<Minutes> m_entryFloor;           // summary entry floor (forward)
    QList<Minutes> m_exitCeiling;          // summary exit ceiling (backward)
    QList<Minutes> m_entryLateStart;       // summary entry late start (backward)
    QHash<quint64, bool> m_cyclic;         // (successor << 32 | link) → ignored
    Minutes m_projectStart = 0;
    Minutes m_projectFinish = 0;
};

} // namespace QindaOffice::Plan::Detail
