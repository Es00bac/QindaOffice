// SPDX-License-Identifier: GPL-3.0-or-later
#include "base_app_test_support.h"
#include "support/quick_dump.h"
#include "support/ribbon_gate.h"

#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>

using namespace BaseAppTest;

namespace {
QVariantMap action(const char *id, const QVariantMap &arguments = {})
{
    return {{str("action"), str(id)}, {str("arguments"), arguments}};
}
QVariant invoke(QObject *target, const char *name, const QVariant &a)
{
    QVariant result;
    QMetaObject::invokeMethod(target, name, Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, a));
    return result;
}
QVariant invoke(QObject *target, const char *name, const QVariant &a, const QVariant &b)
{
    QVariant result;
    QMetaObject::invokeMethod(target, name, Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, a), Q_ARG(QVariant, b));
    return result;
}
struct Scene {
    QTemporaryDir dir;
    QQmlApplicationEngine engine;
    BaseHost host;
    QQuickWindow *window = nullptr;
    QString error;
    explicit Scene(int width)
    {
        if (!host.baseDocument()->createAt(dir.filePath(str("Events.qbase")), str("donors"), &error)
            || !seedDonors(*host.base()->file())) return;
        engine.addImportPath(QStringLiteral(QQO_QINDATK_QML_ROOT));
        QQmlComponent component(&engine, QUrl(str("qrc:/qt/qml/QindaOffice/Base/App/BaseWindow.qml")));
        window = qobject_cast<QQuickWindow *>(component.createWithInitialProperties({{str("host"), QVariant::fromValue(&host)}}));
        if (!window) { error = component.errorString(); return; }
        host.attachWindow(window);
        window->resize(width, width == 520 ? 720 : 800);
        window->show();
    }
    ~Scene() { delete window; }
    QObject *form() const { return window->property("formView").value<QObject *>(); }
    QObject *designer() const { return form()->property("formDesigner").value<QObject *>(); }
    QObject *commands() const { return window->property("commands").value<QObject *>(); }
    QObject *design() const { return form()->property("designDocument").value<QObject *>(); }
    void open(bool designing = false) const
    {
        invoke(commands(), designing ? "designObject" : "openObject", str("form"), str("Donation Entry"));
    }
    bool macro(const char *name, const QVariantList &actions)
    {
        const auto result = host.base()->saveMacro(str(name), {{str("actions"), actions}});
        if (!ok(result)) error = result.value(str("error")).toString();
        return ok(result);
    }
    QVariantMap variables() const { return commands()->property("macroTemporaryValues").toMap(); }
    bool eventRunning() const { return form()->property("events").value<QObject *>()->property("running").toBool(); }
};
}

