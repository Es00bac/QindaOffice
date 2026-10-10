// SPDX-License-Identifier: GPL-3.0-or-later
// M83 (D-092) through PlanHost and PlanService over the seeded gala: the
// Task Type and Effort Driven commands with an assignment's work typed apart,
// custom fields renamed with a lookup that restricts and a formula, their
// table, filter and sort, Split Task and Remove Splits, a recurring task
// inserted and its pattern changed, Reschedule Uncompleted Work, the
// Excel/CSV import, recurring calendar exceptions and work weeks made in
// Change Working Time, and the Gantt printed with progress lines. Every
// change is one undo step.

#include "plan_app_test_support.h"

#include "plan_document.h"
#include "plan_edit.h"
#include "task_types.h"

#include <QUndoStack>

using namespace PlanAppTest;

namespace {

struct Loaded {
    QTemporaryDir dir;
    PlanHost host;
    PlanService *plan = host.plan();
    bool loaded = false;
    Loaded()
    {
        plan->setToday(kToday);
        loaded = host.loadPath(writeGala(dir));
    }
    QString id(const char *name) const
    {
        const Engine::Project &p = plan->project();
        for (int i = 0; i < p.tasks.size(); ++i) {
            if (p.tasks.at(i).name == s(name)) {
                return plan->idOfRow(i);
            }
        }
        return {};
    }
    const Engine::Task &task(const char *name) const { return plan->project().tasks.at(plan->rowOf(id(name))); }
    void select(const char *name)
    {
        plan->setSelectedTaskIds({id(name)});
        plan->setCurrentTaskId(id(name));
    }
    bool trigger(const char *command) { return host.actions()->trigger(s(command)); }
    QUndoStack *undo() const { return host.planDocument()->undoStack(); }
};

} // namespace

class PlanM83Test final : public QObject {
    Q_OBJECT
private slots:
    void commandsExist()
    {
        Loaded l;
        QVERIFY(l.loaded);
        for (const char *command : {"planTaskTypeFixedUnitsAction", "planTaskTypeFixedDurationAction",
                                    "planTaskTypeFixedWorkAction", "planEffortDrivenAction", "planSplitTaskAction",
                                    "planRemoveSplitsAction", "planInsertRecurringAction", "planRecurringInfoAction",
                                    "planCustomFieldsAction", "planRescheduleWorkAction", "planTableCustomAction",
                                    "planFilterCustomAction", "planSortCustomAction", "planShowProgressLinesAction",
                                    "planImportTasksAction"}) {
            QVERIFY2(l.host.actions()->entry(s(command)) != nullptr, command);
        }
    }

    void taskTypesThroughTheCommands()
    {
        Loaded l;
        QVERIFY(l.loaded);
        l.select("Floor plan and seating");
        QVERIFY(l.trigger("planTaskTypeFixedDurationAction"));
        QCOMPARE(int(l.task("Floor plan and seating").type), int(Engine::TaskType::FixedDuration));
        QCOMPARE(l.plan->currentTask().value(s("type")).toString(), s("duration"));
        QVERIFY(l.host.actions()->entry(s("planTaskTypeFixedDurationAction"))->isChecked());
        // Effort driven: a second person halves Recruit volunteers (10 days).
        l.select("Recruit volunteers");
        QVERIFY(l.trigger("planEffortDrivenAction"));
        QVERIFY(l.task("Recruit volunteers").effortDriven);
        const int dana = l.plan->project().resourceIndexByName(s("Dana Whitfield"));
        QVERIFY(ok(l.plan->assign(l.plan->project().resources.at(dana).uid, s("100%"))));
        QCOMPARE(l.task("Recruit volunteers").duration, 5 * kDay);
        QCOMPARE(l.task("Recruit volunteers").work, 80 * 60);
        l.undo()->undo();
        QCOMPARE(l.task("Recruit volunteers").duration, 10 * kDay);
        // One person's work typed apart: Sam is done by lunch on setup day.
        l.select("Venue setup");
        const int sam = l.plan->project().resources.at(l.plan->project().resourceIndexByName(s("Sam Chen"))).uid;
        QVERIFY(ok(l.plan->setAssignmentField(l.id("Venue setup"), sam, s("work"), s("4h"))));
        QCOMPARE(l.task("Venue setup").duration, kDay);
        QCOMPARE(l.task("Venue setup").work, 20 * 60);
        bool found = false;
        for (const QVariant &v : l.plan->assignmentDetails(l.id("Venue setup"))) {
            if (v.toMap().value(s("resourceUid")).toInt() == sam) {
                QCOMPARE(v.toMap().value(s("work")).toString(), s("4 hrs"));
                found = true;
            }
        }
        QVERIFY(found);
        QVERIFY(!ok(l.plan->setAssignmentField(l.id("Venue setup"), sam, s("work"), s("soon"))));
    }

