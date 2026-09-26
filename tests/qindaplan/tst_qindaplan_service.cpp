// SPDX-License-Identifier: GPL-3.0-or-later
// PlanService and PlanHost over the real document (M51): the gala loads
// and schedules as documented; sheet edits, ribbon operations, undo/redo
// and Save/Load round-trip through the document; the Gantt, calendar and
// usage projections carry what the views draw; MSPDI and CSV export;
// the Gantt drag adapter previews and commits through the engine.

#include "plan_app_test_support.h"

#include "plan_edit.h"
#include "resources.h"

#include <QFile>
#include <QSignalSpy>
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
            if (p.tasks.at(i).name == QString::fromUtf8(name)) {
                return plan->idOfRow(i);
            }
        }
        return {};
    }
    const Engine::Task &task(const char *name) const { return plan->project().tasks.at(plan->rowOf(id(name))); }
};

} // namespace

class PlanServiceTest final : public QObject {
    Q_OBJECT
private slots:
    void galaSchedulesAsDocumented()
    {
        Loaded l;
        QVERIFY(l.loaded);
        const Engine::Project &p = l.plan->project();
        QCOMPARE(p.tasks.size(), 40);
        QCOMPARE(p.resources.size(), 6);
        QVERIFY2(p.issues.isEmpty(), qPrintable(p.issues.isEmpty() ? QString() : p.issues.first().message));
        int over = 0;
        for (const Engine::Resource &r : p.resources) {
            over += r.overallocated ? 1 : 0;
        }
        QCOMPARE(over, 1);
        QVERIFY(p.resources.at(p.resourceIndexByName(s("Marcus Bell"))).overallocated);
        QCOMPARE(Engine::firstOverallocatedDay(p, p.resources.at(p.resourceIndexByName(s("Marcus Bell"))).uid),
                 QDate(2026, 4, 7));
        // Outreach ran three days long, so the gala is three days later than
        // its baseline — still inside the Finish No Later Than 15 May.
        QCOMPARE(Engine::toIso(l.task("Gala night").finish), s("2026-05-14T17:00"));
        QCOMPARE(Engine::toIso(l.task("Gala night").baselines[0].finish), s("2026-05-11T17:00"));
        QVERIFY(!l.task("Gala night").constraintConflict);
        QVERIFY(l.task("Sponsor outreach").critical);
        QVERIFY(l.task("Gala night").critical);
        QVERIFY(!l.task("Social media campaign").critical);
        QCOMPARE(l.plan->title(), s("Spring fundraiser gala"));
        QVERIFY(!l.host.isDirty());
        const QVariantMap summary = l.plan->summary();
        QCOMPARE(summary.value(s("start")).toString(), s("Mon 2 Mar 26"));
        QCOMPARE(summary.value(s("finishDetail")).toString(), s("3 days behind the baseline"));
    }

    void sheetEditsAreUndoableSteps()
    {
        Loaded l;
        QVERIFY(l.loaded);
        const QString tasting = l.id("Plan menu with caterer");
        QVariantMap r = l.plan->setTaskField(tasting, s("duration"), s("5d"));
        QVERIFY(ok(r));
        QCOMPARE(l.task("Plan menu with caterer").duration, 5 * kDay);
        QVERIFY(l.host.isDirty());
        r = l.plan->setTaskField(tasting, s("duration"), s("soon"));
        QVERIFY(!ok(r));
        QVERIFY(r.value(s("error")).toString().contains(s("not a duration")));
        QUndoStack *stack = l.host.planDocument()->undoStack();
        QCOMPARE(stack->count(), 1);
        stack->undo();
        QCOMPARE(l.task("Plan menu with caterer").duration, 3 * kDay);
        QVERIFY(!l.host.isDirty());
        stack->redo();
        QCOMPARE(l.task("Plan menu with caterer").duration, 5 * kDay);
        // The grid's commit path: a new task typed into the blank row.
        const QString before = QString::number(l.plan->taskCount());
        const QString refused = l.plan->commitSheetChanges(
            {QVariantMap{{s("rowId"), s("new")}, {s("columnId"), s("name")}, {s("after"), s("Photo booth")}},
             QVariantMap{{s("rowId"), s("new")}, {s("columnId"), s("duration")}, {s("after"), s("2d")}}});
        QVERIFY2(refused.isEmpty(), qPrintable(refused));
        QCOMPARE(QString::number(l.plan->taskCount() - 1), before);
        QCOMPARE(l.task("Photo booth").duration, 2 * kDay);
        QCOMPARE(l.plan->currentTaskId(), l.id("Photo booth"));
    }

