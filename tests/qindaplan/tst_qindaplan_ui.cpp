// SPDX-License-Identifier: GPL-3.0-or-later
// QindaPlan through the real window (M51): the seeded "Spring fundraiser
// gala" renders in every view at 1280 and 520 px — the Gantt with its
// critical path, baselines and today/status lines beside the task sheet,
// in-place editing of the sheet, the resource sheet, resource usage with
// the overallocation, the calendar and the project overview report; Task
// Information opens on the selected task; the ribbon gate holds;
// structural fixtures. QQO_GRAB_DIR saves a PNG of each view for review.

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

class PlanUiTest final : public QObject {
    Q_OBJECT
private slots:
    void viewsRenderTheGala_data()
    {
        QTest::addColumn<int>("width");
        QTest::newRow("1280") << 1280;
        QTest::newRow("520") << 520;
    }
    void viewsRenderTheGala()
    {
        QFETCH(int, width);
        Scene scene(QSize(width, width > 1000 ? 800 : 700));
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QTest::qWait(300);
        // Gantt: the critical path, milestones, summaries, baselines and
        // the today/status lines are drawn.
        scene.host.plan()->setCurrentTaskId(scene.id("Sponsor outreach"));
        QTest::qWait(150);
        QindaOffice::Test::saveGrab(scene.window, s("qindaplan_gantt_%1").arg(width));
        QQuickItem *gantt = scene.item(s("planGantt"));
        QVERIFY(gantt != nullptr);
        QVERIFY(gantt->isVisible());
        QVERIFY(countNamed(gantt, s("ganttTaskBar")) > 5);
        QVERIFY(countNamed(gantt, s("ganttSummaryBar")) >= 3);
        QVERIFY(countNamed(gantt, s("ganttMilestone")) >= 2);
        QVERIFY(countNamed(gantt, s("ganttBaseline")) > 5);
        QVERIFY(findNamed(gantt, s("ganttMarker_0")) != nullptr);
        QVERIFY(findNamed(gantt, s("ganttMarker_1")) != nullptr);
        QVERIFY(scene.item(s("planTimelinePaper")) != nullptr);

        // Task sheet, editing a duration in place.
        scene.show("sheet");
        QQuickItem *sheet = scene.item(s("planTaskSheet"));
        QVERIFY(sheet != nullptr && sheet->isVisible());
        QObject *model = sheet->property("model").value<QObject *>();
        QVERIFY(model != nullptr);
        int row = -1;
        int column = -1;
        QMetaObject::invokeMethod(model, "rowIndex", Q_RETURN_ARG(int, row), Q_ARG(QString, scene.id("Press release")));
        QMetaObject::invokeMethod(model, "columnIndex", Q_RETURN_ARG(int, column), Q_ARG(QString, s("duration")));
        QVERIFY(row >= 0 && column >= 0);
        QMetaObject::invokeMethod(sheet, "setCurrent", Q_ARG(QVariant, row), Q_ARG(QVariant, column), Q_ARG(QVariant, false));
        QVariant began;
        QMetaObject::invokeMethod(sheet, "beginCellEdit", Q_RETURN_ARG(QVariant, began));
        QVERIFY(began.toBool());
        QMetaObject::invokeMethod(sheet, "setEditorText", Q_ARG(QVariant, s("4d")));
        QTest::qWait(150);
        QindaOffice::Test::saveGrab(scene.window, s("qindaplan_sheet_editing_%1").arg(width));
        QVariant committed;
        QMetaObject::invokeMethod(sheet, "commitCellEdit", Q_RETURN_ARG(QVariant, committed), Q_ARG(QVariant, s("4d")));
        QVERIFY(committed.toBool());
        QTRY_COMPARE(scene.host.plan()->taskFieldText(scene.id("Press release"), s("duration")), s("4 days"));

        scene.show("resources");
        QVERIFY(scene.item(s("planResourceSheet"))->isVisible());
        QindaOffice::Test::saveGrab(scene.window, s("qindaplan_resources_%1").arg(width));

        scene.show("usage");
        QVERIFY(scene.item(s("planUsageTable"))->isVisible());
        QVERIFY(scene.item(s("planUsageNotice"))->isVisible());
        QindaOffice::Test::saveGrab(scene.window, s("qindaplan_usage_%1").arg(width));

        scene.show("calendar");
        QVERIFY(scene.item(s("planCalendar"))->isVisible());
        QindaOffice::Test::saveGrab(scene.window, s("qindaplan_calendar_%1").arg(width));

        scene.show("reports");
        QQuickItem *reports = scene.item(s("planReports"));
        QVERIFY(reports != nullptr && reports->isVisible());
        QTRY_VERIFY(findNamed(reports, s("planKpi_finish")) != nullptr);
        QindaOffice::Test::saveGrab(scene.window, s("qindaplan_overview_%1").arg(width));
    }

    void taskInformationOpensOnTheTask()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        scene.host.plan()->setCurrentTaskId(scene.id("Book entertainment"));
        QVERIFY(scene.host.actions()->trigger(s("planTaskInfoAction")));
        QObject *dialog = scene.window->findChild<QObject *>(s("taskInfoDialog"));
        QVERIFY(dialog != nullptr);
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTest::qWait(200);
        QindaOffice::Test::saveGrab(scene.window, s("qindaplan_task_info"));
        QCOMPARE(dialog->property("info").toMap().value(s("name")).toString(), s("Book entertainment"));
    }

    void ribbonTabsForReview()
    {
        // Every tab at a desktop width, saved for review (QQO_GRAB_DIR).
        Scene scene(QSize(1320, 800));
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QQuickItem *ribbon = scene.item(s("officeRibbon"));
        QVERIFY(ribbon != nullptr);
        for (const QString &tab : QindaOffice::Test::callStrings(ribbon, "layoutTabIds")) {
            QVariant shown;
            QMetaObject::invokeMethod(ribbon, "activateTab", Q_RETURN_ARG(QVariant, shown), Q_ARG(QVariant, tab));
            QVERIFY2(shown.toBool(), qPrintable(tab));
            QTest::qWait(120);
            QindaOffice::Test::saveGrab(scene.window, s("qindaplan_tab_%1").arg(tab));
        }
    }

    void ribbonGateHolds()
    {
        for (const int width : {1320, 520}) {
            Scene scene(QSize(width, 800));
            QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
            QVERIFY(QTest::qWaitForWindowExposed(scene.window));
            QTest::qWait(150);
            QindaOffice::Test::saveGrab(scene.window, s("qindaplan_ribbon_%1").arg(width));
            const QStringList problems = QindaOffice::Test::ribbonProblems(
                scene.window, scene.host.menuTree(), {});
            QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
        }
    }

    void structuralFixtures()
    {
        for (const int width : {1280, 520}) {
            Scene scene(QSize(width, width == 1280 ? 800 : 700));
            QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
            QVERIFY(QTest::qWaitForWindowExposed(scene.window));
            QTest::qWait(250);
            QString diff;
            const QString path = QStringLiteral(QQO_FIXTURE_DIR "/qindaplan_%1.txt").arg(width);
            QVERIFY2(QindaOffice::Test::matchesFixture(scene.window, path, &diff), qPrintable(diff));
        }
    }
};

PLAN_APP_TEST_MAIN(PlanUiTest)

#include "tst_qindaplan_ui.moc"