class BaseEventsTest final : public QObject {
    Q_OBJECT
private slots:
    void eventsCancelBeforeWritesAndBindingsUndo_data()
    {
        QTest::addColumn<int>("width");
        QTest::newRow("wide") << 1280;
        QTest::newRow("narrow") << 520;
    }
    void eventsCancelBeforeWritesAndBindingsUndo()
    {
        QFETCH(int, width);
        Scene scene(width);
        QVERIFY2(scene.window, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QVERIFY(scene.macro("Opened", {action("setTempVar", {{str("name"), str("Opened")}, {str("expression"), str("1")}})}));
        QVERIFY(scene.macro("Current", {action("setTempVar", {{str("name"), str("Current")}, {str("expression"), str("1")}})}));
        QVERIFY(scene.macro("Changed", {action("setTempVar", {{str("name"), str("Changed")}, {str("expression"), str("[amount]")}})}));
        QVERIFY(scene.macro("Validate", {
            action("if", {{str("condition"), str("[amount] < 100")}}),
            action("cancelEvent", {{str("message"), str("The amount must be at least 100.")}}), action("else"),
            action("setValue", {{str("field"), str("receipt_sent")}, {str("expression"), str("True")}}), action("endIf")}));
        QVERIFY(scene.macro("After", {action("setTempVar", {{str("name"), str("After")}, {str("expression"), str("[amount]")}})}));
        QVERIFY(scene.macro("Closed", {action("setTempVar", {{str("name"), str("Closed")}, {str("expression"), str("1")}})}));
        QVERIFY(scene.macro("Close button", {action("closeWindow")}));
        scene.open(true);
        QTRY_VERIFY(scene.design());
        const QString button = invoke(scene.form(), "addControl", str("commandButton"),
                                      QVariantMap{{str("caption"), str("Close form")}, {str("actionId"), str("closeWindow")}}).toString();
        QVERIFY(!button.isEmpty());
        const QVariantMap formEvents{{str("open"), str("Opened")}, {str("current"), str("Current")},
                                     {str("beforeUpdate"), str("Validate")}, {str("afterUpdate"), str("After")},
                                     {str("close"), str("Closed")}};
        const QVariantMap controls{{str("amount"), QVariantMap{{str("change"), str("Changed")}}},
                                   {button, QVariantMap{{str("click"), str("Close button")}}}};
        const QVariantMap bindings{{str("form"), formEvents}, {str("controls"), controls}};
        QVERIFY(ok(invoke(scene.form(), "setEventBindings", bindings).toMap()));
        QVERIFY(scene.design()->property("form").toMap().value(str("events")).toMap() == bindings);
        QMetaObject::invokeMethod(scene.form(), "undo");
        QVERIFY(scene.design()->property("form").toMap().value(str("events")).toMap().isEmpty());
        QMetaObject::invokeMethod(scene.form(), "redo");
        QVERIFY(scene.design()->property("form").toMap().value(str("events")).toMap() == bindings);
        // The events editor is reachable through the same stable action as
        // the ribbon and exported menu; its apply edits stay on this history.
        QVERIFY(scene.host.actions()->trigger(str("baseFormEventsAction")));
        QTest::qWait(250);
        auto *picker = QindaOffice::Test::findNamed(scene.window->contentItem(), str("baseFormEvent_beforeUpdate"));
        QVERIFY(picker && picker->isVisible());
        QindaOffice::Test::saveGrab(scene.window, str("qindabase_events_%1").arg(width));
        QTest::keyClick(scene.window, Qt::Key_Escape);
        QVariant saved;
        QVERIFY(QMetaObject::invokeMethod(scene.form(), "save", Q_RETURN_ARG(QVariant, saved)));
        QVERIFY(saved.toBool());
        scene.open();
        QTRY_VERIFY(scene.variables().contains(str("Opened")));
        QTRY_VERIFY(scene.variables().contains(str("Current")));
        QTRY_VERIFY(!scene.eventRunning());
        QCOMPARE(scene.design()->property("form").toMap().value(str("events")).toMap(), bindings);
        const qint64 row = scene.host.base()->file()->db().scalar(str("SELECT min(rowid) FROM donations")).toLongLong();
        const QVariant before = scene.host.base()->file()->db().scalar(str("SELECT amount FROM donations WHERE rowid=?"), {row});
        QVERIFY(invoke(scene.designer(), "setFieldValue", str("amount"), str("50.00")).toBool());
        QTRY_VERIFY(scene.variables().contains(str("Changed")));
        QTRY_VERIFY(!scene.eventRunning());
        QVERIFY(scene.host.actions()->trigger(str("baseSaveRecordAction")));
        QTRY_COMPARE(scene.designer()->property("requestState").toString(), str("rejected"));
        QCOMPARE(scene.host.base()->file()->db().scalar(str("SELECT amount FROM donations WHERE rowid=?"), {row}), before);
        QVERIFY(!scene.variables().contains(str("After")));
        QVERIFY(invoke(scene.designer(), "setFieldValue", str("amount"), str("150.00")).toBool());
        QTRY_COMPARE(scene.variables().value(str("Changed")).toMap().value(str("value")).toLongLong(), qint64(15000));
        QTRY_VERIFY(!scene.eventRunning());
        QVERIFY(scene.host.actions()->trigger(str("baseSaveRecordAction")));
        QTRY_VERIFY(scene.variables().contains(str("After")));
        QTRY_VERIFY(!scene.eventRunning());
        QCOMPARE(scene.host.base()->file()->db().scalar(str("SELECT amount FROM donations WHERE rowid=?"), {row}).toLongLong(), qint64(15000));
        QCOMPARE(scene.host.base()->file()->db().scalar(str("SELECT receipt_sent FROM donations WHERE rowid=?"), {row}).toInt(), 1);
        QCOMPARE(scene.variables().value(str("After")).toMap().value(str("type")).toString(), str("currency"));
        // Click invokes CloseWindow, which in turn runs the distinct form
        // Close event. Their continuations must not cancel or deadlock.
        invoke(scene.form(), "runCommand", button, str("closeWindow"));
        QTRY_VERIFY(scene.variables().contains(str("Closed")));
        QTRY_COMPARE(scene.host.currentView(), str("start"));
    }

    void nativeWindowCloseHonorsTheFormVeto()
    {
        Scene scene(1280);
        QVERIFY2(scene.window, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QVERIFY(scene.macro("Refuse close", {
            action("setTempVar", {{str("name"), str("CloseAttempt")}, {str("expression"), str("1")}}),
            action("cancelEvent", {{str("message"), str("Keep the active form open.")}})}));
        scene.open(true);
        QTRY_VERIFY(scene.design());
        QVERIFY(ok(invoke(scene.form(), "setEventBindings",
                          QVariantMap{{str("form"), QVariantMap{{str("close"), str("Refuse close")}}}}).toMap()));
        QVariant saved;
        QVERIFY(QMetaObject::invokeMethod(scene.form(), "save", Q_RETURN_ARG(QVariant, saved)));
        QVERIFY(saved.toBool());
        scene.open();
        QTRY_VERIFY(scene.form()->property("recordEditing").toBool());
        scene.window->close();
        QTRY_VERIFY(scene.variables().contains(str("CloseAttempt")));
        QTRY_VERIFY(!scene.eventRunning());
        QVERIFY(scene.window->isVisible());
        QVERIFY(!scene.window->property("closePreparationPending").toBool());
        QCOMPARE(scene.host.currentView(), str("form"));
    }

    void sequentialCallsAreNotMistakenForRecursiveDepth()
    {
        Scene scene(1280);
        QVERIFY2(scene.window, qPrintable(scene.error));
        QVERIFY(scene.macro("Leaf", {action("setTempVar", {{str("name"), str("Leaf")}, {str("expression"), str("1")}})}));
        QVariantList calls;
        for (int i = 0; i < 25; ++i) calls.append(action("runMacro", {{str("macro"), str("Leaf")}}));
        calls.append(action("setTempVar", {{str("name"), str("Complete")}, {str("expression"), str("1")}}));
        QVERIFY(scene.macro("Sequence", calls));
        auto *runner = scene.commands()->property("macroRunner").value<QObject *>();
        QVERIFY(QMetaObject::invokeMethod(runner, "runNamed", Q_ARG(QVariant, str("Sequence")),
                                          Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, QVariant())));
        QTRY_VERIFY(scene.variables().contains(str("Complete")));
        QTRY_VERIFY(!runner->property("running").toBool());
        QCOMPARE(runner->property("depth").toInt(), 0);
        QVERIFY(scene.macro("Cycle A", {}));
        QVERIFY(scene.macro("Cycle B", {action("runMacro", {{str("macro"), str("Cycle A")}})}));
        QVERIFY(scene.macro("Cycle A", {action("runMacro", {{str("macro"), str("Cycle B")}})}));
        QVERIFY(QMetaObject::invokeMethod(runner, "runNamed", Q_ARG(QVariant, str("Cycle A")),
                                          Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, QVariant())));
        QTRY_VERIFY(!runner->property("running").toBool());
        QCOMPARE(runner->property("depth").toInt(), 20);
    }
};
BASE_APP_TEST_MAIN(BaseEventsTest)
#include "tst_qindabase_events.moc"
