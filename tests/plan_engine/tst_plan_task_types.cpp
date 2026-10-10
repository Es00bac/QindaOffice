// SPDX-License-Identifier: GPL-3.0-or-later
// M83 (D-092): Project's task types and effort-driven scheduling, worked by
// hand on the standard calendar (8 h a day, Monday 2 March 2026 08:00):
// every path of Project's table — units, duration or work changed on a
// Fixed Units, Fixed Duration or Fixed Work task — resources added and
// removed with and without effort driving, work typed on a task without
// resources, one assignment's work typed apart from the others', and the
// .qplan and Microsoft Project XML that carry it.

#include "mspdi.h"
#include "plan_edit.h"
#include "plan_file.h"
#include "plan_fixture.h"
#include "resources.h"
#include "task_types.h"

using namespace PlanTest;

namespace {

const QLocale kC = QLocale::c();

struct Fixture {
    Builder b;
    int task = -1;
    int ana = 0;
    int ben = 0;

    explicit Fixture(TaskType type, Minutes duration = 4 * kDay, bool effort = false)
    {
        task = b.task(QStringLiteral("Wire the hall"), duration);
        b.p.tasks[task].type = type;
        b.p.tasks[task].effortDriven = effort || type == TaskType::FixedWork;
        ana = b.resource(QStringLiteral("Ana"), 6000);
        ben = b.resource(QStringLiteral("Ben"), 3000);
    }
    Project &p() { return b.p; }
    const Task &t() const { return b.p.tasks.at(task); }
    int assignment(int resourceUid) const { return b.p.assignmentIndex(b.p.tasks.at(task).uid, resourceUid); }
    Minutes work(int resourceUid) const
    {
        const int a = assignment(resourceUid);
        return a < 0 ? -1 : assignmentWork(t(), b.p.assignments.at(a));
    }
    int units(int resourceUid) const { return b.p.assignments.at(assignment(resourceUid)).units; }
    void assign(int resourceUid, int units = 100) { QVERIFY(assignResource(b.p, task, resourceUid, units).ok); }
    void type(const char *field, const char *text)
    {
        const EditResult r = setTaskField(b.p, task, QString::fromLatin1(field), QString::fromUtf8(text), kC);
        QVERIFY2(r.ok, qPrintable(r.error));
        schedule(b.p);
    }
};

} // namespace

class PlanTaskTypesTest final : public QObject {
    Q_OBJECT
private slots:
    void fixedUnits()
    {
        Fixture f(TaskType::FixedUnits);
        f.assign(f.ana);
        schedule(f.p());
        QCOMPARE(f.t().work, 32 * kHour);       // 4 days × 8 h × 100 %
        f.type("duration", "10d");              // duration → work recalculated
        QCOMPARE(f.t().work, 80 * kHour);
        f.type("work", "60h");                  // work → duration recalculated
        QCOMPARE(f.t().duration, Minutes(7.5 * kDay));
        QCOMPARE_AT(f.t().finish, at(11, 12));  // Mon 2 + 7.5 working days
        QVERIFY(setAssignmentUnits(f.p(), f.assignment(f.ana), 50).ok); // units → duration
        schedule(f.p());
        QCOMPARE(f.work(f.ana), 60 * kHour);
        QCOMPARE(f.t().duration, 15 * kDay);
        QCOMPARE(f.t().cost, 360000);           // 60 h at $60.00/h, in cents
    }

    void fixedDuration()
    {
        Fixture f(TaskType::FixedDuration, 5 * kDay);
        f.assign(f.ana);
        schedule(f.p());
        QCOMPARE(f.t().work, 40 * kHour);
        f.type("duration", "10d");              // duration → work recalculated
        QCOMPARE(f.t().work, 80 * kHour);
        f.type("work", "20h");                  // work → units recalculated
        QCOMPARE(f.t().duration, 10 * kDay);
        QCOMPARE(f.units(f.ana), 25);           // 20 h over 80 working hours
        QVERIFY(setAssignmentUnits(f.p(), f.assignment(f.ana), 50).ok); // units → work
        schedule(f.p());
        QCOMPARE(f.t().work, 40 * kHour);
        QCOMPARE(f.t().duration, 10 * kDay);
    }

    void fixedWork()
    {
        Fixture f(TaskType::FixedWork, 5 * kDay);
        QVERIFY(f.t().effortDriven);
        f.assign(f.ana);
        schedule(f.p());
        f.type("duration", "10d");              // duration → units recalculated
        QCOMPARE(f.t().work, 40 * kHour);
        QCOMPARE(f.units(f.ana), 50);
        QVERIFY(setAssignmentUnits(f.p(), f.assignment(f.ana), 200).ok); // units → duration
        schedule(f.p());
        QCOMPARE(f.t().duration, Minutes(2.5 * kDay));
        f.type("work", "80h");                  // work → duration
        QCOMPARE(f.t().duration, 5 * kDay);     // 80 h at 200 %
        QVERIFY(!setTaskField(f.p(), f.task, QStringLiteral("duration"), QStringLiteral("0"), kC).ok);
        QVERIFY(!setTaskField(f.p(), f.task, QStringLiteral("effortDriven"), QStringLiteral("No"), kC).ok);
    }

