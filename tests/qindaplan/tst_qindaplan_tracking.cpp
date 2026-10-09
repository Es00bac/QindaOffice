// SPDX-License-Identifier: GPL-3.0-or-later
// M79 (D-088) through PlanHost and PlanService over the seeded gala:
// Project's filters and sorts shape every view (sheet, Gantt, Network
// Diagram, Task Usage, printout); the Tracking and Leveling Gantt styles
// with their own tables; baselines 0–10 set and cleared for the selection;
// Mark on Track and Update Tasks as single undo steps; the leveling options,
// Level Selection and Clear Leveling of the selection, saved to Microsoft
// Project XML with Project's LevelingDelay and read back; the Cost and Work
// Overview dashboards; PDFs of the Network Diagram and the Tracking Gantt.

#include "plan_app_test_support.h"

#include "mspdi.h"
#include "outline.h"

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
    QStringList visibleNames() const
    {
        QStringList out;
        for (int i : plan->visibleRows()) {
            out.append(plan->project().tasks.at(i).name);
        }
        return out;
    }
    QVariantMap ganttRow(const char *name) const
    {
        for (const QVariant &v : plan->ganttRows(plan->project())) {
            if (v.toMap().value(s("taskId")).toString() == id(name)) {
                return v.toMap();
            }
        }
        return {};
    }
    bool checked(const char *action) { return host.actions()->entry(s(action))->isChecked(); }
};

int stackCount(Loaded &l)
{
    return l.host.planDocument()->undoStack()->count();
}

} // namespace

class PlanTrackingTest final : public QObject {
    Q_OBJECT
private slots:
    void filtersShapeEveryView()
    {
        Loaded l;
        QVERIFY(l.loaded);
        QCOMPARE(l.plan->visibleRows().size(), 40);
        QVERIFY(l.host.actions()->trigger(s("planFilterMilestonesAction")));
        QCOMPARE(l.plan->filter(), s("milestones"));
        QCOMPARE(l.visibleNames(), (QStringList{s("Planning"), s("Venue and date confirmed"), s("Sponsorship and marketing"),
                                                s("Sponsors confirmed"), s("Invitations out"), s("Program and logistics"),
                                                s("Logistics locked"), s("Gala and wrap-up"), s("Gala night"),
                                                s("Project closed")}));
        // Every view shows exactly these rows.
        QCOMPARE(l.plan->ganttModel()->count(), 10);
        QCOMPARE(l.plan->sheetModel()->rows(), 11); // + the blank "new" row
        QCOMPARE(l.plan->networkLayout().nodes.size(), 10);
        QCOMPARE(l.plan->calendarEvents().size(), 6); // milestones; summaries are not events
        QVERIFY(l.checked("planFilterMilestonesAction"));
        // Without related summary rows.
        QVERIFY(l.host.actions()->trigger(s("planFilterRelatedSummariesAction")));
        QVERIFY(!l.plan->relatedSummaries());
        QCOMPARE(l.plan->visibleRows().size(), 6);
        QVERIFY(l.host.actions()->trigger(s("planFilterRelatedSummariesAction")));
        // Using Resource… asks first: the check stays on the current filter.
        QSignalSpy asked(&l.host, &PlanHost::filterRequested);
        QVERIFY(l.host.actions()->trigger(s("planFilterResourceAction")));
        QCOMPARE(asked.count(), 1);
        QCOMPARE(asked.first().first().toString(), s("resource"));
        QVERIFY(l.checked("planFilterMilestonesAction"));
        const int marcus = l.plan->project().resources.at(l.plan->project().resourceIndexByName(s("Marcus Bell"))).uid;
        QVERIFY(ok(l.plan->applyFilter(s("resource"), {{s("resourceUid"), marcus}})));
        QCOMPARE(l.visibleNames(), (QStringList{s("Planning"), s("Book the venue"), s("Program and logistics"),
                                                s("Book entertainment"), s("Plan menu with caterer"), s("Rent AV equipment"),
                                                s("Final headcount to caterer"), s("Gala and wrap-up"), s("Venue setup")}));
        QVERIFY(l.checked("planFilterResourceAction"));
        QVERIFY(!ok(l.plan->applyFilter(s("resource"), {{s("resourceUid"), 999}})));
        QVERIFY(!ok(l.plan->applyFilter(s("dateRange"), {{s("from"), s("2026-04-10")}, {s("to"), s("2026-04-01")}})));
        // Working in the week of 6 April.
        QVERIFY(ok(l.plan->applyFilter(s("dateRange"), {{s("from"), s("2026-04-06")}, {s("to"), s("2026-04-10")}})));
        for (int i : l.plan->visibleRows()) {
            const Engine::Task &t = l.plan->project().tasks.at(i);
            QVERIFY2(t.start < Engine::toMinutes(QDate(2026, 4, 11)) && t.finish >= Engine::toMinutes(QDate(2026, 4, 6)),
                     qPrintable(t.name));
        }
        // Critical: the critical tasks and their summaries, nothing else.
        QVERIFY(ok(l.plan->applyFilter(s("critical"))));
        for (int i : l.plan->visibleRows()) {
            const Engine::Task &t = l.plan->project().tasks.at(i);
            QVERIFY2(t.critical || t.summary, qPrintable(t.name));
        }
        QVERIFY(l.visibleNames().contains(s("Gala night")));
        QVERIFY(!l.visibleNames().contains(s("Social media campaign")));
        QVERIFY(ok(l.plan->applyFilter(s("all"))));
        QCOMPARE(l.plan->visibleRows().size(), 40);
        QVERIFY(!l.host.isDirty()); // filters are view state
    }

