// SPDX-License-Identifier: GPL-3.0-or-later
// M83 (D-092), worked by hand on the standard calendar (Mon–Fri 08–12,
// 13–17; Monday 2 March 2026): elapsed durations and lags on the wall
// clock, split tasks and their Microsoft Project XML (assignment timephased
// data), Reschedule Uncompleted Work, the progress line, and actual work
// and cost typed.

#include "mspdi.h"
#include "plan_edit.h"
#include "plan_file.h"
#include "plan_fixture.h"
#include "resources.h"
#include "task_types.h"
#include "tracking.h"

#include <QRegularExpression>

using namespace PlanTest;

namespace {

const QLocale kC = QLocale::c();

QString s(const char *text)
{
    return QString::fromUtf8(text);
}

qint64 day(int d, int month = 3)
{
    return QDate(1970, 1, 1).daysTo(QDate(2026, month, d));
}

void type(Project &p, int index, const char *field, const char *text)
{
    const EditResult r = setTaskField(p, index, s(field), s(text), kC);
    QVERIFY2(r.ok, qPrintable(r.error));
}

} // namespace

class PlanTrackingM83Test final : public QObject {
    Q_OBJECT
private slots:
    void elapsedDurationsAndLags()
    {
        const std::optional<ParsedDuration> eh = parseDuration(s("3eh"), DurationFormat{});
        QVERIFY(eh && eh->elapsed && eh->minutes == 180);
        QCOMPARE(parseDuration(s("1emo"), DurationFormat{})->minutes, 43200);
        QCOMPARE(formatDuration(180, DurationUnit::Hours, DurationFormat{}, false, kC, true), s("3 ehrs"));
        Builder b;
        const int a = b.task(s("Cure the concrete"), kDay);
        const int next = b.task(s("Frame"), kDay);
        const int fri = b.task(s("Order"), kDay);
        const int after = b.task(s("Deliver"), kDay);
        b.constrain(a, ConstraintType::StartNoEarlierThan, at(6));
        b.link(next, a);
        b.constrain(fri, ConstraintType::StartNoEarlierThan, at(6));
        b.link(after, fri, LinkType::FinishToStart, 2 * 1440);
        b.p.tasks[after].predecessors[0].lagElapsed = true;
        const int ana = b.resource(s("Ana"));
        b.assign(a, ana);
        Project &p = b.run();
        type(p, a, "duration", "2ed");
        schedule(p);
        QVERIFY(p.tasks.at(a).durationElapsed);
        QCOMPARE(taskFieldText(p, a, s("duration"), kC), s("2 edays"));
        QCOMPARE_AT(p.tasks.at(a).finish, at(8, 8));    // Fri 08:00 + 48 h: Sunday 08:00
        QCOMPARE_AT(p.tasks.at(next).start, at(9, 8));  // the next working time
        QCOMPARE(p.tasks.at(a).work, 48 * kHour);       // around the clock at 100 %
        QCOMPARE_AT(p.tasks.at(after).start, at(9, 8)); // Fri 17:00 + 2 edays = Sun 17:00
        QCOMPARE(taskFieldText(p, after, s("predecessors"), kC), s("3FS+2 edays"));
        // Microsoft Project XML: format 8 both ways.
        const QByteArray xml = writeMspdi(p);
        QVERIFY(xml.contains("<Duration>PT48H0M0S</Duration>"));
        QVERIFY(xml.contains("<DurationFormat>8</DurationFormat>"));
        QVERIFY(xml.contains("<LinkLag>28800</LinkLag>"));
        QVERIFY(xml.contains("<LagFormat>8</LagFormat>"));
        Project back;
        const MspdiResult r = readMspdi(xml, &back);
        QVERIFY(r.ok);
        QVERIFY(!r.warnings.join(QLatin1Char('\n')).contains(s("lapsed")));
        QVERIFY(back.tasks.at(a).durationElapsed);
        QVERIFY(back.tasks.at(after).predecessors.first().lagElapsed);
        QCOMPARE(back.tasks.at(after).start, p.tasks.at(after).start);
        QCOMPARE(writeMspdi(back), xml);
    }

    void splitTasks()
    {
        Builder b;
        const int t = b.task(s("Paint"), 4 * kDay);
        const int u = b.task(s("Inspect"), 2 * kDay);
        const int ana = b.resource(s("Ana"));
        b.assign(t, ana);
        Project &p = b.run();
        QVERIFY(splitTask(p, t, at(3, 17), at(5, 8)).ok); // stops Tue night, resumes Thu
        schedule(p);
        QCOMPARE(p.tasks.at(t).splits, (QList<TaskSplit>{{2 * kDay, kDay}}));
        QCOMPARE(p.tasks.at(t).duration, 4 * kDay);
        QCOMPARE_AT(p.tasks.at(t).finish, at(6, 17));
        QVERIFY(taskFieldText(p, t, s("indicators"), kC).contains(s("split")));
        const DailyLoad load = dailyLoad(p);
        QCOMPARE(load.value(ana).value(day(3)), 480 * 100);
        QCOMPARE(load.value(ana).value(day(4)), 0); // the gap
        QCOMPARE(load.value(ana).value(day(6)), 480 * 100);
        QVERIFY(!splitTask(p, t, at(4, 10), at(4, 15)).ok); // inside the gap
        QVERIFY(!splitTask(p, t, at(9), at(10)).ok);        // past the finish
        QVERIFY(splitTask(p, u, at(2, 12), at(3, 12)).ok);  // a task without resources
        schedule(p);
        QCOMPARE_AT(p.tasks.at(u).finish, at(4, 17)); // 4 h, a day off, 12 h: Wednesday night
        // Microsoft Project XML: the assignment's timephased data, and a
        // placeholder for the task without resources; read back the same.
        const QByteArray xml = writeMspdi(p);
        QVERIFY(xml.contains("<ResourceUID>-65535</ResourceUID>"));
        QCOMPARE(xml.count("<TimephasedData>"), 3 + 3);
        Project back;
        QVERIFY(readMspdi(xml, &back).ok);
        QCOMPARE(back.tasks.at(t).splits, p.tasks.at(t).splits);
        QCOMPARE(back.tasks.at(u).splits, p.tasks.at(u).splits);
        QCOMPARE(back.tasks.at(t).finish, p.tasks.at(t).finish);
        QCOMPARE(writeMspdi(back), xml);
        Project json;
        QVERIFY(readPlanJson(writePlanJson(p), &json).ok);
        QCOMPARE(json.tasks.at(t).splits, p.tasks.at(t).splits);
        QCOMPARE(removeSplits(p, {t, u}), 2);
        schedule(p);
        QCOMPARE_AT(p.tasks.at(t).finish, at(5, 17));
    }