    void effortDrivenAddAndRemove()
    {
        // Fixed Units: two people do the same work in half the time.
        Fixture u(TaskType::FixedUnits, 4 * kDay, true);
        u.assign(u.ana);
        u.assign(u.ben);
        schedule(u.p());
        QCOMPARE(u.t().duration, 2 * kDay);
        QCOMPARE(u.work(u.ana), 16 * kHour);
        QCOMPARE(u.work(u.ben), 16 * kHour);
        QCOMPARE_AT(u.t().finish, at(3, 17));
        QVERIFY(unassignResource(u.p(), u.task, u.ben).ok); // removed: Ana takes it all back
        schedule(u.p());
        QCOMPARE(u.t().duration, 4 * kDay);
        QCOMPARE(u.work(u.ana), 32 * kHour);
        // Unequal units share by units: 48 h over 150 % = 4 days.
        Fixture w(TaskType::FixedUnits, 6 * kDay, true);
        w.assign(w.ana);
        w.assign(w.ben, 50);
        QCOMPARE(w.t().duration, 4 * kDay);
        QCOMPARE(w.work(w.ana), 32 * kHour);
        QCOMPARE(w.work(w.ben), 16 * kHour);
        // Fixed Duration: the units halve, the dates stay.
        Fixture d(TaskType::FixedDuration, 4 * kDay, true);
        d.assign(d.ana);
        d.assign(d.ben);
        QCOMPARE(d.t().duration, 4 * kDay);
        QCOMPARE(d.units(d.ana), 50);
        QCOMPARE(d.units(d.ben), 50);
        QCOMPARE(d.work(d.ben), 16 * kHour);
        // Fixed Work is always effort driven.
        Fixture fw(TaskType::FixedWork, 4 * kDay);
        fw.assign(fw.ana);
        fw.assign(fw.ben);
        QCOMPARE(fw.t().duration, 2 * kDay);
        // Not effort driven: Ben adds his own work over the same days.
        Fixture n(TaskType::FixedUnits, 4 * kDay);
        n.assign(n.ana);
        n.assign(n.ben, 50);
        schedule(n.p());
        QCOMPARE(n.t().duration, 4 * kDay);
        QCOMPARE(n.t().work, 48 * kHour);
        // Typed as Resource Names, the same rules.
        Fixture names(TaskType::FixedUnits, 4 * kDay, true);
        names.type("resources", "Ana");
        names.type("resources", "Ana, Ben");
        QCOMPARE(names.t().duration, 2 * kDay);
        names.type("resources", "Ana");
        QCOMPARE(names.t().duration, 4 * kDay);
    }

    void workTypedWithoutResources()
    {
        Fixture f(TaskType::FixedUnits, 5 * kDay, false);
        f.type("work", "24h");                  // no resource yet: Project keeps it on the task
        QCOMPARE(f.t().unassignedWork, 24 * kHour);
        QCOMPARE(f.t().work, 24 * kHour);
        QCOMPARE(f.t().duration, 5 * kDay);
        f.assign(f.ana);                        // the first resource takes it over
        schedule(f.p());
        QCOMPARE(f.t().unassignedWork, 0);
        QCOMPARE(f.work(f.ana), 24 * kHour);
        QCOMPARE(f.t().duration, 3 * kDay);
    }

    void oneAssignmentApart()
    {
        Fixture f(TaskType::FixedUnits, 4 * kDay);
        f.assign(f.ana);
        f.assign(f.ben);
        schedule(f.p());
        QVERIFY(setAssignmentWork(f.p(), f.assignment(f.ben), 16 * kHour).ok); // Ben is done in 2 days
        schedule(f.p());
        QCOMPARE(f.t().duration, 4 * kDay);                                    // Ana still takes 4
        QCOMPARE(assignmentSpan(f.t(), f.p().assignments.at(f.assignment(f.ben))), 2 * kDay);
        const DailyLoad load = dailyLoad(f.p());
        const qint64 wed = QDate(1970, 1, 1).daysTo(QDate(2026, 3, 4));
        QCOMPARE(load.value(f.ben).value(wed - 1), 480 * 100); // Tue: a full day
        QCOMPARE(load.value(f.ben).value(wed), 0);             // Wed: done
        QCOMPARE(load.value(f.ana).value(wed), 480 * 100);
        QVERIFY(setAssignmentWork(f.p(), f.assignment(f.ben), 48 * kHour).ok); // now Ben is the longest
        schedule(f.p());
        QCOMPARE(f.t().duration, 6 * kDay);
        QCOMPARE(assignmentSpan(f.t(), f.p().assignments.at(f.assignment(f.ana))), 4 * kDay);
        QCOMPARE(f.t().work, 80 * kHour);
        // A longer duration stretches both in proportion (Fixed Units: work follows).
        f.type("duration", "12d");
        QCOMPARE(f.work(f.ben), 96 * kHour);
        QCOMPARE(f.work(f.ana), 64 * kHour);
        QCOMPARE(assignmentSpan(f.t(), f.p().assignments.at(f.assignment(f.ana))), 8 * kDay);
    }

