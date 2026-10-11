// SPDX-License-Identifier: GPL-3.0-or-later
// M89 (D-097) through the real PlanWindow.qml at 1280 and 520 px: View ▸
// Group By's bands as QindaTK group header rows in the task sheet and the
// Gantt chart (a band collapsed from the chart's chevron leaves both), the
// planning bars the chart draws since M89 — slack, a deadline, a split
// task's pieces, a second baseline and the progress line — and View ▸
// Highlight in the sheet. QQO_GRAB_DIR saves a PNG of each.

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
    // Every item under `root` named `name` (a Repeater's delegates).
    static void collect(QQuickItem *root, const QString &name, QList<QQuickItem *> *out)
    {
        if (root == nullptr) {
            return;
        }
        if (root->objectName() == name && root->isVisible()) {
            out->append(root);
        }
        for (QQuickItem *child : root->childItems()) {
            collect(child, name, out);
        }
    }
    [[nodiscard]] int visibleCount(const char *name) const
    {
        QList<QQuickItem *> items;
        collect(window->contentItem(), s(name), &items);
        return int(items.size());
    }
    void grab(const char *name, int width) const
    {
        QindaOffice::Test::saveGrab(window, s("qindaplan_m89_%1_%2").arg(s(name)).arg(width));
    }
};

} // namespace

class PlanM89UiTest final : public QObject {
    Q_OBJECT
private slots:
    void groupsBarsAndHighlight_data()
    {
        QTest::addColumn<int>("width");
        QTest::newRow("1280") << 1280;
        QTest::newRow("520") << 520;
    }
    void groupsBarsAndHighlight()
    {
        QFETCH(int, width);
        Scene scene(QSize(width, width > 1000 ? 800 : 720));
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        PlanService *plan = scene.host.plan();

        // Group By Status: bands in the sheet and the chart.
        QVERIFY(scene.host.actions()->trigger(s("planGroupStatusAction")));
        QTest::qWait(300);
        const QString band = plan->sheetModel()->rowIdAt(0);
        QVERIFY(plan->groupHeader(band).isValid());
        QTRY_VERIFY(scene.item(s("gridGroupHeader_") + band) != nullptr);
        QTRY_VERIFY(scene.visibleCount("ganttGroupHeader") >= 2);
        scene.grab("groups", width);
        // The chart's chevron collapses the first band in both views.
        const int rows = plan->sheetModel()->rows();
        QQuickItem *chevron = scene.item(s("ganttGroupChevron"));
        QVERIFY(chevron != nullptr);
        QTest::mouseClick(scene.window, Qt::LeftButton, {},
                          chevron->mapToScene(QPointF(chevron->width() / 2, chevron->height() / 2)).toPoint());
        QTRY_VERIFY(plan->sheetModel()->rows() < rows);
        bool collapsed = false;
        for (int r = 0; r < plan->sheetModel()->rows(); ++r) {
            collapsed = collapsed || plan->groupHeader(plan->sheetModel()->rowIdAt(r)).toMap().value(s("collapsed")).toBool();
        }
        QVERIFY(collapsed);
        QCOMPARE(plan->ganttModel()->rowCount(), plan->sheetModel()->rows() - 1); // the same rows, "new" aside
        scene.grab("groups_collapsed", width);
        QVERIFY(scene.host.actions()->trigger(s("planGroupNoneAction")));

        // The planning bars.
        QVERIFY(ok(plan->setTaskField(scene.id("Sponsor outreach"), s("deadline"), s("2026-04-10"))));
        plan->setSelectedTaskIds({scene.id("Floor plan and seating")});
        const QDate start = Engine::dateOf(plan->project().tasks.at(plan->rowOf(scene.id("Floor plan and seating"))).start);
        QVERIFY(ok(plan->splitCurrentTask(start.toString(Qt::ISODate), start.addDays(7).toString(Qt::ISODate))));
        QVERIFY(ok(plan->setBaseline(1)));
        for (const char *command : {"planShowSlackAction", "planShowProgressLinesAction", "planBaseline2Shown1Action"}) {
            QVERIFY2(scene.host.actions()->trigger(s(command)), command);
        }
        QTest::qWait(300);
        QTRY_VERIFY(scene.visibleCount("ganttSlack") > 0);
        QVERIFY(scene.visibleCount("ganttDeadline") > 0);
        QVERIFY(scene.visibleCount("ganttBaseline2") > 0);
        QVERIFY(scene.visibleCount("ganttSplitGap") > 0);
        QVERIFY(scene.item(s("ganttProgressLines")) != nullptr);
        scene.grab("bars", width);

        // Highlight: the critical tasks marked in the sheet, nothing hidden.
        QVERIFY(scene.host.actions()->trigger(s("planHighlightCriticalAction")));
        QTest::qWait(200);
        QVERIFY(plan->highlightFill().isValid());
        scene.grab("highlight", width);
    }
};

PLAN_APP_TEST_MAIN(PlanM89UiTest)
#include "tst_qindaplan_m89_ui.moc"