    void sortsKeepTheOutline()
    {
        Loaded l;
        QVERIFY(l.loaded);
        QVERIFY(l.host.actions()->trigger(s("planSortFinishAction")));
        QCOMPARE(l.plan->sortKey(), s("finish"));
        const Engine::Project &p = l.plan->project();
        const QList<int> rows = l.plan->visibleRows();
        QCOMPARE(rows.size(), 40);
        Engine::Minutes lastTop = Engine::kNoDate;
        for (int k = 0; k < rows.size(); ++k) {
            const Engine::Task &t = p.tasks.at(rows.at(k));
            if (t.parentIndex >= 0) {
                QVERIFY2(rows.indexOf(t.parentIndex) < k, qPrintable(t.name)); // under its summary
            } else {
                QVERIFY2(t.finish >= lastTop, qPrintable(t.name));
                lastTop = t.finish;
            }
        }
        // The sheet shows the same order.
        QCOMPARE(l.plan->sheetModel()->rowIdAt(1), l.plan->idOfRow(rows.at(1)));
        QVERIFY(l.host.actions()->trigger(s("planSortIdAction")));
        QCOMPARE(l.plan->visibleRows().first(), 0);
    }

    void ganttViewsAndTheirTables()
    {
        Loaded l;
        QVERIFY(l.loaded);
        l.host.setCurrentView(s("tracking"));
        QCOMPARE(l.plan->ganttStyle(), s("tracking"));
        QCOMPARE(l.plan->table(), s("tracking"));
        QVERIFY(l.host.ribbonContexts().contains(s("ganttFormat")));
        QCOMPARE(l.ganttRow("Sponsor outreach").value(s("barText")).toString(), s("30%"));
        QVERIFY(l.ganttRow("Sponsor outreach").contains(s("baselineStartUtc")));
        // Baseline 1 is not set: no lower bars; then it is.
        QVERIFY(l.host.actions()->trigger(s("planBaselineShown1Action")));
        QCOMPARE(l.plan->ganttBaseline(), 1);
        QVERIFY(!l.ganttRow("Sponsor outreach").contains(s("baselineStartUtc")));
        QVERIFY(ok(l.plan->setBaseline(1)));
        QVERIFY(l.ganttRow("Sponsor outreach").contains(s("baselineStartUtc")));
        QVERIFY(l.checked("planBaselineShown1Action"));
        l.host.setCurrentView(s("leveling"));
        QCOMPARE(l.plan->table(), s("delay"));
        QVERIFY(ok(l.plan->level(0)));
        bool delayed = false;
        for (const Engine::Task &t : l.plan->project().tasks) {
            if (t.levelingDelay > 0) {
                delayed = true;
                const QVariantMap row = l.ganttRow(t.name.toUtf8().constData());
                QCOMPARE(row.value(s("baselineStartUtc")).toString(), PlanService::utcOf(t.preLeveledStart));
                QVERIFY(row.value(s("barText")).toString().endsWith(s("edays"))
                        || row.value(s("barText")).toString().endsWith(s("eday")));
            }
        }
        QVERIFY(delayed);
        // Each view keeps its table: Cost on the Gantt survives a visit elsewhere.
        l.host.setCurrentView(s("gantt"));
        QCOMPARE(l.plan->table(), s("entry"));
        QCOMPARE(l.plan->ganttStyle(), s("gantt"));
        QVERIFY(l.host.actions()->trigger(s("planTableCostAction")));
        l.host.setCurrentView(s("tracking"));
        QCOMPARE(l.plan->table(), s("tracking"));
        l.host.setCurrentView(s("network"));
        QVERIFY(!l.host.ribbonContexts().contains(s("ganttFormat")));
        l.host.setCurrentView(s("gantt"));
        QCOMPARE(l.plan->table(), s("cost"));
        // Slack after the bars: a non-critical task says how much it has.
        QVERIFY(l.host.actions()->trigger(s("planShowSlackAction")));
        QVERIFY(l.ganttRow("Social media campaign").value(s("barText")).toString().contains(s("slack")));
        QVERIFY(!l.ganttRow("Gala night").value(s("barText")).toString().contains(s("slack")));
    }

