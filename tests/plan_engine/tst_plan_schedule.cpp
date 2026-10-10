// SPDX-License-Identifier: GPL-3.0-or-later
// The scheduler against hand-computed fixtures (standard calendar, project
// start Monday 2 March 2026 08:00; every expected date is worked out in the
// comment beside it). Covers the four link types with lag, lead and
// percentage lag, the eight constraints, summaries and their links,
// milestones, inactive and manual tasks, actuals, slack and the critical
// path, cycles from files, resource calendars, costs and the link check.

#include "outline.h"
#include "plan_fixture.h"

using namespace PlanTest;

class PlanScheduleTest final : public QObject {
    Q_OBJECT
private slots:
    void finishToStartChainIsCritical()
    {
        Builder b;
        const int a = b.task(QStringLiteral("A"), 3 * kDay);
        const int bb = b.task(QStringLiteral("B"), 2 * kDay);
        const int c = b.task(QStringLiteral("C"), kDay);
        b.link(bb, a);
        b.link(c, bb);
        const Project &p = b.run();
        QCOMPARE_AT(b.t(a).start, at(2));
        QCOMPARE_AT(b.t(a).finish, at(4, 17));  // Mon–Wed
        QCOMPARE_AT(b.t(bb).start, at(5));      // Thu
        QCOMPARE_AT(b.t(bb).finish, at(6, 17)); // Fri
        QCOMPARE_AT(b.t(c).start, at(9));       // over the weekend
        QCOMPARE_AT(p.finish, at(9, 17));
        for (int i : {a, bb, c}) {
            QVERIFY(b.t(i).critical);
            QCOMPARE(b.t(i).totalSlack, 0);
            QCOMPARE(b.t(i).freeSlack, 0);
        }
        QVERIFY(p.issues.isEmpty());
        QCOMPARE(b.t(c).id, 3);
        QCOMPARE(b.t(c).wbs, QStringLiteral("3"));
    }

    void parallelPathHasSlack()
    {
        Builder b;
        const int a = b.task(QStringLiteral("A"), 3 * kDay);
        const int bb = b.task(QStringLiteral("B"), 2 * kDay);
        const int c = b.task(QStringLiteral("C"), kDay);
        const int d = b.task(QStringLiteral("D"), kDay);
        b.link(bb, a);
        b.link(c, a);
        b.link(d, bb);
        b.link(d, c);
        b.run();
        QCOMPARE_AT(b.t(c).start, at(5));
        QCOMPARE_AT(b.t(c).finish, at(5, 17));
        QCOMPARE_AT(b.t(d).start, at(9));
        // C can slip one day (Thu → Fri) before D moves.
        QCOMPARE(b.t(c).totalSlack, kDay);
        QCOMPARE(b.t(c).freeSlack, kDay);
        QCOMPARE_AT(b.t(c).lateFinish, at(6, 17));
        QCOMPARE_AT(b.t(c).lateStart, at(6));
        QVERIFY(!b.t(c).critical);
        QVERIFY(b.t(a).critical && b.t(bb).critical && b.t(d).critical);
    }

    void linkTypesWithLagAndLead()
    {
        Builder b;
        const int a = b.task(QStringLiteral("A"), 4 * kDay);      // Mon–Thu
        const int ss = b.task(QStringLiteral("SS+1d"), 2 * kDay);
        const int ff = b.task(QStringLiteral("FF"), 2 * kDay);
        const int lead = b.task(QStringLiteral("FS-1d"), 2 * kDay);
        const int pct = b.task(QStringLiteral("FS+50%"), kDay);
        const int hours = b.task(QStringLiteral("FS+4h"), kDay);
        b.link(ss, a, LinkType::StartToStart, kDay);
        b.link(ff, a, LinkType::FinishToFinish);
        b.link(lead, a, LinkType::FinishToStart, -kDay);
        b.link(pct, a);
        b.p.tasks[pct].predecessors[0].lagPercent = 50;
        b.link(hours, a, LinkType::FinishToStart, 4 * kHour);
        b.run();
        QCOMPARE_AT(b.t(ss).start, at(3));        // Mon 08:00 + 1 day = Mon 17:00 → Tue 08:00
        QCOMPARE_AT(b.t(ss).finish, at(4, 17));
        QCOMPARE_AT(b.t(ff).start, at(4));        // finishes with A on Thu 17:00
        QCOMPARE_AT(b.t(ff).finish, at(5, 17));
        QCOMPARE_AT(b.t(lead).start, at(5));      // overlaps A's last day
        QCOMPARE_AT(b.t(lead).finish, at(6, 17));
        QCOMPARE_AT(b.t(pct).start, at(10));      // 50% of 4 days = 2 days: Fri, Mon → Tue
        QCOMPARE_AT(b.t(hours).start, at(6, 13)); // Fri morning is the lag
        QCOMPARE_AT(b.t(hours).finish, at(9, 12));
    }