    void fieldsAsText()
    {
        Fixture f(TaskType::FixedUnits);
        f.type("type", "Fixed Duration");
        QCOMPARE(taskFieldText(f.p(), f.task, QStringLiteral("type"), kC), QStringLiteral("Fixed Duration"));
        f.type("effortDriven", "Yes");
        QCOMPARE(taskFieldText(f.p(), f.task, QStringLiteral("effortDriven"), kC), QStringLiteral("Yes"));
        f.type("type", "Fixed Work");
        QVERIFY(f.t().effortDriven);
        QVERIFY(!setTaskField(f.p(), f.task, QStringLiteral("type"), QStringLiteral("Fixed Fun"), kC).ok);
        // New tasks follow Project Options.
        f.p().info.defaultTaskType = TaskType::FixedDuration;
        f.p().info.newTasksEffortDriven = true;
        const int added = addTask(f.p(), 1, NewTaskKind::Task, QStringLiteral("New"));
        QCOMPARE(int(f.p().tasks.at(added).type), int(TaskType::FixedDuration));
        QVERIFY(f.p().tasks.at(added).effortDriven);
    }

    void qplanCarriesIt()
    {
        Fixture f(TaskType::FixedWork, 4 * kDay);
        f.assign(f.ana);
        f.assign(f.ben);
        QVERIFY(setAssignmentWork(f.p(), f.assignment(f.ben), 8 * kHour).ok);
        f.type("work", "8h"); // shared by their work: Ana 5 h 20 min, Ben 2 h 40 min
        f.p().info.defaultTaskType = TaskType::FixedWork;
        f.p().info.newTasksEffortDriven = true;
        schedule(f.p());
        Project back;
        const LoadResult r = readPlanJson(writePlanJson(f.p()), &back);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(int(back.tasks.at(f.task).type), int(TaskType::FixedWork));
        QVERIFY(back.tasks.at(f.task).effortDriven);
        QCOMPARE(int(back.info.defaultTaskType), int(TaskType::FixedWork));
        QVERIFY(back.info.newTasksEffortDriven);
        for (int k = 0; k < f.p().assignments.size(); ++k) {
            QCOMPARE(back.assignments.at(k).workInput, f.p().assignments.at(k).workInput);
            QCOMPARE(back.assignments.at(k).workSpan, f.p().assignments.at(k).workSpan);
            QCOMPARE(back.assignments.at(k).units, f.p().assignments.at(k).units);
        }
        QCOMPARE(back.tasks.at(f.task).finish, f.t().finish);
    }

    void projectXmlCarriesIt()
    {
        Fixture f(TaskType::FixedDuration, 4 * kDay, true);
        f.assign(f.ana);
        f.assign(f.ben);             // effort driven: 50 % each
        const int other = f.b.task(QStringLiteral("Paint"), 4 * kDay);
        f.p().tasks[other].type = TaskType::FixedUnits;
        f.b.assign(other, f.ana);
        f.b.assign(other, f.ben);
        schedule(f.p());
        QVERIFY(setAssignmentWork(f.p(), f.p().assignmentIndex(f.p().tasks.at(other).uid, f.ben), 16 * kHour).ok);
        schedule(f.p());
        const QByteArray xml = writeMspdi(f.p());
        QVERIFY(xml.contains("<Type>1</Type>"));
        QVERIFY(xml.contains("<EffortDriven>1</EffortDriven>"));
        QVERIFY(xml.contains("<Units>0.50</Units>"));
        Project back;
        const MspdiResult r = readMspdi(xml, &back);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(int(back.tasks.at(f.task).type), int(TaskType::FixedDuration));
        QVERIFY(back.tasks.at(f.task).effortDriven);
        const int benOther = back.assignmentIndex(back.tasks.at(other).uid, f.ben);
        QCOMPARE(back.assignments.at(benOther).workInput, 16 * kHour);
        QCOMPARE(back.assignments.at(benOther).workSpan, 2 * kDay); // read from its work and units
        QCOMPARE(back.tasks.at(other).finish, f.p().tasks.at(other).finish);
        QCOMPARE(writeMspdi(back), xml);                            // and written the same again
    }
};

QTEST_GUILESS_MAIN(PlanTaskTypesTest)

#include "tst_plan_task_types.moc"