    void baselinesZeroToTen()
    {
        Loaded l;
        QVERIFY(l.loaded);
        const QVariantList list = l.plan->baselines();
        QCOMPARE(list.size(), 11);
        QCOMPARE(list.at(10).toMap().value(s("label")).toString(), s("Baseline 10"));
        QVERIFY(!list.at(10).toMap().value(s("set")).toBool());
        QVERIFY(ok(l.plan->setBaseline(10)));
        QCOMPARE(l.plan->baselines().at(10).toMap().value(s("tasks")).toInt(), 40);
        l.plan->setCurrentTaskId(l.id("Press release"));
        QVERIFY(ok(l.plan->clearBaseline(10, true)));
        QCOMPARE(l.plan->baselines().at(10).toMap().value(s("tasks")).toInt(), 39);
        QVERIFY(!l.task("Press release").baselines[10].set);
        QVERIFY(!ok(l.plan->clearBaseline(10, true))); // already cleared there
        // Baseline 10 travels through Project XML.
        const QString path = l.dir.filePath(s("gala.xml"));
        QVERIFY(l.host.planDocument()->save(path));
        PlanHost reopened;
        QVERIFY(reopened.loadPath(path));
        int count = 0;
        for (const Engine::Task &t : reopened.plan()->project().tasks) {
            count += t.baselines[10].set ? 1 : 0;
        }
        QCOMPARE(count, 39);
    }

    void markOnTrackAndUpdateTasks()
    {
        Loaded l;
        QVERIFY(l.loaded);
        const int before = stackCount(l);
        l.plan->setCurrentTaskId(l.id("Social media campaign"));
        QVERIFY(l.host.actions()->trigger(s("planMarkOnTrackAction")));
        QVERIFY(l.task("Social media campaign").percentComplete > 15);
        QCOMPARE(stackCount(l), before + 1);
        // Update Tasks on a task not started: half done, then more remains.
        l.plan->setCurrentTaskId(l.id("Press release"));
        QVERIFY(ok(l.plan->updateTasks({{s("percent"), s("50")}})));
        QCOMPARE(l.task("Press release").percentComplete, 50);
        QVERIFY(l.task("Press release").actualStart != Engine::kNoDate);
        QVERIFY(ok(l.plan->updateTasks({{s("remainingDuration"), s("2d")}})));
        QCOMPARE(l.task("Press release").duration, 3 * kDay);
        QCOMPARE(l.task("Press release").percentComplete, 33);
        QCOMPARE(l.plan->taskFieldText(l.id("Press release"), s("actualDuration")), s("1 day"));
        const int steps = stackCount(l);
        const QVariantMap refused = l.plan->updateTasks({{s("actualDuration"), s("lots")}});
        QVERIFY(!ok(refused));
        QVERIFY(refused.value(s("error")).toString().contains(s("Act. Dur.")));
        QCOMPARE(stackCount(l), steps); // nothing changed
        QCOMPARE(stackCount(l), before + 3);
    }

