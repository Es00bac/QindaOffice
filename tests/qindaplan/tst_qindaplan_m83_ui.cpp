// SPDX-License-Identifier: GPL-3.0-or-later
// M83 (D-092) through the real PlanWindow.qml at 1280 and 520 px: the
// Custom Fields table in the Task Sheet, and the dialogs M83 added or grew —
// Custom Fields with a lookup table, the Custom Field filter, Recurring
// Task, Split Task, Import Tasks with its column map, Task Information's
// Resources, Advanced (task type, effort driven) and Custom Fields pages,
// Change Working Time with a recurring exception and a work week, and Update
// Project's Reschedule Uncompleted Work. QQO_GRAB_DIR saves a PNG of each.

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
    // A dialog's objects, or a delegate a Repeater made (only in the item tree).
    QObject *named(const char *name) const
    {
        QObject *o = window->findChild<QObject *>(s(name));
        return o != nullptr ? o : findNamed(window->contentItem(), s(name));
    }
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
    // Opens a dialog through its command, grabs it and closes it.
    QObject *opened(const char *command, const char *dialog)
    {
        if (command != nullptr && !host.actions()->trigger(s(command))) {
            return nullptr;
        }
        QObject *d = named(dialog);
        if (d != nullptr && !QTest::qWaitFor([d] { return d->property("opened").toBool(); }, 3000)) {
            return nullptr;
        }
        QTest::qWait(200);
        return d;
    }
    void grab(const char *name, int width) const
    {
        QindaOffice::Test::saveGrab(window, s("qindaplan_m83_%1_%2").arg(s(name)).arg(width));
    }
};

} // namespace

