// SPDX-License-Identifier: GPL-3.0-or-later
// Resources, leveling, baselines and reports: per-day overallocation and
// the usage table (resources.h), the leveling heuristic within slack and
// beyond it (leveling.h), baselines and variances (baselines.h), and the
// four reports (plan_reports.h). Standard calendar, Monday 2 March 2026.

#include "baselines.h"
#include "leveling.h"
#include "plan_edit.h"
#include "plan_fixture.h"
#include "plan_reports.h"
#include "resources.h"

using namespace PlanTest;

namespace {

// R works on A (2 days, slack 3 days) and B (3 days, critical: C follows
// it for 2 days), all starting Monday — R is double-booked Mon and Tue.
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
};

} // namespace

class PlanResourcesTest final : public QObject {
    Q_OBJECT
private slots:
    void overallocationPerDay()
    {
        Clash x;
        const Project &p = x.b.p;
        QVERIFY(p.resources.first().overallocated);
        QVERIFY(p.tasks.at(x.a).overallocated);
        QVERIFY(p.tasks.at(x.bb).overallocated);
        QVERIFY(!p.tasks.at(x.c).overallocated);
        QCOMPARE(firstOverallocatedDay(p, x.r), QDate(2026, 3, 2));
        QCOMPARE(firstOverallocatedDay(p, x.r, QDate(2026, 3, 4)), QDate());
        const DailyLoad load = dailyLoad(p);
        QCOMPARE(load.value(x.r).value(QDate(1970, 1, 1).daysTo(QDate(2026, 3, 3))), 960 * 100);
        // Half-time on each is fine.
        Project half = p;
        for (Assignment &a : half.assignments) {
            a.units = 50;
        }
        schedule(half);
        QVERIFY(!half.resources.first().overallocated);
    }

    void usageTable()
    {
        Clash x;
        const UsageTable days = QindaOffice::Plan::usageTable(x.b.p, QDate(2026, 3, 2), 7, false);
        QCOMPARE(days.rows.size(), 3); // R, then its two assignments
        const UsageRow &r = days.rows.at(0);
        QCOMPARE(r.id, QStringLiteral("r:%1").arg(x.r));
        QCOMPARE(r.work.at(0), 960);
        QCOMPARE(r.work.at(2), 480);
        QCOMPARE(r.capacity.at(0), 480);
        QCOMPARE(r.capacity.at(5), 0); // Saturday
        QCOMPARE(r.capacityPerBucket, 480);
        QVERIFY(r.over.at(0) && r.over.at(1) && !r.over.at(2));
        QCOMPARE(r.total, 5 * kDay);
        QVERIFY(days.rows.at(1).assignment);
        QCOMPARE(days.rows.at(1).name, QStringLiteral("A"));
        QCOMPARE(days.rows.at(1).total, 2 * kDay);
        const UsageTable weeks = QindaOffice::Plan::usageTable(x.b.p, QDate(2026, 3, 4), 2, true);
        QCOMPARE(weeks.bucketStarts.first(), QDate(2026, 3, 2)); // snapped to Monday
        QCOMPARE(weeks.rows.at(0).work.at(0), 5 * kDay);
        QCOMPARE(weeks.rows.at(0).capacity.at(0), 5 * kDay);
        QVERIFY(weeks.rows.at(0).over.at(0));
    }

    void levelingWithinSlack()
    {
        // C lasts 2 days: B+C end Friday; A (slack 3 days) can move to
        // Thu–Fri without moving the finish.
        Clash x(2 * kDay);
        const Minutes finish = x.b.p.finish;
        const LevelingResult r = levelResources(x.b.p, x.r);
        QVERIFY(r.resolved);
        QVERIFY(!r.finishMoved);
        QCOMPARE(r.delayedTasks, 1);
        QCOMPARE_AT(x.b.t(x.a).start, at(5));
        QCOMPARE_AT(x.b.t(x.a).finish, at(6, 17));
        QCOMPARE_AT(x.b.p.finish, finish);
        QVERIFY(!x.b.p.resources.first().overallocated);
        QCOMPARE(clearLeveling(x.b.p), 1);
        QVERIFY(x.b.p.resources.first().overallocated);
    }

    void levelingBeyondSlack()
    {
        // C lasts 1 day: A's slack (2 days) is too little, so the finish moves.
        Clash x(kDay);
        const LevelingResult r = levelResources(x.b.p);
        QVERIFY(r.resolved);
        QVERIFY(r.finishMoved);
        QCOMPARE_AT(x.b.t(x.a).start, at(5));
        QCOMPARE_AT(x.b.p.finish, at(6, 17));
    }