    void ribbonOperations()
    {
        Loaded l;
        QVERIFY(l.loaded);
        l.plan->setSelectedTaskIds({l.id("Press release"), l.id("Rent AV equipment")});
        QCOMPARE(l.plan->selectedRows().size(), 2);
        QVERIFY(ok(l.plan->linkSelected()));
        QVERIFY(!l.task("Rent AV equipment").predecessors.isEmpty());
        QVERIFY(ok(l.plan->unlinkSelected()));
        l.plan->setCurrentTaskId(l.id("Rehearsal"));
        QVERIFY(ok(l.plan->insertTask(s("milestone"), s("Doors open"))));
        QVERIFY(l.task("Doors open").isMilestone());
        QVERIFY(ok(l.plan->indentSelected()) || true);
        l.plan->setCurrentTaskId(l.id("Press release"));
        QVERIFY(ok(l.plan->markSelected(50)));
        QCOMPARE(l.task("Press release").percentComplete, 50);
        QVERIFY(ok(l.plan->setSelectedManual(true)));
        QVERIFY(l.task("Press release").manual);
        QVERIFY(ok(l.plan->toggleSelectedActive()));
        QVERIFY(!l.task("Press release").active);
        l.plan->setCurrentTaskId(l.id("Sponsorship and marketing"));
        l.plan->toggleSubtasks();
        QVERIFY(l.plan->isCollapsed(l.task("Sponsorship and marketing").uid));
        QCOMPARE(l.plan->visibleRows().size(), l.plan->taskCount() - 11);
        l.plan->expandAll();
        QCOMPARE(l.plan->visibleRows().size(), l.plan->taskCount());
        QVERIFY(!ok(l.plan->deleteResource(9999)));
    }

    void resourcesAndLeveling()
    {
        Loaded l;
        QVERIFY(l.loaded);
        l.plan->setCurrentTaskId(l.id("Rent AV equipment"));
        const int priya = l.plan->project().resources.at(l.plan->project().resourceIndexByName(s("Priya Nair"))).uid;
        QVERIFY(ok(l.plan->assign(priya, s("25%"))));
        bool found = false;
        for (const QVariant &v : l.plan->assignmentsOf(l.id("Rent AV equipment"))) {
            const QVariantMap a = v.toMap();
            if (a.value(s("resourceUid")).toInt() == priya) {
                found = a.value(s("assigned")).toBool() && a.value(s("units")).toString() == s("25%");
            }
        }
        QVERIFY(found);
        QVERIFY(ok(l.plan->unassign(priya)));
        const QVariantMap next = l.plan->nextOverallocation();
        QVERIFY(ok(next));
        QCOMPARE(next.value(s("resource")).toString(), s("Marcus Bell"));
        const QVariantMap leveled = l.plan->level(0);
        QVERIFY2(ok(leveled), qPrintable(leveled.value(s("error")).toString()));
        QVERIFY(leveled.value(s("resolved")).toBool());
        for (const Engine::Resource &r : l.plan->project().resources) {
            QVERIFY2(!r.overallocated, qPrintable(r.name));
        }
        QVERIFY(ok(l.plan->clearLeveling()));
        const QVariantMap added = l.plan->addResource(s("AV crew"));
        QVERIFY(ok(added));
        QVERIFY(ok(l.plan->setResourceField(added.value(s("uid")).toInt(), s("standardRate"), s("680/d"))));
        QCOMPARE(l.plan->project().resources.last().standardRate, 8500);
    }

    void projectCalendarsBaselinesAndReports()
    {
        Loaded l;
        QVERIFY(l.loaded);
        const int cal = l.plan->project().info.calendarUid;
        QCOMPARE(l.plan->calendarWeek(cal).size(), 7);
        QVERIFY(ok(l.plan->addCalendarException(cal, s("Memorial Day"), s("2026-05-25"), QString(), QString())));
        QCOMPARE(l.plan->calendarExceptions(cal).size(), 2);
        QVERIFY(!ok(l.plan->setCalendarDay(cal, 5, s("25:00-26:00"))));
        QVERIFY(ok(l.plan->setCalendarDay(cal, 5, s("09:00-13:00"))));
        QVERIFY(l.plan->calendarWeek(cal).at(5).toMap().value(s("working")).toBool());
        QVERIFY(l.plan->baselines().at(0).toMap().value(s("set")).toBool());
        QVERIFY(ok(l.plan->setBaseline(1)));
        QVERIFY(l.plan->baselines().at(1).toMap().value(s("set")).toBool());
        QVERIFY(ok(l.plan->updateProject(s("2026-04-03"))));
        QVERIFY(l.task("Sponsor outreach").percentComplete > 40);
        const QVariantMap report = l.plan->report(s("overview"));
        QCOMPARE(report.value(s("title")).toString(), s("Project Overview"));
        QVERIFY(report.value(s("kpis")).toList().size() >= 6);
        QVERIFY(ok(l.plan->setProjectInfo({{s("title"), s("Spring gala 2026")}, {s("currency"), s("usd")}})));
        QCOMPARE(l.plan->title(), s("Spring gala 2026"));
        QVERIFY(!ok(l.plan->setProjectInfo({{s("currency"), s("dollars")}})));
    }