class PlanM83UiTest final : public QObject {
    Q_OBJECT
private slots:
    void dialogsAndTable_data()
    {
        QTest::addColumn<int>("width");
        QTest::newRow("1280") << 1280;
        QTest::newRow("520") << 520;
    }
    void dialogsAndTable()
    {
        QFETCH(int, width);
        Scene scene(QSize(width, width > 1000 ? 800 : 720));
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        PlanService *plan = scene.host.plan();
        const QVariantList lookup{QVariantMap{{s("value"), s("Dana")}}, QVariantMap{{s("value"), s("Priya")}}};
        QVERIFY(ok(plan->saveCustomField(s("Text1"), {{s("alias"), s("Owner")}, {s("lookup"), lookup}, {s("restrict"), true}})));
        QVERIFY(ok(plan->saveCustomField(s("Number1"), {{s("alias"), s("Days")}, {s("formula"), s("[Duration]/480")}})));
        QVERIFY(ok(plan->setTaskField(scene.id("Book entertainment"), s("Text1"), s("Priya"))));

        // The Custom Fields table in the Task Sheet.
        scene.host.setCurrentView(s("sheet"));
        QVERIFY(scene.host.actions()->trigger(s("planTableCustomAction")));
        QTest::qWait(250);
        QQuickItem *sheet = scene.item(s("planTaskSheet"));
        QVERIFY(sheet != nullptr && sheet->isVisible());
        QObject *model = sheet->property("model").value<QObject *>();
        int column = -1;
        QMetaObject::invokeMethod(model, "columnIndex", Q_RETURN_ARG(int, column), Q_ARG(QString, s("Text1")));
        QVERIFY(column >= 0);
        scene.grab("custom_table", width);

        QObject *fields = scene.opened("planCustomFieldsAction", "customFieldsDialog");
        QVERIFY(fields != nullptr);
        QCOMPARE(fields->property("field").toString(), s("Text1"));
        QCOMPARE(fields->property("lookup").toList().size(), 2);
        QVERIFY(scene.named("customFieldsFormula") != nullptr);
        scene.grab("custom_fields", width);
        QMetaObject::invokeMethod(fields, "close");

        QObject *filter = scene.opened("planFilterCustomAction", "filterDialog");
        QVERIFY(filter != nullptr);
        QCOMPARE(filter->property("filterId").toString(), s("custom"));
        scene.grab("custom_filter", width);
        QMetaObject::invokeMethod(filter, "close");

        plan->setSelectedTaskIds({});
        QObject *recurring = scene.opened("planInsertRecurringAction", "recurringTaskDialog");
        QVERIFY(recurring != nullptr);
        QVERIFY(scene.named("recurrenceFrequency") != nullptr);
        scene.grab("recurring_task", width);
        QMetaObject::invokeMethod(recurring, "close");

        plan->setSelectedTaskIds({scene.id("Floor plan and seating")});
        plan->setCurrentTaskId(scene.id("Floor plan and seating"));
        QObject *split = scene.opened("planSplitTaskAction", "splitTaskDialog");
        QVERIFY(split != nullptr);
        QVERIFY(!scene.named("splitTaskStop")->property("text").toString().isEmpty());
        scene.grab("split_task", width);
        QMetaObject::invokeMethod(split, "close");

        // Task Information: Resources (Venue setup has three people),
        // Advanced (type, effort driven) and Custom Fields.
        plan->setCurrentTaskId(scene.id("Venue setup"));
        QObject *info = scene.opened("planTaskInfoAction", "taskInfoDialog");
        QVERIFY(info != nullptr);
        QMetaObject::invokeMethod(info, "openFor", Q_ARG(QVariant, scene.id("Venue setup")), Q_ARG(QVariant, s("resources")));
        QTRY_VERIFY(scene.named("taskInfoAssignments")->property("visible").toBool());
        QCOMPARE(scene.named("taskInfoAssignments")->property("rows").toList().size(), 3);
        QTest::qWait(150);
        scene.grab("task_info_resources", width);
        QMetaObject::invokeMethod(info, "openFor", Q_ARG(QVariant, scene.id("Venue setup")), Q_ARG(QVariant, s("advanced")));
        QTRY_VERIFY(scene.named("taskInfoType")->property("visible").toBool());
        QTest::qWait(150);
        scene.grab("task_info_advanced", width);
        QMetaObject::invokeMethod(info, "openFor", Q_ARG(QVariant, scene.id("Book entertainment")), Q_ARG(QVariant, s("custom")));
        QTRY_VERIFY(scene.named("taskInfoCustom")->property("visible").toBool());
        QTest::qWait(150);
        scene.grab("task_info_custom", width);
        QMetaObject::invokeMethod(info, "close");

        const int cal = plan->project().info.calendarUid;
        QVERIFY(ok(plan->saveRecurringException(cal, -1, {{s("frequency"), s("yearly")}, {s("name"), s("Founders' day")},
                                                          {s("day"), 15}, {s("month"), 4}, {s("from"), s("2026-01-01")},
                                                          {s("to"), s("2030-12-31")}})));
        QVERIFY(ok(plan->saveWorkWeek(cal, -1, {{s("name"), s("Gala week")}, {s("from"), s("2026-05-11")}, {s("to"), s("2026-05-15")},
                                               {s("days"), QStringList({s("default"), s("default"), s("default"), s("default"),
                                                                        s("08:00–12:00"), s("default"), s("default")})}})));
        QObject *working = scene.opened("planWorkingTimeAction", "workingTimeDialog");
        QVERIFY(working != nullptr);
        QTRY_VERIFY(scene.named("workingTimeRecurring_0") != nullptr);
        QVERIFY(scene.named("workingTimeWeek_0") != nullptr);
        scene.grab("working_time", width);
        QMetaObject::invokeMethod(scene.named("workingTimeRecurringEdit_0"), "clicked");
        QTest::qWait(150);
        QCOMPARE(scene.named("workingTimeRecurringName")->property("text").toString(), s("Founders' day"));
        scene.grab("working_time_editing", width);
        QMetaObject::invokeMethod(working, "close");

        QObject *reschedule = scene.opened("planRescheduleWorkAction", "statusDateDialog");
        QVERIFY(reschedule != nullptr);
        QVERIFY(reschedule->property("rescheduling").toBool());
        scene.grab("reschedule", width);
        QMetaObject::invokeMethod(reschedule, "close");

        const QString csv = scene.dir.filePath(s("tasks.csv"));
        QFile f(csv);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("ID,Task Name,Duration,Predecessors,Text1\n1,Caterer contract,2d,,Dana\n2,Table linens,1d,1,Priya\n");
        f.close();
        QObject *import = scene.named("importTasksDialog");
        QMetaObject::invokeMethod(import, "openFor", Q_ARG(QVariant, QUrl::fromLocalFile(csv)));
        QTRY_VERIFY(import->property("opened").toBool());
        QCOMPARE(import->property("mapping").toStringList(),
                 QStringList({s("id"), s("name"), s("duration"), s("predecessors"), s("Text1")}));
        QTest::qWait(200);
        scene.grab("import_tasks", width);
        QMetaObject::invokeMethod(import, "accept");
        QTRY_VERIFY(!scene.id("Table linens").isEmpty());
        QCOMPARE(plan->taskFieldText(scene.id("Table linens"), s("Text1")), s("Priya"));
    }
};

PLAN_APP_TEST_MAIN(PlanM83UiTest)

#include "tst_qindaplan_m83_ui.moc"