    void customFields()
    {
        Loaded l;
        QVERIFY(l.loaded);
        QVariantList lookup{QVariantMap{{s("value"), s("Dana")}}, QVariantMap{{s("value"), s("Priya")}, {s("description"), s("Development")}}};
        QVERIFY(ok(l.plan->saveCustomField(s("Text1"), {{s("alias"), s("Owner")}, {s("lookup"), lookup}, {s("restrict"), true}})));
        QVERIFY(ok(l.plan->saveCustomField(s("Number1"), {{s("formula"), s("[Duration]/480")}})));
        QCOMPARE(l.plan->customFieldDefinition(s("Text1")).value(s("lookup")).toList().size(), 2);
        QVERIFY(l.trigger("planTableCustomAction"));
        QCOMPARE(l.plan->table(), s("custom"));
        QCOMPARE(l.plan->tableFields(), QStringList({s("indicators"), s("name"), s("Text1"), s("Number1")}));
        QCOMPARE(l.plan->sheetColumns().at(2).toMap().value(s("title")).toString(), s("Owner"));
        QVERIFY(ok(l.plan->setTaskField(l.id("Book entertainment"), s("Text1"), s("Priya"))));
        QVERIFY(!ok(l.plan->setTaskField(l.id("Book entertainment"), s("Text1"), s("Bob"))));
        QCOMPARE(l.plan->taskFieldText(l.id("Sponsor outreach"), s("Number1")), s("39"));
        QVERIFY(ok(l.plan->applyCustomFilter(s("Text1"), s("equals"), s("priya"))));
        QCOMPARE(l.plan->visibleRowCount(), 2); // the task and its summary
        QCOMPARE(l.plan->filterTitle(), s("Custom Field: Owner"));
        QVERIFY(ok(l.plan->applyFilter(s("all"))));
        QVERIFY(ok(l.plan->setCustomSort(s("Number1"), true)));
        QCOMPARE(l.plan->sortKey(), s("custom:Number1:desc"));
        QVERIFY(!ok(l.plan->applyCustomFilter(s("Number1"), s("greater"), s("lots"))));
        // Task Information's Custom Fields page saves with the dialog.
        QVERIFY(ok(l.plan->saveTaskInfo(l.id("Rehearsal"), {{s("Text1"), s("Dana")}})));
        QCOMPARE(l.plan->taskFieldText(l.id("Rehearsal"), s("Text1")), s("Dana"));
        QCOMPARE(l.plan->taskCustomValues(l.id("Rehearsal")).size(), 2);
    }

    void splitsRecurringAndReschedule()
    {
        Loaded l;
        QVERIFY(l.loaded);
        // Split Task, then Remove Splits.
        l.select("Floor plan and seating");
        const Engine::Minutes finish = l.task("Floor plan and seating").finish;
        const QDate start = Engine::dateOf(l.task("Floor plan and seating").start);
        QVERIFY(ok(l.plan->splitCurrentTask(start.toString(Qt::ISODate), start.addDays(7).toString(Qt::ISODate))));
        QCOMPARE(l.task("Floor plan and seating").splits.size(), 1);
        QVERIFY(l.task("Floor plan and seating").finish > finish);
        QVERIFY(l.plan->taskFieldText(l.id("Floor plan and seating"), s("indicators")).contains(s("split")));
        QVERIFY(l.trigger("planRemoveSplitsAction"));
        QCOMPARE(l.task("Floor plan and seating").finish, finish);
        // A recurring task, inserted at the end, then every other occurrence.
        l.plan->setSelectedTaskIds({});
        const QVariantMap fields{{s("name"), s("Weekly check-in")}, {s("duration"), s("1h")}, {s("frequency"), s("weekly")},
                                 {s("every"), 1}, {s("days"), 1}, {s("from"), s("2026-03-30")}, {s("count"), 4},
                                 {s("startTime"), s("09:00")}};
        QVERIFY(ok(l.plan->saveRecurringTask(QString(), fields)));
        QVERIFY(l.task("Weekly check-in").recurring);
        QCOMPARE(l.task("Weekly check-in 4").constraintDate, Engine::toMinutes(QDate(2026, 4, 20), 9 * 60));
        QCOMPARE(l.plan->recurringTaskFields(l.id("Weekly check-in")).value(s("count")).toInt(), 4);
        QVariantMap fewer = fields;
        fewer.insert(s("count"), 2);
        QVERIFY(ok(l.plan->saveRecurringTask(l.id("Weekly check-in"), fewer)));
        QVERIFY(l.id("Weekly check-in 3").isEmpty());
        l.select("Weekly check-in");
        QVERIFY(l.host.actions()->entry(s("planRecurringInfoAction"))->isEnabled());
        // Reschedule Uncompleted Work after Friday 3 April: outreach (30 %) splits.
        QVERIFY(l.task("Sponsor outreach").splits.isEmpty());
        const QVariantMap r = l.plan->rescheduleUncompletedWork(s("2026-04-03"));
        QVERIFY2(ok(r), qPrintable(r.value(s("error")).toString()));
        QVERIFY(r.value(s("tasks")).toInt() >= 1);
        QCOMPARE(l.task("Sponsor outreach").splits.size(), 1);
        l.undo()->undo();
        QVERIFY(l.task("Sponsor outreach").splits.isEmpty());
    }