    void startToFinish()
    {
        Builder b;
        const int a = b.task(QStringLiteral("A"), 2 * kDay);
        const int sf = b.task(QStringLiteral("SF"), kDay);
        b.constrain(a, ConstraintType::StartNoEarlierThan, at(4)); // A: Wed–Thu
        b.link(sf, a, LinkType::StartToFinish);
        b.run();
        QCOMPARE_AT(b.t(a).start, at(4));
        QCOMPARE_AT(b.t(sf).finish, at(3, 17));  // SF finishes when A starts (Tue 17:00 ≡ Wed 08:00)…
        QCOMPARE_AT(b.t(sf).start, at(3));       // … so it is Tuesday's work
    }

    void constraints()
    {
        Builder b;
        const int a = b.task(QStringLiteral("A"), 3 * kDay);        // Mon–Wed
        const int fnlt = b.task(QStringLiteral("FNLT"), 2 * kDay);  // wants Thu–Fri
        const int mso = b.task(QStringLiteral("MSO"), kDay);
        const int mfo = b.task(QStringLiteral("MFO"), 2 * kDay);
        const int snet = b.task(QStringLiteral("SNET"), kDay);
        const int fnet = b.task(QStringLiteral("FNET"), kDay);
        const int snlt = b.task(QStringLiteral("SNLT"), kDay);
        b.link(fnlt, a);
        b.constrain(fnlt, ConstraintType::FinishNoLaterThan, at(5, 17));
        b.link(mso, a);
        b.constrain(mso, ConstraintType::MustStartOn, at(10));
        b.constrain(mfo, ConstraintType::MustFinishOn, at(13, 17));
        b.constrain(snet, ConstraintType::StartNoEarlierThan, at(4));
        b.constrain(fnet, ConstraintType::FinishNoEarlierThan, at(6, 17));
        b.link(snlt, a);
        b.constrain(snlt, ConstraintType::StartNoLaterThan, at(9));
        const Project &p = b.run();
        // The FNLT constraint wins over the link: B is pulled back to Wed–Thu,
        // flagged, and A gets negative slack (it would have to finish a day
        // earlier).
        QCOMPARE_AT(b.t(fnlt).start, at(4));
        QCOMPARE_AT(b.t(fnlt).finish, at(5, 17));
        QVERIFY(b.t(fnlt).constraintConflict);
        QVERIFY(!b.t(fnlt).warning.isEmpty());
        QCOMPARE(b.t(a).totalSlack, -kDay);
        QVERIFY(b.t(a).critical);
        QCOMPARE_AT(b.t(mso).start, at(10));
        QVERIFY(!b.t(mso).constraintConflict);
        QCOMPARE_AT(b.t(mfo).start, at(12));
        QCOMPARE_AT(b.t(mfo).finish, at(13, 17));
        QCOMPARE_AT(b.t(snet).start, at(4));
        QCOMPARE_AT(b.t(fnet).start, at(6));
        QCOMPARE_AT(b.t(snlt).start, at(5));      // the link (Thu) is before the limit
        QVERIFY(!b.t(snlt).constraintConflict);
        int conflicts = 0;
        for (const ScheduleIssue &issue : p.issues) {
            conflicts += issue.message.contains(QStringLiteral("FNLT")) ? 1 : 0;
        }
        QCOMPARE(conflicts, 1);
    }

    void asLateAsPossibleUsesFreeSlack()
    {
        Builder b;
        const int a = b.task(QStringLiteral("A"), 3 * kDay);
        const int alap = b.task(QStringLiteral("B"), kDay);
        const int c = b.task(QStringLiteral("C"), 5 * kDay);
        const int d = b.task(QStringLiteral("D"), kDay);
        b.link(alap, a);
        b.link(d, alap);
        b.link(d, c);
        b.constrain(alap, ConstraintType::AsLateAsPossible, kNoDate);
        b.run();
        QCOMPARE_AT(b.t(alap).earlyStart, at(5));  // Thursday as soon as possible…
        QCOMPARE_AT(b.t(alap).start, at(6));       // … Friday as late as possible
        QCOMPARE_AT(b.t(alap).finish, at(6, 17));
        QCOMPARE_AT(b.t(d).start, at(9));          // D unmoved
    }

