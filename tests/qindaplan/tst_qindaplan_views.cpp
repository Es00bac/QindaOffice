// SPDX-License-Identifier: GPL-3.0-or-later
// M79 (D-088) through the real PlanWindow.qml at 1280 and 520 px: the
// Tracking Gantt, the Leveling Gantt after leveling, a filtered Gantt, the
// Network Diagram (a click selects a box, a double click opens Task
// Information), Task Usage, the Cost and Work Overview dashboards, and the
// dialogs M79 added or grew — Resource
// Leveling, Update Tasks, Using Resource, Set Baseline with Project's
// eleven baselines and the critical-path options in Project Information.
// QQO_GRAB_DIR saves a PNG of each (and the PDFs the views print) for review.

#include "plan_app_test_support.h"

#include "support/quick_dump.h"
#include "support/ribbon_gate.h"

#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>

using namespace PlanAppTest;
using QindaOffice::Test::findNamed;

namespace {

struct Scene {
    QTemporaryDir dir;
    QQmlApplicationEngine engine;
    PlanHost host;
    QQuickWindow *window = nullptr;
    QString error;

    explicit Scene(const QSize &size)
    {
        host.plan()->setToday(kToday);
        if (!host.loadPath(writeGala(dir))) {
            error = s("the gala did not load");
            return;
        }
        engine.addImportPath(QStringLiteral(QQO_QINDATK_QML_ROOT));
        QQmlComponent component(&engine, QUrl(s("qrc:/qt/qml/QindaOffice/Plan/App/PlanWindow.qml")));
        QObject *object = component.createWithInitialProperties({{s("host"), QVariant::fromValue(&host)}});
        if (object == nullptr) {
            error = component.errorString();
            return;
        }
        window = qobject_cast<QQuickWindow *>(object);
        host.attachWindow(window);
        window->resize(size);
        window->show();
    }
    ~Scene() { delete window; }

    [[nodiscard]] QQuickItem *item(const QString &name) const { return findNamed(window->contentItem(), name); }
    QString id(const char *name) const
    {
        const Engine::Project &p = host.plan()->project();
        for (int i = 0; i < p.tasks.size(); ++i) {
            if (p.tasks.at(i).name == s(name)) {
                return host.plan()->idOfRow(i);
            }
        }
        return {};
    }
    void show(const char *view)
    {
        host.setCurrentView(s(view));
        QTest::qWait(250);
    }
    QObject *dialog(const char *name) const { return window->findChild<QObject *>(s(name)); }
    // Exports what the current view prints, next to the grabs.
    void exportPdf(const QString &name)
    {
        const QString grabs = qEnvironmentVariable("QQO_GRAB_DIR");
        const QString path = grabs.isEmpty() ? dir.filePath(name + s(".pdf"))
                                             : grabs + QLatin1Char('/') + qEnvironmentVariable("QQO_GRAB_PREFIX") + name + s(".pdf");
        QVERIFY(ok(host.exportPdfTo(QUrl::fromLocalFile(path))));
    }
};

int countNamed(QQuickItem *root, const QString &name)
{
    int n = root->objectName() == name && root->isVisible() ? 1 : 0;
    for (QQuickItem *child : root->childItems()) {
        n += countNamed(child, name);
    }
    return n;
}

} // namespace

