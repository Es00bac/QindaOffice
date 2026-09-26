// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Builders for the plan_engine tests. Every fixture starts Monday
// 2 March 2026 at 08:00 on the standard calendar (Mon–Fri 08–12, 13–17),
// so a "day" is 480 working minutes and the expected dates in the tests can
// be checked against a paper calendar.

#include "plan_time.h"
#include "plan_types.h"
#include "scheduler.h"

#include <QtTest>

namespace PlanTest {

using namespace QindaOffice::Plan;

inline constexpr Minutes kDay = 480;
inline constexpr Minutes kHour = 60;

// An instant in March 2026 (or another month).
inline Minutes at(int day, int hour = 8, int minute = 0, int month = 3, int year = 2026)
{
    return toMinutes(QDate(year, month, day), hour * 60 + minute);
}

inline QString iso(Minutes m)
{
    return toIso(m);
}

struct Builder {
    Project p = newProject(QDate(2026, 3, 2), QStringLiteral("Fixture"));

    // Appends a task; returns its index.
    int task(const QString &name, Minutes duration, int level = 1)
    {
        Task t;
        t.uid = p.nextTaskUid();
        t.guid = QUuid::createUuid();
        t.name = name;
        t.duration = duration;
        t.outlineLevel = level;
        p.tasks.append(t);
        return int(p.tasks.size()) - 1;
    }
    void link(int successor, int predecessor, LinkType type = LinkType::FinishToStart, Minutes lag = 0)
    {
        Link l;
        l.predecessorUid = p.tasks.at(predecessor).uid;
        l.type = type;
        l.lag = lag;
        p.tasks[successor].predecessors.append(l);
    }
    void constrain(int index, ConstraintType type, Minutes date)
    {
        p.tasks[index].constraint = type;
        p.tasks[index].constraintDate = date;
    }
    int resource(const QString &name, qint64 rate = 0, int maxUnits = 100, ResourceType type = ResourceType::Work)
    {
        Resource r;
        r.uid = p.nextResourceUid();
        r.name = name;
        r.standardRate = rate;
        r.maxUnits = maxUnits;
        r.type = type;
        p.resources.append(r);
        return r.uid;
    }
    void assign(int taskIndex, int resourceUid, int units = 100)
    {
        Assignment a;
        a.uid = p.nextAssignmentUid();
        a.taskUid = p.tasks.at(taskIndex).uid;
        a.resourceUid = resourceUid;
        a.units = units;
        p.assignments.append(a);
    }
    Project &run()
    {
        schedule(p);
        return p;
    }
    const Task &t(int index) const { return p.tasks.at(index); }
};

} // namespace PlanTest

#define QCOMPARE_AT(actual, expected) QCOMPARE(PlanTest::iso(actual), PlanTest::iso(expected))