    void summariesRollUpAndCarryLinks()
    {
        Builder b;
        const int p1 = b.task(QStringLiteral("Phase 1"), 0);
        const int a = b.task(QStringLiteral("A"), 2 * kDay, 2);
        const int bb = b.task(QStringLiteral("B"), 3 * kDay, 2);
        const int p2 = b.task(QStringLiteral("Phase 2"), 0);
        const int c = b.task(QStringLiteral("C"), kDay, 2);
        const int d = b.task(QStringLiteral("D"), 2 * kDay, 2);
        const int m = b.task(QStringLiteral("Done"), 0, 1);
        b.link(bb, a);
        b.link(p2, p1);  // the summary link drives both of Phase 2's children
        b.link(m, p2);
        b.p.tasks[a].percentComplete = 100;
        b.p.tasks[a].actualStart = at(2);
        b.p.tasks[a].actualFinish = at(3, 17);
        const Project &p = b.run();
        QVERIFY(b.t(p1).summary && b.t(p2).summary && !b.t(a).summary);
        QCOMPARE(b.t(bb).wbs, QStringLiteral("1.2"));
        QCOMPARE(b.t(d).wbs, QStringLiteral("2.2"));
        QCOMPARE(b.t(c).parentIndex, p2);
        QCOMPARE_AT(b.t(p1).start, at(2));
        QCOMPARE_AT(b.t(p1).finish, at(6, 17));
        QCOMPARE(b.t(p1).duration, 5 * kDay);
        QCOMPARE(b.t(p1).percentComplete, 40);    // (2 days × 100%) / 5 days of children
        QCOMPARE_AT(b.t(c).start, at(9));
        QCOMPARE_AT(b.t(d).start, at(9));
        QCOMPARE_AT(b.t(p2).finish, at(10, 17));
        QCOMPARE(b.t(c).totalSlack, kDay);        // D is the longer child
        QVERIFY(b.t(d).critical && !b.t(c).critical);
        QVERIFY(b.t(p2).critical);
        QVERIFY(b.t(m).isMilestone());
        QCOMPARE_AT(b.t(m).start, at(10, 17));    // a milestone sits at its predecessor's finish
        QVERIFY(!b.t(a).critical);                // complete tasks are never critical
        QCOMPARE_AT(p.finish, at(10, 17));
    }

    void inactiveManualAndActuals()
    {
        Builder b;
        const int a = b.task(QStringLiteral("A"), kDay);
        const int x = b.task(QStringLiteral("X (inactive)"), 5 * kDay);
        const int after = b.task(QStringLiteral("After X"), kDay);
        const int manual = b.task(QStringLiteral("Manual"), kDay);
        const int succ = b.task(QStringLiteral("After manual"), kDay);
        const int started = b.task(QStringLiteral("Started late"), 3 * kDay);
        b.link(x, a);
        b.link(after, x);
        b.p.tasks[x].active = false;
        b.p.tasks[manual].manual = true;
        b.p.tasks[manual].manualStart = at(11);
        b.p.tasks[manual].manualFinish = at(12, 17);
        b.link(succ, manual);
        b.p.tasks[started].actualStart = at(3);
        b.p.tasks[started].percentComplete = 50;
        const Project &p = b.run();
        QCOMPARE_AT(b.t(after).start, at(2));     // the inactive predecessor drives nothing
        QVERIFY(!b.t(x).effectiveActive);
        QVERIFY(!b.t(x).critical);
        QCOMPARE_AT(b.t(manual).start, at(11));
        QCOMPARE_AT(b.t(manual).finish, at(12, 17));
        QCOMPARE_AT(b.t(succ).start, at(13));
        QCOMPARE_AT(b.t(started).start, at(3));
        QCOMPARE_AT(b.t(started).finish, at(5, 17));
        QCOMPARE_AT(p.finish, at(13, 17));        // the inactive task's week does not count
    }

    void cyclesFromFilesAreIgnoredWithAnIssue()
    {
        Builder b;
        const int a = b.task(QStringLiteral("A"), kDay);
        const int bb = b.task(QStringLiteral("B"), kDay);
        const int c = b.task(QStringLiteral("C"), kDay);
        b.link(bb, a);
        b.link(a, bb);
        b.link(c, bb);
        b.p.tasks[c].predecessors.append(Link{4242, LinkType::FinishToStart, 0, std::nullopt, false, {}});
        const Project &p = b.run();
        QVERIFY(p.issues.size() >= 3);
        QVERIFY(b.t(a).warning.contains(QStringLiteral("circular")));
        QVERIFY(b.t(c).warning.contains(QStringLiteral("no longer exists")));
        QCOMPARE_AT(b.t(a).start, at(2));
        QCOMPARE_AT(b.t(c).start, at(3));         // B → C still holds
        QCOMPARE(b.t(a).ignoredLinks, QList<int>{0});
        QCOMPARE(b.t(bb).ignoredLinks, QList<int>{0});
        QCOMPARE(b.t(c).ignoredLinks, QList<int>{1}); // only the dangling one
    }