class PlanViewsUiTest final : public QObject {
    Q_OBJECT
private slots:
    void ganttViewsRender_data()
    {
        QTest::addColumn<int>("width");
        QTest::newRow("1280") << 1280;
        QTest::newRow("520") << 520;
    }
    void ganttViewsRender()
    {
        QFETCH(int, width);
        Scene scene(QSize(width, width > 1000 ? 800 : 700));
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        // Tracking Gantt: the Tracking table, baseline bars under every
        // baselined task, % complete after the bars.
        scene.show("tracking");
        QQuickItem *gantt = scene.item(s("planGantt"));
        QVERIFY(gantt != nullptr && gantt->isVisible());
        QVERIFY(countNamed(gantt, s("ganttBaseline")) > 10);
        QVERIFY(countNamed(gantt, s("ganttBarText")) > 10);
        QCOMPARE(scene.host.plan()->table(), s("tracking"));
        QindaOffice::Test::saveGrab(scene.window, s("qindaplan_tracking_%1").arg(width));
        if (width > 1000) {
            scene.exportPdf(s("qindaplan_tracking_pdf"));
        }
        // Critical path, filtered to the critical tasks (Gantt Chart).
        scene.show("gantt");
        QVERIFY(scene.host.actions()->trigger(s("planFilterCriticalAction")));
        scene.host.plan()->setCurrentTaskId(scene.id("Venue setup"));
        QVERIFY(scene.host.actions()->trigger(s("planScrollToTaskAction")));
        QTest::qWait(200);
        QVERIFY(countNamed(gantt, s("ganttTaskBar")) >= 5);
        QindaOffice::Test::saveGrab(scene.window, s("qindaplan_critical_%1").arg(width));
        QVERIFY(scene.host.actions()->trigger(s("planFilterAllAction")));
        // Leveling Gantt after Level All: pre-leveled bars under the delayed.
        scene.show("leveling");
        QVERIFY(ok(scene.host.plan()->level(0)));
        QString delayed;
        for (const Engine::Task &t : scene.host.plan()->project().tasks) {
            delayed = delayed.isEmpty() && t.levelingDelay > 0 ? scene.host.plan()->idOfRow(scene.host.plan()->project().taskIndex(t.uid))
                                                                : delayed;
        }
        QVERIFY(!delayed.isEmpty());
        scene.host.plan()->setCurrentTaskId(delayed);
        QVERIFY(scene.host.actions()->trigger(s("planScrollToTaskAction")));
        QTest::qWait(250);
        QVERIFY(countNamed(gantt, s("ganttBaseline")) >= 1);
        QCOMPARE(scene.host.plan()->table(), s("delay"));
        QindaOffice::Test::saveGrab(scene.window, s("qindaplan_leveling_%1").arg(width));
        if (width > 1000) {
            scene.exportPdf(s("qindaplan_leveling_pdf"));
        }
    }

    void networkDiagramSelectsAndOpens_data()
    {
        QTest::addColumn<int>("width");
        QTest::newRow("1280") << 1280;
        QTest::newRow("520") << 520;
    }
    void networkDiagramSelectsAndOpens()
    {
        QFETCH(int, width);
        Scene scene(QSize(width, width > 1000 ? 800 : 700));
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        scene.show("network");
        QQuickItem *network = scene.item(s("planNetwork"));
        QVERIFY(network != nullptr && network->isVisible());
        QVERIFY(network->width() > 100 && network->height() > 100);
        QindaOffice::Test::saveGrab(scene.window, s("qindaplan_network_%1").arg(width));
        if (width > 1000) {
            scene.exportPdf(s("qindaplan_network_pdf"));
        }
        // Click the first box: it becomes the current task.
        PlanService *plan = scene.host.plan();
        const auto &nodes = plan->networkLayout().nodes;
        QVERIFY(!nodes.isEmpty());
        const QString first = plan->idOfRow(nodes.first().taskIndex);
        const QPointF centre = nodes.first().box.center() * scene.host.zoom();
        const QPointF inWindow = network->mapToScene(centre - QPointF(network->x(), network->y()));
        QTest::mouseClick(scene.window, Qt::LeftButton, Qt::NoModifier, inWindow.toPoint());
        QTRY_COMPARE(plan->currentTaskId(), first);
        QTest::qWait(100);
        QindaOffice::Test::saveGrab(scene.window, s("qindaplan_network_selected_%1").arg(width));
        QTest::mouseDClick(scene.window, Qt::LeftButton, Qt::NoModifier, inWindow.toPoint());
        QObject *info = scene.dialog("taskInfoDialog");
        QVERIFY(info != nullptr);
        QTRY_VERIFY(info->property("opened").toBool());
        QCOMPARE(info->property("taskId").toString(), first);
    }