    void reschedulesUncompletedWork()
    {
        Builder b;
        const int going = b.task(s("Survey"), 4 * kDay);
        const int waiting = b.task(s("Report"), 2 * kDay);
        const int later = b.task(s("Present"), kDay);
        b.constrain(waiting, ConstraintType::StartNoEarlierThan, at(3));
        b.constrain(later, ConstraintType::StartNoEarlierThan, at(10));
        Project &p = b.run();
        setPercentComplete(p, {going}, 25); // Monday done
        schedule(p);
        QCOMPARE(rescheduleUncompletedWork(p, at(4, 17)), 2);
        schedule(p);
        const Task &t = p.tasks.at(going);
        QCOMPARE(t.splits, (QList<TaskSplit>{{kDay, 2 * kDay}})); // Tue and Wed skipped
        QCOMPARE_AT(t.finish, at(9, 17));
        QCOMPARE(int(p.tasks.at(waiting).constraint), int(ConstraintType::StartNoEarlierThan));
        QCOMPARE_AT(p.tasks.at(waiting).start, at(5, 8));
        QCOMPARE_AT(p.tasks.at(later).start, at(10, 8)); // after the date already: untouched
        QCOMPARE(rescheduleUncompletedWork(p, at(4, 17)), 0); // nothing left before it
    }

    void drawsTheProgressLine()
    {
        Builder b;
        const int behind = b.task(s("Behind"), 4 * kDay);
        const int ahead = b.task(s("Ahead"), 2 * kDay);
        const int done = b.task(s("Done"), kDay);
        const int future = b.task(s("Future"), kDay);
        const int late = b.task(s("Late start"), kDay);
        b.constrain(ahead, ConstraintType::StartNoEarlierThan, at(9));
        b.constrain(future, ConstraintType::StartNoEarlierThan, at(6));
        b.constrain(late, ConstraintType::StartNoEarlierThan, at(3));
        Project &p = b.run();
        setPercentComplete(p, {behind, ahead}, 50);
        setPercentComplete(p, {done}, 100);
        schedule(p);
        const Minutes status = at(5, 17);
        const QList<ProgressPoint> line = progressLine(p, {behind, ahead, done, future, late}, status);
        QCOMPARE(line.size(), 5);
        QCOMPARE_AT(line.at(0).at, at(3, 17)); // two of four days done: behind
        QVERIFY(line.at(0).bent);
        QCOMPARE_AT(line.at(1).at, at(9, 17)); // a day done of a task that starts later: ahead
        QVERIFY(!line.at(2).bent && line.at(2).at == status);
        QVERIFY(!line.at(3).bent);
        QCOMPARE_AT(line.at(4).at, at(3, 8)); // should have started Tuesday
    }

    void actualWorkAndCost()
    {
        Builder b;
        const int t = b.task(s("Write"), 5 * kDay);
        const int k = b.task(s("Print"), 2 * kDay);
        b.p.tasks[k].fixedCost = 100000;
        const int ana = b.resource(s("Ana"), 5000);
        b.assign(t, ana);
        Project &p = b.run();
        type(p, t, "actualWork", "10h");
        schedule(p);
        QCOMPARE(p.tasks.at(t).percentComplete, 25);
        QCOMPARE(taskFieldText(p, t, s("actualWork"), kC), s("10 hrs"));
        type(p, t, "actualWork", "50h"); // more than planned: done, and the work grows
        schedule(p);
        QCOMPARE(p.tasks.at(t).percentComplete, 100);
        QCOMPARE(p.tasks.at(t).work, 50 * kHour);
        QCOMPARE_AT(p.tasks.at(t).finish, at(10, 10));
        // Actual cost is calculated until the project lets it be typed.
        setPercentComplete(p, {k}, 50);
        schedule(p);
        QVERIFY(!setTaskField(p, k, s("actualCost"), s("700"), kC).ok);
        p.info.editableActualCosts = true;
        type(p, k, "actualCost", "$700");
        schedule(p);
        QCOMPARE(taskFieldText(p, k, s("actualCost"), kC), s("$700.00"));
        QCOMPARE(taskFieldText(p, k, s("remainingCost"), kC), s("$500.00"));
        QCOMPARE(p.tasks.at(k).cost, 120000);
        Project back;
        QVERIFY(readPlanJson(writePlanJson(p), &back).ok);
        QVERIFY(back.info.editableActualCosts);
        QCOMPARE(back.tasks.at(k).actualCostInput, 70000);
        QCOMPARE(back.tasks.at(k).cost, 120000);
        type(p, k, "actualCost", "NA");
        schedule(p);
        QCOMPARE(p.tasks.at(k).cost, 100000);
    }
};

QTEST_GUILESS_MAIN(PlanTrackingM83Test)

#include "tst_plan_tracking_m83.moc"