    void importAndWorkingTime()
    {
        Loaded l;
        QVERIFY(l.loaded);
        const QString csv = l.dir.filePath(s("extra.csv"));
        QFile f(csv);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("Task Name,Duration,Predecessors\nCaterer contract,2d,\nTable linens,1d,1\n");
        f.close();
        const QVariantMap preview = l.plan->importPreview(QUrl::fromLocalFile(csv));
        QVERIFY(ok(preview));
        QCOMPARE(preview.value(s("mapping")).toStringList(), QStringList({s("name"), s("duration"), s("predecessors")}));
        const int before = l.plan->taskCount();
        const QVariantMap imported = l.plan->importTasks(QUrl::fromLocalFile(csv), preview.value(s("mapping")).toStringList(), true);
        QVERIFY(ok(imported));
        QCOMPARE(imported.value(s("tasks")).toInt(), 2);
        QCOMPARE(l.plan->taskCount(), before + 2);
        QCOMPARE(l.plan->taskFieldText(l.id("Table linens"), s("predecessors")), QString::number(l.task("Caterer contract").id));
        l.undo()->undo();
        QCOMPARE(l.plan->taskCount(), before);
        // A yearly holiday and a short work week, made here.
        const int cal = l.plan->project().info.calendarUid;
        QVERIFY(ok(l.plan->saveRecurringException(cal, -1, {{s("frequency"), s("yearly")}, {s("name"), s("Founders' day")},
                                                             {s("day"), 15}, {s("month"), 4}, {s("from"), s("2026-01-01")},
                                                             {s("to"), s("2030-12-31")}, {s("hours"), QString()}})));
        QCOMPARE(l.plan->recurringExceptions(cal).size(), 1);
        QVERIFY(l.plan->recurringExceptions(cal).first().toMap().value(s("text")).toString().contains(s("April")));
        QVERIFY(ok(l.plan->saveWorkWeek(cal, -1, {{s("name"), s("Gala week")}, {s("from"), s("2026-05-11")}, {s("to"), s("2026-05-15")},
                                                  {s("days"), QStringList({s("default"), s("default"), s("default"), s("default"),
                                                                           s("08:00–12:00"), s("default"), s("default")})}})));
        QCOMPARE(l.plan->workWeeks(cal).size(), 1);
        QCOMPARE(l.plan->workWeeks(cal).first().toMap().value(s("days")).toStringList().at(4), s("08:00–12:00"));
        QVERIFY(ok(l.plan->deleteWorkWeek(cal, 0)));
        QVERIFY(ok(l.plan->deleteRecurringException(cal, 0)));
        QCOMPARE(l.plan->recurringExceptions(cal).size(), 0);
        QVERIFY(!ok(l.plan->saveWorkWeek(cal, -1, {{s("name"), s("Backwards")}, {s("from"), s("2026-05-15")}, {s("to"), s("2026-05-11")}})));
    }

    void progressLinesPrint()
    {
        Loaded l;
        QVERIFY(l.loaded);
        QVERIFY(l.trigger("planShowProgressLinesAction"));
        QVERIFY(l.plan->showProgressLines());
        l.select("Floor plan and seating");
        const QDate start = Engine::dateOf(l.task("Floor plan and seating").start);
        QVERIFY(ok(l.plan->splitCurrentTask(start.toString(Qt::ISODate), start.addDays(7).toString(Qt::ISODate))));
        const QString grabs = qEnvironmentVariable("QQO_GRAB_DIR");
        const QString path = grabs.isEmpty() ? l.dir.filePath(s("gantt.pdf")) : grabs + s("/qindaplan_m83_gantt.pdf");
        QVERIFY(ok(l.host.exportPdfTo(QUrl::fromLocalFile(path))));
        QVERIFY(QFileInfo(path).size() > 1000);
    }
};

PLAN_APP_TEST_MAIN(PlanM83Test)

#include "tst_qindaplan_m83.moc"