    void levelingLeavesStartedAndPinnedWork()
    {
        Clash x;
        x.b.p.tasks[x.a].percentComplete = 10;
        x.b.p.tasks[x.a].actualStart = at(2);
        x.b.p.tasks[x.bb].constraint = ConstraintType::MustStartOn;
        x.b.p.tasks[x.bb].constraintDate = at(2);
        const LevelingResult r = levelResources(x.b.p, x.r);
        QVERIFY(!r.resolved);
        QCOMPARE(r.delayedTasks, 0);
        QVERIFY(r.unresolvedDays.contains(QStringLiteral("2026-03-02")));
    }

    void baselinesAndVariance()
    {
        Clash x;
        Project &p = x.b.p;
        QVERIFY(!hasBaseline(p, 0));
        QCOMPARE(setBaseline(p, 0), 3);
        QVERIFY(hasBaseline(p, 0));
        QCOMPARE(setBaseline(p, 2, {p.tasks.at(x.a).uid}), 1);
        QCOMPARE(setBaseline(p, 3), 0);
        p.tasks[x.bb].duration += kDay;
        schedule(p);
        const Variance v = variance(p, p.tasks.at(x.c), 0);
        QVERIFY(v.valid);
        QCOMPARE(v.start, kDay);
        QCOMPARE(v.finish, kDay);
        QCOMPARE(v.duration, 0);
        const Variance vb = variance(p, p.tasks.at(x.bb), 0);
        QCOMPARE(vb.duration, kDay);
        QCOMPARE(vb.cost, 8 * 6000);
        QVERIFY(!variance(p, p.tasks.at(x.bb), 1).valid);
        QCOMPARE(taskFieldText(p, x.c, QStringLiteral("finishVariance"), QLocale::c()), QStringLiteral("1 day"));
        QCOMPARE(clearBaseline(p, 0), 3);
        QVERIFY(!hasBaseline(p, 0));
    }

    void reports()
    {
        Clash x;
        Project &p = x.b.p;
        p.info.title = QStringLiteral("Report fixture");
        const int m = x.b.task(QStringLiteral("Done"), 0);
        x.b.link(m, x.c);
        schedule(p);
        setBaseline(p, 0);
        p.tasks[x.bb].duration += kDay; // B slips a day, so C and the milestone slip
        schedule(p);
        const PlanReport overview = buildReport(p, QStringLiteral("overview"), QDate(2026, 3, 4), QLocale::c());
        QCOMPARE(overview.title, QStringLiteral("Project Overview"));
        QVERIFY(overview.subtitle.startsWith(QStringLiteral("Report fixture")));
        const auto kpi = [&](const QString &id) {
            for (const ReportKpi &k : overview.kpis) {
                if (k.id == id) {
                    return k;
                }
            }
            return ReportKpi{};
        };
        QCOMPARE(kpi(QStringLiteral("finish")).value, QStringLiteral("Mon 9 Mar 26"));
        QCOMPARE(kpi(QStringLiteral("finish")).detail, QStringLiteral("1 day behind the baseline"));
        QCOMPARE(kpi(QStringLiteral("finish")).tone, QStringLiteral("danger"));
        QCOMPARE(kpi(QStringLiteral("complete")).percent, 0);
        QVERIFY(kpi(QStringLiteral("cost")).detail.contains(QStringLiteral("over budget")));
        // Late as of Wed 4 Mar 17:00: A and B should be under way (nothing
        // was marked), B, C and the milestone slipped.
        QCOMPARE(kpi(QStringLiteral("late")).value, QStringLiteral("4"));
        const ReportTable &milestones = overview.tables.at(1);
        QCOMPARE(milestones.rows.size(), 1);
        QVERIFY(milestones.rows.first().at(3).startsWith(QStringLiteral("Slipped")));
        const PlanReport critical = buildReport(p, QStringLiteral("critical"), QDate(2026, 3, 4), QLocale::c());
        QCOMPARE(critical.tables.first().rows.size(), 3); // B, C and the milestone
        const PlanReport resources = buildReport(p, QStringLiteral("resources"), QDate(2026, 3, 4), QLocale::c());
        QCOMPARE(resources.tables.first().rows.first().at(5), QStringLiteral("Overallocated"));
        QCOMPARE(resources.tables.first().rowTones.first(), QStringLiteral("danger"));
        QCOMPARE(buildReport(p, QStringLiteral("nonsense"), QDate(2026, 3, 4)).id, QStringLiteral("overview"));
        QCOMPARE(reportIds().size(), 4);
    }
};

QTEST_GUILESS_MAIN(PlanResourcesTest)

#include "tst_plan_resources.moc"