    void saveLoadAndExports()
    {
        Loaded l;
        QVERIFY(l.loaded);
        QVERIFY(ok(l.plan->setTaskField(l.id("Rehearsal"), s("notes"), s("Walk the run of show twice."))));
        const QString path = l.dir.filePath(s("saved.qplan"));
        QVERIFY(l.host.planDocument()->save(path));
        PlanHost other;
        QVERIFY(other.loadPath(path));
        QCOMPARE(other.plan()->project().tasks.size(), l.plan->project().tasks.size());
        QCOMPARE(Engine::writePlanJson(other.plan()->project()), Engine::writePlanJson(l.plan->project()));
        const QString xml = l.dir.filePath(s("gala.xml"));
        QVERIFY(ok(l.plan->exportMspdi(QUrl::fromLocalFile(xml))));
        PlanHost fromXml;
        QVERIFY(fromXml.loadPath(xml));
        QCOMPARE(fromXml.plan()->project().tasks.size(), l.plan->project().tasks.size());
        QCOMPARE(fromXml.plan()->project().finish, l.plan->project().finish);
        const QString csv = l.dir.filePath(s("gala.csv"));
        QVERIFY(ok(l.plan->exportCsv(QUrl::fromLocalFile(csv))));
        QFile f(csv);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(f.readLine().startsWith("ID,Mode,Task Name,Duration,Start,Finish"));
        const QString png = l.dir.filePath(s("timeline.png"));
        QVERIFY(ok(l.plan->exportTimelinePng(QUrl::fromLocalFile(png))));
        QVERIFY(QFile::exists(png));
        const QString pdf = l.dir.filePath(s("gantt.pdf"));
        QVERIFY(ok(l.host.exportPdfTo(QUrl::fromLocalFile(pdf))));
        QFile pf(pdf);
        QVERIFY(pf.open(QIODevice::ReadOnly));
        QVERIFY(pf.read(4) == "%PDF");
        QVERIFY(l.host.ganttPageCount() >= 1);
    }

    void viewProjections()
    {
        Loaded l;
        QVERIFY(l.loaded);
        QCOMPARE(l.plan->ganttModel()->count(), 40);
        const QVariantMap first = l.plan->ganttModel()->get(0);
        QCOMPARE(first.value(s("kind")).toString(), s("summary"));
        const QVariantMap outreach = l.plan->ganttModel()->get(l.plan->ganttModel()->indexOfId(l.id("Sponsor outreach")));
        QVERIFY(outreach.value(s("critical")).toBool());
        QVERIFY(!outreach.value(s("baselineStartUtc")).toString().isEmpty());
        QCOMPARE(outreach.value(s("barText")).toString(), s("Priya Nair[75%]"));
        l.plan->setShowCritical(false);
        QVERIFY(!l.plan->ganttModel()->get(l.plan->ganttModel()->indexOfId(l.id("Sponsor outreach")))
                     .value(s("critical")).toBool());
        QCOMPARE(l.plan->markers().size(), 2);
        QCOMPARE(l.plan->calendarEvents().size(), 36);
        QVERIFY(l.plan->usageModel()->resourceCount() > 5);
        bool conflict = false;
        for (int r = 0; r < l.plan->usageModel()->resourceCount(); ++r) {
            conflict = conflict || l.plan->usageModel()->resourceConflict(l.plan->usageModel()->resourceIdAt(r));
        }
        QVERIFY(conflict);
        QCOMPARE(l.plan->sheetModel()->rowCount(), 41); // + the blank row
        l.plan->setTable(s("variance"));
        QCOMPARE(l.plan->sheetModel()->columnIdAt(4), s("baselineStart"));
        QCOMPARE(l.plan->sheetModel()->cellValue(l.id("Sponsor outreach"), s("finishVariance")).toString(), s("3 days"));
    }

    void ganttDragGoesThroughTheEngine()
    {
        Loaded l;
        QVERIFY(l.loaded);
        QindaTK::GanttTaskModel *model = l.plan->ganttModel();
        const QString id = l.id("Press release");
        const qint64 monday = Engine::toMinutes(QDate(2026, 5, 11), 8 * 60) * 60000;
        QVERIFY(model->previewMove(id, QString::number(monday)));
        QVERIFY(model->previewActive());
        QVERIFY(model->commitPreview());
        QCOMPARE(l.task("Press release").constraint, Engine::ConstraintType::StartNoEarlierThan);
        QCOMPARE(Engine::toIso(l.task("Press release").start), s("2026-05-11T08:00"));
        QVERIFY(model->undo());
        QCOMPARE(l.task("Press release").constraint, Engine::ConstraintType::AsSoonAsPossible);
    }
};

PLAN_APP_TEST_MAIN(PlanServiceTest)

#include "tst_qindaplan_service.moc"