    void levelingOptionsSelectionAndProjectXml()
    {
        Loaded l;
        QVERIFY(l.loaded);
        const QVariantMap options = l.plan->levelingOptions();
        QCOMPARE(options.value(s("order")).toString(), s("standard"));
        QVERIFY(options.value(s("clearFirst")).toBool());
        QVERIFY(ok(l.plan->setLevelingOptions({{s("order"), s("priority")}, {s("withinSlack"), true}})));
        QCOMPARE(int(l.plan->project().info.levelingOrder), int(Engine::LevelingOrder::PriorityStandard));
        QVERIFY(l.host.isDirty());
        l.host.planDocument()->undoStack()->undo();
        QCOMPARE(int(l.plan->project().info.levelingOrder), int(Engine::LevelingOrder::Standard));
        QVERIFY(!ok(l.plan->setLevelingOptions({{s("order"), s("random")}})));
        // A range after Marcus's clash levels nothing.
        const QVariantMap late = l.plan->level(0, s("2026-05-01"));
        QVERIFY(!ok(late));
        QVERIFY(!ok(l.plan->level(0, s("2026-05-01"), s("2026-04-01"))));
        // Level Selection: only Rent AV equipment may move.
        l.plan->setCurrentTaskId(l.id("Rent AV equipment"));
        QVERIFY(l.host.actions()->trigger(s("planLevelSelectionAction")));
        QVERIFY(l.task("Rent AV equipment").levelingDelay > 0);
        QCOMPARE(l.task("Plan menu with caterer").levelingDelay, 0);
        QVERIFY(l.task("Rent AV equipment").start >= l.task("Plan menu with caterer").finish);
        QCOMPARE(l.task("Rent AV equipment").preLeveledStart, l.task("Rent AV equipment").unleveledStart);
        const Engine::Minutes leveledStart = l.task("Rent AV equipment").start;
        // Saved as Project XML with Project's LevelingDelay, read back alike.
        const QString path = l.dir.filePath(s("leveled.xml"));
        QVERIFY(l.host.planDocument()->save(path));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray xml = file.readAll();
        QVERIFY(xml.contains("<LevelingDelayFormat>8</LevelingDelayFormat>"));
        QVERIFY(xml.contains("<PreLeveledStart>"));
        PlanHost reopened;
        QVERIFY(reopened.loadPath(path));
        const Engine::Project &back = reopened.plan()->project();
        for (const Engine::Task &t : back.tasks) {
            if (t.name == s("Rent AV equipment")) {
                QCOMPARE(t.start, leveledStart);
                QCOMPARE(t.levelingDelay, l.task("Rent AV equipment").levelingDelay);
            }
        }
        // Clear Leveling of the selection.
        QVERIFY(l.host.actions()->trigger(s("planClearLevelingSelectedAction")));
        QCOMPARE(l.task("Rent AV equipment").levelingDelay, 0);
        QVERIFY(!ok(l.plan->clearLevelingSelected()));
    }

