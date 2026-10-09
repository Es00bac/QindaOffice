// SPDX-License-Identifier: GPL-3.0-or-later
// M79 (D-088): the critical path as Project computes it, by hand on the
// standard calendar (Mon–Fri 08–12, 13–17) from Monday 2 March 2026:
//  * the four link types with lag, and the total and free slack they leave;
//  * a deadline is a late finish — negative slack, critical, dates unmoved;
//  * "tasks are critical if slack is less than or equal to N days";
//  * "calculate multiple critical paths";
//  * Project's Leveling Delay: elapsed or working time from the unleveled
//    early start, relative (it follows its predecessors), ignored by a
//    Must Start On task.

#include "plan_fixture.h"

using namespace PlanTest;

class PlanCriticalTest final : public QObject {
    Q_OBJECT
private slots:
    void linkTypesWithLagAndSlack()
    {
        // A 4d Mon–Thu. B 1d SS A+1d → Tue. C 1d FF A+1d → finishes Fri
        // 17:00 (the project finish). D 2d SF A+3d → finishes when A's
        // start + 3 days (Wed 17:00) is reached → Tue–Wed.
        Builder b;
        const int a = b.task(QStringLiteral("A"), 4 * kDay);
        const int bb = b.task(QStringLiteral("B"), kDay);
        const int c = b.task(QStringLiteral("C"), kDay);
        const int d = b.task(QStringLiteral("D"), 2 * kDay);
        b.link(bb, a, LinkType::StartToStart, kDay);
        b.link(c, a, LinkType::FinishToFinish, kDay);
        b.link(d, a, LinkType::StartToFinish, 3 * kDay);
        b.run();
        QCOMPARE_AT(b.t(bb).start, at(3));
        QCOMPARE_AT(b.t(bb).finish, at(3, 17));
        QCOMPARE_AT(b.t(c).start, at(6));
        QCOMPARE_AT(b.t(c).finish, at(6, 17));
        QCOMPARE_AT(b.t(d).start, at(3));
        QCOMPARE_AT(b.t(d).finish, at(4, 17));
        QCOMPARE_AT(b.p.finish, at(6, 17));
        // A drives C exactly (FF + 1 day): no slack, critical; the others
        // end before the finish and have it all as free slack.
        QCOMPARE(b.t(a).totalSlack, 0);
        QVERIFY(b.t(a).critical && b.t(c).critical);
        QCOMPARE(b.t(bb).totalSlack, 3 * kDay);
        QCOMPARE(b.t(bb).freeSlack, 3 * kDay);
        QCOMPARE(b.t(d).totalSlack, 2 * kDay);
        QVERIFY(!b.t(bb).critical && !b.t(d).critical);
        // Late dates of the critical chain equal the early ones.
        QCOMPARE_AT(b.t(a).lateStart, b.t(a).earlyStart);
        QCOMPARE_AT(b.t(c).lateFinish, at(6, 17));
    }

    void deadlineIsALateFinish()
    {
        // A 3d → B 2d (Mon–Fri); C 2d alone (Mon–Tue, 3 days of slack).
        Builder b;
        const int a = b.task(QStringLiteral("A"), 3 * kDay);
        const int bb = b.task(QStringLiteral("B"), 2 * kDay);
        const int c = b.task(QStringLiteral("C"), 2 * kDay);
        b.link(bb, a);
        b.run();
        QCOMPARE(b.t(c).totalSlack, 3 * kDay);
        // B must be done by Thursday: it still finishes Friday (a deadline
        // moves nothing) but now has −1 day of slack, and so has A.
        b.p.tasks[bb].deadline = at(5, 17);
        b.run();
        QCOMPARE_AT(b.t(bb).finish, at(6, 17));
        QCOMPARE_AT(b.t(bb).lateFinish, at(5, 17));
        QCOMPARE(b.t(bb).totalSlack, -kDay);
        QCOMPARE(b.t(a).totalSlack, -kDay);
        QVERIFY(b.t(a).critical && b.t(bb).critical);
        QCOMPARE(b.t(c).totalSlack, 3 * kDay);
        // A deadline after the late finish changes nothing.
        b.p.tasks[c].deadline = at(13, 17);
        b.run();
        QCOMPARE(b.t(c).totalSlack, 3 * kDay);
        // On C, Monday evening: −1 day, critical.
        b.p.tasks[c].deadline = at(2, 17);
        b.run();
        QCOMPARE(b.t(c).totalSlack, -kDay);
        QVERIFY(b.t(c).critical);
        // A deadline on a summary bounds its subtasks.
        Builder s;
        const int sum = s.task(QStringLiteral("Phase"), 0, 1);
        const int x = s.task(QStringLiteral("X"), 2 * kDay, 2);
        const int y = s.task(QStringLiteral("Y"), 4 * kDay, 1);
        s.run();
        QCOMPARE(s.t(x).totalSlack, 2 * kDay);
        s.p.tasks[sum].deadline = at(2, 17);
        s.run();
        QCOMPARE(s.t(x).totalSlack, -kDay);
        QVERIFY(s.t(sum).critical);
        QVERIFY(s.t(y).critical);
    }

