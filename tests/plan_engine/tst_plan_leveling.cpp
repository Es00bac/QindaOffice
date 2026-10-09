// SPDX-License-Identifier: GPL-3.0-or-later
// M79 (D-088): Project's resource leveling options over the leveling
// heuristic (leveling.h) — the leveling order (Standard; Priority,
// Standard; ID Only), priority 1000 never moves, "level only within
// available slack", the leveling range, Level Selection, Clear Leveling of
// selected tasks, the pre-leveled dates, and the delay kept as Project's
// relative Leveling Delay. Standard calendar from Monday 2 March 2026.

#include "leveling.h"
#include "plan_fixture.h"
#include "resources.h"

using namespace PlanTest;

namespace {

// R works on A (2 days, 3 days of slack) and B (3 days, critical: C
// follows it for 2 days), all from Monday — R is double-booked Mon and Tue.
struct Clash {
    Builder b;
    int a = 0;
    int bb = 0;
    int c = 0;
    int r = 0;
    explicit Clash(Minutes cDuration = 2 * kDay)
    {
        a = b.task(QStringLiteral("A"), 2 * kDay);
        bb = b.task(QStringLiteral("B"), 3 * kDay);
        c = b.task(QStringLiteral("C"), cDuration);
        b.link(c, bb);
        r = b.resource(QStringLiteral("R"), 6000);
        b.assign(a, r);
        b.assign(bb, r);
        b.run();
    }
    LevelingOptions options() const { return projectLevelingOptions(b.p, r); }
};

} // namespace

class PlanLevelingTest final : public QObject {
    Q_OBJECT
private slots:
    void standardOrderDelaysTheTaskWithSlack()
    {
        Clash x;
        const LevelingResult r = levelResources(x.b.p, x.options());
        QVERIFY(r.resolved);
        QVERIFY(!r.finishMoved);
        // A waits for B: from Mon 08:00 to Thu 08:00 — 3 elapsed days, the
        // Leveling Delay Project writes ("3 edays").
        QCOMPARE(x.b.t(x.a).levelingDelay, 3 * 1440);
        QVERIFY(x.b.t(x.a).levelingDelayElapsed);
        QCOMPARE_AT(x.b.t(x.a).start, at(5));
        QCOMPARE(formatLevelingDelay(x.b.t(x.a), x.b.p.info, QLocale::c()), QStringLiteral("3 edays"));
        // What it was before leveling, for the Leveling Gantt.
        QCOMPARE_AT(x.b.t(x.a).preLeveledStart, at(2));
        QCOMPARE_AT(x.b.t(x.a).preLeveledFinish, at(3, 17));
        QCOMPARE_AT(x.b.t(x.bb).preLeveledStart, at(2));
        QCOMPARE(x.b.t(x.bb).levelingDelay, 0);
    }

    void idOnlyDelaysTheHigherId()
    {
        Clash x;
        LevelingOptions o = x.options();
        o.order = LevelingOrder::IdOnly;
        const LevelingResult r = levelResources(x.b.p, o);
        QVERIFY(r.resolved);
        // A (ID 1) keeps its dates; B (ID 2, critical) waits — past its
        // slack, so the finish moves: B Wed–Fri, C the next Mon–Tue.
        QCOMPARE(x.b.t(x.a).levelingDelay, 0);
        QCOMPARE_AT(x.b.t(x.bb).start, at(4));
        QCOMPARE(x.b.t(x.bb).levelingDelay, 2 * 1440);
        QVERIFY(r.finishMoved);
        QCOMPARE_AT(x.b.p.finish, at(10, 17));
    }

    void priorityStandardKeepsTheHigherPriority()
    {
        Clash x;
        x.b.p.tasks[x.a].priority = 900;
        LevelingOptions o = x.options();
        o.order = LevelingOrder::PriorityStandard;
        levelResources(x.b.p, o);
        QCOMPARE(x.b.t(x.a).levelingDelay, 0);
        QCOMPARE_AT(x.b.t(x.bb).start, at(4));
        // Under Standard the slack decides, priority only breaks ties: A moves.
        Clash y;
        y.b.p.tasks[y.a].priority = 900;
        levelResources(y.b.p, y.options());
        QCOMPARE_AT(y.b.t(y.a).start, at(5));
        QCOMPARE(y.b.t(y.bb).levelingDelay, 0);
    }

    void priority1000IsNeverLeveled()
    {
        Clash x;
        x.b.p.tasks[x.a].priority = 1000; // Project's "Do Not Level"
        const LevelingResult r = levelResources(x.b.p, x.options());
        QCOMPARE(x.b.t(x.a).levelingDelay, 0);
        QCOMPARE_AT(x.b.t(x.a).start, at(2));
        QCOMPARE_AT(x.b.t(x.bb).start, at(4)); // B waits for A instead
        QVERIFY(r.resolved);
    }