    void taskUsageRenders_data()
    {
        QTest::addColumn<int>("width");
        QTest::newRow("1280") << 1280;
        QTest::newRow("520") << 520;
    }
    void taskUsageRenders()
    {
        QFETCH(int, width);
        Scene scene(QSize(width, width > 1000 ? 800 : 700));
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        scene.show("taskUsage");
        QQuickItem *table = scene.item(s("planTaskUsageTable"));
        QVERIFY(table != nullptr && table->isVisible());
        QindaOffice::Test::saveGrab(scene.window, s("qindaplan_task_usage_%1").arg(width));
        // The Cost and Work Overview dashboards.
        QVERIFY(scene.host.actions()->trigger(s("planReportCostAction")));
        QTest::qWait(250);
        QQuickItem *reports = scene.item(s("planReports"));
        QTRY_VERIFY(findNamed(reports, s("planReportTable_costStatus")) != nullptr);
        QVERIFY(findNamed(reports, s("planKpi_actualCost")) != nullptr);
        QindaOffice::Test::saveGrab(scene.window, s("qindaplan_report_cost_%1").arg(width));
        QVERIFY(scene.host.actions()->trigger(s("planReportWorkAction")));
        QTRY_VERIFY(findNamed(reports, s("planReportTable_workStats")) != nullptr);
        QindaOffice::Test::saveGrab(scene.window, s("qindaplan_report_work_%1").arg(width));
    }

    void dialogsOpen()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        PlanService *plan = scene.host.plan();
        // Resource Leveling: order, slack, clear-first, range.
        QVERIFY(scene.host.actions()->trigger(s("planLevelingOptionsAction")));
        QObject *level = scene.dialog("levelDialog");
        QTRY_VERIFY(level->property("opened").toBool());
        QVERIFY(scene.window->findChild<QObject *>(s("levelOrder")) != nullptr);
        QCOMPARE(scene.window->findChild<QObject *>(s("levelOrder"))->property("currentIndex").toInt(), 1);
        QTest::qWait(150);
        QindaOffice::Test::saveGrab(scene.window, s("qindaplan_leveling_options"));
        QMetaObject::invokeMethod(level, "close");
        // Update Tasks on Sponsor outreach (30 %).
        plan->setCurrentTaskId(scene.id("Sponsor outreach"));
        QVERIFY(scene.host.actions()->trigger(s("planUpdateTasksAction")));
        QObject *update = scene.dialog("updateTasksDialog");
        QTRY_VERIFY(update->property("opened").toBool());
        QCOMPARE(scene.window->findChild<QObject *>(s("updatePercent"))->property("text").toString(), s("30"));
        QTest::qWait(150);
        QindaOffice::Test::saveGrab(scene.window, s("qindaplan_update_tasks"));
        QMetaObject::invokeMethod(update, "close");
        // Using Resource…
        QVERIFY(scene.host.actions()->trigger(s("planFilterResourceAction")));
        QObject *filter = scene.dialog("filterDialog");
        QTRY_VERIFY(filter->property("opened").toBool());
        QCOMPARE(filter->property("filterId").toString(), s("resource"));
        QTest::qWait(150);
        QindaOffice::Test::saveGrab(scene.window, s("qindaplan_filter_resource"));
        QMetaObject::invokeMethod(filter, "close");
        // Set Baseline: Baseline … Baseline 10.
        QVERIFY(scene.host.actions()->trigger(s("planSetBaselineAction")));
        QObject *baseline = scene.dialog("baselineDialog");
        QTRY_VERIFY(baseline->property("opened").toBool());
        QCOMPARE(baseline->property("baselines").toList().size(), 11);
        QTest::qWait(150);
        QindaOffice::Test::saveGrab(scene.window, s("qindaplan_set_baseline"));
        QMetaObject::invokeMethod(baseline, "close");
        // Project Information carries the critical-path options.
        QVERIFY(scene.host.actions()->trigger(s("planProjectInfoAction")));
        QObject *project = scene.dialog("projectInfoDialog");
        QTRY_VERIFY(project->property("opened").toBool());
        QCOMPARE(scene.window->findChild<QObject *>(s("projectCriticalSlack"))->property("text").toString(), s("0"));
        QTest::qWait(150);
        QindaOffice::Test::saveGrab(scene.window, s("qindaplan_project_info"));
    }
};

PLAN_APP_TEST_MAIN(PlanViewsUiTest)

#include "tst_qindaplan_views.moc"