    void criticalSlackLimit()
    {
        Builder b;
        const int a = b.task(QStringLiteral("A"), 3 * kDay);
        const int bb = b.task(QStringLiteral("B"), 2 * kDay);
        const int c = b.task(QStringLiteral("C"), 2 * kDay);
        b.link(bb, a);
        b.run();
        QVERIFY(!b.t(c).critical);
        b.p.info.criticalSlackDays = 2;
        b.run();
        QVERIFY(!b.t(c).critical); // 3 days of slack > 2
        b.p.info.criticalSlackDays = 3;
        b.run();
        QVERIFY(b.t(c).critical); // ≤ 3 days
        QCOMPARE(b.t(c).totalSlack, 3 * kDay);
    }

    void multipleCriticalPaths()
    {
        // A 3d → B 2d ends Friday; C 2d → D 1d ends Wednesday.
        Builder b;
        const int a = b.task(QStringLiteral("A"), 3 * kDay);
        const int bb = b.task(QStringLiteral("B"), 2 * kDay);
        const int c = b.task(QStringLiteral("C"), 2 * kDay);
        const int d = b.task(QStringLiteral("D"), kDay);
        b.link(bb, a);
        b.link(d, c);
        b.run();
        QCOMPARE(b.t(d).totalSlack, 2 * kDay);
        QVERIFY(!b.t(c).critical && !b.t(d).critical);
        b.p.info.multipleCriticalPaths = true;
        b.run();
        // D drives nothing, so it ends a critical path of its own, and C
        // drives D exactly.
        QCOMPARE(b.t(d).totalSlack, 0);
        QCOMPARE(b.t(c).totalSlack, 0);
        QVERIFY(b.t(c).critical && b.t(d).critical);
        QVERIFY(b.t(a).critical && b.t(bb).critical);
        QCOMPARE_AT(b.t(d).lateFinish, at(4, 17));
    }

    void levelingDelayElapsedAndWorking()
    {
        // P 3d Mon–Wed → T 2d from Thu 08:00 unleveled.
        Builder b;
        const int p = b.task(QStringLiteral("P"), 3 * kDay);
        const int t = b.task(QStringLiteral("T"), 2 * kDay);
        b.link(t, p);
        b.run();
        QCOMPARE_AT(b.t(t).start, at(5));
        QCOMPARE_AT(b.t(t).unleveledStart, at(5));
        // 36 elapsed hours from Thu 08:00 is Fri 20:00 → Monday 08:00.
        b.p.tasks[t].levelingDelay = 36 * 60;
        b.p.tasks[t].levelingDelayElapsed = true;
        b.run();
        QCOMPARE_AT(b.t(t).unleveledStart, at(5));
        QCOMPARE_AT(b.t(t).start, at(9));
        QCOMPARE_AT(b.t(t).finish, at(10, 17));
        // 1.5 working days from Thu 08:00 is Fri 12:00 → after lunch.
        b.p.tasks[t].levelingDelay = 720;
        b.p.tasks[t].levelingDelayElapsed = false;
        b.run();
        QCOMPARE_AT(b.t(t).start, at(6, 13));
        // Relative: P one day longer moves T's start one day too.
        b.p.tasks[p].duration += kDay;
        b.run();
        QCOMPARE_AT(b.t(t).unleveledStart, at(6));
        QCOMPARE_AT(b.t(t).start, at(9, 13));
        // A Must Start On task is pinned: the delay does not apply.
        b.p.tasks[t].constraint = ConstraintType::MustStartOn;
        b.p.tasks[t].constraintDate = at(10);
        b.run();
        QCOMPARE_AT(b.t(t).start, at(10));
    }
};

QTEST_GUILESS_MAIN(PlanCriticalTest)

#include "tst_plan_critical.moc"