    void linkProblems()
    {
        Builder b;
        const int s = b.task(QStringLiteral("Summary"), 0);
        const int a = b.task(QStringLiteral("A"), kDay, 2);
        const int bb = b.task(QStringLiteral("B"), kDay, 2);
        const int c = b.task(QStringLiteral("C"), kDay);
        b.link(bb, a);
        b.link(c, bb);
        b.run();
        QVERIFY(!linkProblem(b.p, a, a).isEmpty());
        QVERIFY(linkProblem(b.p, s, a).contains(QStringLiteral("own subtasks")));
        QVERIFY(linkProblem(b.p, a, bb).contains(QStringLiteral("already linked")));
        QVERIFY(linkProblem(b.p, c, a).contains(QStringLiteral("circular")));
        QVERIFY(linkProblem(b.p, c, s, LinkType::FinishToFinish).contains(QStringLiteral("summary")));
        QVERIFY(linkProblem(b.p, c, s).contains(QStringLiteral("circular"))); // C follows B, inside S
        QVERIFY(linkProblem(b.p, a, c).isEmpty());
    }

    void resourceCalendarDrivesTheTask()
    {
        Builder b;
        Calendar ana;
        ana.uid = 2;
        ana.name = QStringLiteral("Ana");
        ana.baseUid = 1;
        ana.exceptions.append({QDate(2026, 3, 2), QDate(2026, 3, 4), QStringLiteral("Away"), {}});
        b.p.calendars.append(ana);
        const int t = b.task(QStringLiteral("Hers"), 2 * kDay);
        const int r = b.resource(QStringLiteral("Ana"));
        b.p.resources.last().calendarUid = 2;
        b.assign(t, r);
        b.run();
        QCOMPARE(effectiveCalendarUid(b.p, t), 2);
        QCOMPARE_AT(b.t(t).start, at(5));
        QCOMPARE_AT(b.t(t).finish, at(6, 17));
    }

    void workAndCost()
    {
        Builder b;
        const int t = b.task(QStringLiteral("Build"), 2 * kDay);
        const int s = b.task(QStringLiteral("Summary"), 0);
        const int child = b.task(QStringLiteral("Child"), kDay, 2);
        const int r = b.resource(QStringLiteral("Carpenter"), 5000); // $50.00/h
        b.p.resources.last().costPerUse = 2500;
        const int wood = b.resource(QStringLiteral("Lumber"), 400, 100, ResourceType::Material);
        b.assign(t, r);
        b.assign(t, wood, 250); // 2.5 units at $4.00
        b.assign(child, r, 50);
        b.p.tasks[t].fixedCost = 10000;
        const Project &p = b.run();
        QCOMPARE(b.t(t).work, 960);                        // 16 h
        QCOMPARE(b.t(t).cost, 80000 + 2500 + 1000 + 10000);
        QCOMPARE(b.t(child).work, 240);
        QCOMPARE(b.t(s).work, 240);
        QCOMPARE(b.t(s).cost, 20000 + 2500);
        const int ri = p.resourceIndex(r);
        QCOMPARE(p.resources.at(ri).work, 1200);
        QCOMPARE(p.resources.at(ri).cost, 80000 + 2500 + 20000 + 2500);
        QCOMPARE(p.resources.at(p.resourceIndex(wood)).work, 250);
    }

    void recalculatesTwoThousandTasksQuickly()
    {
        Builder b;
        int previous = -1;
        for (int phase = 0; phase < 100; ++phase) {
            b.task(QStringLiteral("Phase %1").arg(phase), 0);
            for (int k = 0; k < 19; ++k) {
                const int i = b.task(QStringLiteral("Task %1.%2").arg(phase).arg(k), kDay * (1 + k % 3), 2);
                if (previous >= 0 && k % 4 != 1) {
                    b.link(i, previous, k % 5 == 0 ? LinkType::StartToStart : LinkType::FinishToStart, k % 7 == 0 ? kHour : 0);
                }
                previous = i;
            }
        }
        QCOMPARE(b.p.tasks.size(), 2000);
        QElapsedTimer timer;
        timer.start();
        schedule(b.p);
        const qint64 ms = timer.elapsed();
        qInfo("2,000 tasks scheduled in %lld ms", ms);
        QVERIFY2(ms < 1000, qPrintable(QString::number(ms)));
        QVERIFY(b.p.issues.isEmpty());
        QVERIFY(b.p.finish > b.p.info.start);
    }
};

QTEST_GUILESS_MAIN(PlanScheduleTest)

#include "tst_plan_schedule.moc"