    void networkUsageReportsAndPdfs()
    {
        Loaded l;
        QVERIFY(l.loaded);
        QVERIFY(l.plan->networkSize().width() > 0);
        const QRectF box = l.plan->networkBoxOf(l.id("Gala night"));
        QVERIFY(box.width() > 0);
        QCOMPARE(l.plan->networkTaskAt(box.center().x(), box.center().y()), l.id("Gala night"));
        QCOMPARE(l.plan->networkTaskAt(-5, -5), QString());
        // Task Usage: every task, then its work assignments (the card stock
        // is material, so not one of them).
        int work = 0;
        for (const Engine::Assignment &a : l.plan->project().assignments) {
            const int r = l.plan->project().resourceIndex(a.resourceUid);
            work += l.plan->project().resources.at(r).type == Engine::ResourceType::Work ? 1 : 0;
        }
        QCOMPARE(l.plan->taskUsageRows().size(), 40 + work);
        QCOMPARE(l.plan->taskUsageModel()->resourceCount(), 40 + work);
        QCOMPARE(l.plan->report(s("cost")).value(s("title")).toString(), s("Cost Overview"));
        QCOMPARE(l.plan->report(s("work")).value(s("title")).toString(), s("Work Overview"));
        QVERIFY(l.host.actions()->trigger(s("planReportCostAction")));
        QCOMPARE(l.host.reportId(), s("cost"));
        QCOMPARE(l.host.currentView(), s("reports"));
        // The print source follows the view: the Network Diagram, then the
        // Tracking Gantt, each a real PDF.
        for (const char *view : {"network", "tracking"}) {
            l.host.setCurrentView(s(view));
            QVERIFY(l.host.ganttPageCount() >= 1);
            const QString pdf = l.dir.filePath(s(view) + s(".pdf"));
            QVERIFY(ok(l.host.exportPdfTo(QUrl::fromLocalFile(pdf))));
            QFile f(pdf);
            QVERIFY(f.open(QIODevice::ReadOnly));
            QVERIFY(f.read(4).startsWith("%PDF"));
        }
    }

    void costResourcesThroughTheHost()
    {
        Loaded l;
        QVERIFY(l.loaded);
        QVERIFY(l.host.actions()->trigger(s("planAddCostResourceAction")));
        QCOMPARE(l.host.currentView(), s("resources"));
        const Engine::Resource &fee = l.plan->project().resources.last();
        QCOMPARE(int(fee.type), int(Engine::ResourceType::Cost));
        const int uid = fee.uid;
        QVERIFY(ok(l.plan->setResourceField(uid, s("name"), s("Hall rental"))));
        const qint64 before = l.task("Venue setup").cost;
        l.plan->setCurrentTaskId(l.id("Venue setup"));
        QVERIFY(ok(l.plan->assign(uid, s("$1,200"))));
        QCOMPARE(l.task("Venue setup").cost, before + 120000);
        bool listed = false;
        for (const QVariant &v : l.plan->assignmentsOf(l.id("Venue setup"))) {
            const QVariantMap a = v.toMap();
            if (a.value(s("resourceUid")).toInt() == uid) {
                listed = a.value(s("costResource")).toBool() && a.value(s("units")).toString() == s("$1,200.00");
            }
        }
        QVERIFY(listed);
        QVERIFY(!ok(l.plan->assign(uid, s("a lot"))));
        QVERIFY(l.plan->taskFieldText(l.id("Venue setup"), s("resources")).contains(s("Hall rental[$1,200.00]")));
    }

    void scheduleOptionsInProjectInformation()
    {
        Loaded l;
        QVERIFY(l.loaded);
        QVERIFY(!l.task("Social media campaign").critical);
        QVERIFY(ok(l.plan->setProjectInfo({{s("criticalSlackDays"), s("40")}})));
        QCOMPARE(l.plan->projectInfo().value(s("criticalSlackDays")).toInt(), 40);
        QVERIFY(l.task("Social media campaign").critical);
        QVERIFY(!ok(l.plan->setProjectInfo({{s("criticalSlackDays"), s("-1")}})));
        QVERIFY(ok(l.plan->setProjectInfo({{s("criticalSlackDays"), s("0")}, {s("multipleCriticalPaths"), true}})));
        QVERIFY(l.plan->projectInfo().value(s("multipleCriticalPaths")).toBool());
    }
};

PLAN_APP_TEST_MAIN(PlanTrackingTest)

#include "tst_qindaplan_tracking.moc"