    void withinAvailableSlackOnly()
    {
        // C lasts 1 day: A's 2 days of slack are too few to clear B.
        Clash x(kDay);
        const Minutes finish = x.b.p.finish;
        LevelingOptions o = x.options();
        o.withinSlackOnly = true;
        const LevelingResult r = levelResources(x.b.p, o);
        QVERIFY(!r.resolved);
        QCOMPARE(r.delayedTasks, 0);
        QCOMPARE(r.unresolvedDays, (QStringList{QStringLiteral("2026-03-02"), QStringLiteral("2026-03-03")}));
        QCOMPARE_AT(x.b.p.finish, finish);
        // The project's own option says the same.
        Clash y(kDay);
        y.b.p.info.levelWithinSlack = true;
        QVERIFY(!levelResources(y.b.p, y.r).resolved);
    }

    void levelingRangeAndSelection()
    {
        Clash x;
        LevelingOptions o = x.options();
        o.from = QDate(2026, 3, 4); // the clash is Mon–Tue: outside
        const LevelingResult r = levelResources(x.b.p, o);
        QCOMPARE(r.delayedTasks, 0);
        QVERIFY(r.resolved); // nothing overbooked inside the range
        QVERIFY(x.b.p.resources.first().overallocated);
        // Level Selection: only B may move, so B waits for A.
        Clash y;
        LevelingOptions s = y.options();
        s.taskUids = {y.b.t(y.bb).uid};
        const LevelingResult rs = levelResources(y.b.p, s);
        QVERIFY(rs.resolved);
        QCOMPARE(y.b.t(y.a).levelingDelay, 0);
        QCOMPARE_AT(y.b.t(y.bb).start, at(4));
        // A selection keeps the other tasks' delays (Clear First only
        // clears what it may move) and their pre-leveled dates.
        QCOMPARE(y.b.t(y.a).preLeveledStart, kNoDate);
    }

    void clearLevelingOfSelectedTasks()
    {
        Clash x;
        x.b.p.tasks[x.a].levelingDelay = 1440;
        x.b.p.tasks[x.bb].levelingDelay = 2880;
        schedule(x.b.p);
        QCOMPARE(clearLeveling(x.b.p, {x.b.t(x.a).uid}), 1);
        QCOMPARE(x.b.t(x.a).levelingDelay, 0);
        QCOMPARE(x.b.t(x.bb).levelingDelay, 2880);
        QCOMPARE(clearLeveling(x.b.p), 1);
        QCOMPARE(x.b.t(x.bb).levelingDelay, 0);
        QCOMPARE_AT(x.b.t(x.bb).start, at(2));
    }

    void delayFollowsPredecessors()
    {
        Clash x;
        levelResources(x.b.p, x.options());
        QCOMPARE_AT(x.b.t(x.a).start, at(5));
        // A new predecessor of A pushes its unleveled start to Tuesday; the
        // 3 edays of delay now end on Friday — as Project's relative delay.
        const int p = x.b.task(QStringLiteral("Prep"), kDay);
        x.b.link(x.a, p);
        x.b.run();
        QCOMPARE_AT(x.b.t(x.a).unleveledStart, at(3));
        QCOMPARE_AT(x.b.t(x.a).start, at(6));
    }

    void delayAsText()
    {
        ProjectInfo info;
        Minutes m = 0;
        bool elapsed = false;
        QVERIFY(parseLevelingDelay(QStringLiteral("2 edays"), info, &m, &elapsed));
        QCOMPARE(m, 2 * 1440);
        QVERIFY(elapsed);
        QVERIFY(parseLevelingDelay(QStringLiteral("1.5ed"), info, &m, &elapsed));
        QCOMPARE(m, 2160);
        QVERIFY(parseLevelingDelay(QStringLiteral("6 ehrs"), info, &m, &elapsed));
        QCOMPARE(m, 360);
        QVERIFY(parseLevelingDelay(QStringLiteral("2d"), info, &m, &elapsed));
        QCOMPARE(m, 2 * kDay);
        QVERIFY(!elapsed);
        QVERIFY(parseLevelingDelay(QStringLiteral("0"), info, &m, &elapsed));
        QCOMPARE(m, 0);
        QVERIFY(!parseLevelingDelay(QStringLiteral("soon"), info, &m, &elapsed));
        Task t;
        t.levelingDelay = 2160;
        QCOMPARE(formatLevelingDelay(t, info, QLocale::c()), QStringLiteral("1.50 edays"));
        t.levelingDelay = 360;
        QCOMPARE(formatLevelingDelay(t, info, QLocale::c()), QStringLiteral("6 ehrs"));
        t.levelingDelay = 1440;
        QCOMPARE(formatLevelingDelay(t, info, QLocale::c()), QStringLiteral("1 eday"));
        t.levelingDelay = 2 * kDay;
        t.levelingDelayElapsed = false;
        QCOMPARE(formatLevelingDelay(t, info, QLocale::c()), QStringLiteral("2 days"));
    }
};

QTEST_GUILESS_MAIN(PlanLevelingTest)

#include "tst_plan_leveling.moc"
