// SPDX-License-Identifier: GPL-3.0-or-later
// M89 (D-097) through the real window, on the Donors & Donations starter
// seeded with 40 donors and 120 donations: forms on QindaTK's positioned
// controls. An M80 form opens laid out (text boxes, the subform's linked
// datasheet, the command button running its macro); a lookup combo box
// shows the related record's name and saves its key; a calculated text box
// shows the engine's value; in Design, a toolbox click and a drag on the
// canvas are one undo step each, and Add Button adds a command button with
// a macro action. At 1280 and 520 px; QQO_GRAB_DIR saves PNGs.

#include "base_app_test_support.h"

#include "support/quick_dump.h"
#include "support/ribbon_gate.h"

#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>

using namespace BaseAppTest;
using QindaOffice::Test::findNamed;

namespace {

struct Scene {
    QTemporaryDir dir;
    QQmlApplicationEngine engine;
    BaseHost host;
    QQuickWindow *window = nullptr;
    QString error;

    explicit Scene(const QSize &size)
    {
        if (!host.baseDocument()->createAt(dir.filePath(str("Community Donors.qbase")), str("donors"), &error)
            || !seedDonors(*host.base()->file())) {
            error = error.isEmpty() ? str("seeding failed") : error;
            return;
        }
        engine.addImportPath(QStringLiteral(QQO_QINDATK_QML_ROOT));
        QQmlComponent component(&engine, QUrl(str("qrc:/qt/qml/QindaOffice/Base/App/BaseWindow.qml")));
        QObject *object = component.createWithInitialProperties({{str("host"), QVariant::fromValue(&host)}});
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
    [[nodiscard]] QObject *commands() const { return window->property("commands").value<QObject *>(); }
    [[nodiscard]] QObject *form() const { return window->property("formView").value<QObject *>(); }
    [[nodiscard]] QObject *designer() const { return item(str("baseFormDesigner")); }
    [[nodiscard]] QObject *document() const { return form()->property("designDocument").value<QObject *>(); }
    void call(QObject *target, const char *method, const QVariant &a = {}, const QVariant &b = {}) const
    {
        QMetaObject::invokeMethod(target, method, Q_ARG(QVariant, a), Q_ARG(QVariant, b));
    }
    void open(const char *kind, const char *name, bool design = false)
    {
        host.setSelectedKind(str(kind));
        host.setSelectedName(str(name));
        call(commands(), design ? "designObject" : "openObject", str(kind), str(name));
    }
    void settle(int ms = 300) const { QTest::qWait(ms); }
    [[nodiscard]] bool visible(const char *name) const
    {
        QQuickItem *i = item(str(name));
        return i != nullptr && i->isVisible();
    }
    [[nodiscard]] int controlCount() const
    {
        return designer()->property("controls").toList().size();
    }
    [[nodiscard]] QPointF centre(QQuickItem *i) const { return i->mapToScene(QPointF(i->width() / 2, i->height() / 2)); }
};

#define SCENE(width)                                                                                                   \
    Scene scene(QSize(width, (width) == 1280 ? 800 : 700));                                                            \
    QVERIFY2(scene.window != nullptr, qPrintable(scene.error));                                                        \
    QVERIFY(QTest::qWaitForWindowExposed(scene.window))

} // namespace

class BaseUiM89Test final : public QObject {
    Q_OBJECT
private slots:
    void anM80FormRunsAsPositionedControls()
    {
        for (const int width : {1280, 520}) {
            SCENE(width);
            scene.open("form", "Donor");
            scene.settle(500);
            QVERIFY(scene.visible("formLive_name"));
            const QString first = scene.host.base()->file()->db().scalar(str("SELECT name FROM donors ORDER BY rowid LIMIT 1")).toString();
            QCOMPARE(scene.item(str("formLive_name"))->property("text").toString(), first);
            QVERIFY(scene.visible("formLive_subform1"));
            QTRY_VERIFY(scene.visible("formSubformGrid_subform1"));
            const int gifts = scene.host.base()->file()->db().scalar(str("SELECT count(*) FROM donations WHERE donor_id = 1")).toInt();
            QCOMPARE(scene.host.base()->subformSheet(0)->recordCount(), gifts);
            QVERIFY(scene.visible("formLive_button1"));
            QVERIFY(!scene.visible("formLookups")); // M80's boxes beside the designer are gone
            QindaOffice::Test::saveGrab(scene.window, str("qindabase_m89_form_%1").arg(width));
            // The next record's gifts follow in the subform.
            QMetaObject::invokeMethod(scene.item(str("formNext")), "clicked");
            scene.settle();
            const int second = scene.host.base()->file()->db().scalar(str("SELECT count(*) FROM donations WHERE donor_id = 2")).toInt();
            QCOMPARE(scene.host.base()->subformSheet(0)->recordCount(), second);
            // Design: the toolbox, the canvas and the selected control's sheet.
            scene.open("form", "Donor", true);
            scene.settle(500);
            QVERIFY(scene.visible("formToolbox"));
            QVERIFY(scene.visible("formCanvas"));
            QMetaObject::invokeMethod(scene.designer(), "selectControl", Q_ARG(QVariant, str("button1")));
            scene.settle();
            QVERIFY(scene.visible("formPropertySheet"));
            QCOMPARE(scene.item(str("formControlAction"))->property("text").toString(), str("runMacro"));
            QindaOffice::Test::saveGrab(scene.window, str("qindabase_m89_design_%1").arg(width));
        }
    }

    void aLookupComboBoxShowsTheNameAndSavesTheKey()
    {
        SCENE(1280);
        scene.open("form", "Donation Entry");
        scene.settle(500);
        QQuickItem *combo = scene.item(str("formLive_donor_id"));
        QVERIFY(combo != nullptr && combo->isVisible());
        const qint64 rowid = scene.host.base()->file()->db().scalar(str("SELECT min(rowid) FROM donations")).toLongLong();
        const QString name = scene.host.base()->file()->db().scalar(
            str("SELECT name FROM donors WHERE id = (SELECT donor_id FROM donations WHERE rowid = ?)"), {rowid}).toString();
        QCOMPARE(combo->property("currentText").toString(), name);
        QindaOffice::Test::saveGrab(scene.window, str("qindabase_m89_lookups_1280"));
        // Choosing another donor saves that donor's key with the record.
        const QVariantMap items = scene.host.base()->lookupItems(str("donations"), str("donor_id"));
        QCOMPARE(combo->property("count").toInt(), items.value(str("items")).toStringList().size());
        const QString label = items.value(str("items")).toStringList().at(5);
        const QVariant key = items.value(str("keys")).toList().at(5);
        QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, 5));
        QVariant requested;
        QMetaObject::invokeMethod(scene.designer(), "requestRecordSave", Q_RETURN_ARG(QVariant, requested));
        QVERIFY(requested.toString().size() > 0);
        QTRY_COMPARE(scene.host.base()->file()->db().scalar(str("SELECT donor_id FROM donations WHERE rowid = ?"), {rowid}), key);
        QTRY_COMPARE(scene.item(str("formLive_donor_id"))->property("currentText").toString(), label);
    }

    void aCommandButtonRunsItsMacro()
    {
        SCENE(1280);
        scene.open("form", "Donor");
        scene.settle(500);
        QMetaObject::invokeMethod(scene.item(str("formLive_button1")), "clicked");
        QTRY_COMPARE(scene.form()->property("formName").toString(), str("Donation Entry"));
        QTRY_COMPARE(scene.form()->property("position").toInt(), -1);
    }

    void designGesturesAreOneUndoStepEach()
    {
        SCENE(1280);
        scene.open("form", "Donation Entry", true);
        scene.settle(500);
        QObject *doc = scene.document();
        QVERIFY(doc != nullptr);
        const int before = scene.controlCount();
        // A toolbox click adds one control, one step.
        QQuickItem *addTabs = scene.item(str("formAdd_tabControl"));
        QVERIFY(addTabs != nullptr);
        QVERIFY(addTabs->isVisible() && addTabs->isEnabled());
        QVERIFY(QRectF(QPointF(), scene.window->size()).contains(scene.centre(addTabs)));
        QindaOffice::Test::saveGrab(scene.window, str("qindabase_m89_add_control"));
        QTest::mouseClick(scene.window, Qt::LeftButton, {}, scene.centre(addTabs).toPoint());
        QTRY_VERIFY2(scene.controlCount() == before + 1,
                     qPrintable(scene.designer()->property("requestMessage").toString()));
        QVERIFY(doc->property("canUndo").toBool());
        QCOMPARE(doc->property("undoText").toString(), str("Add tab control"));
        QVERIFY(scene.host.designDirty());
        QMetaObject::invokeMethod(scene.form(), "undo");
        QTRY_COMPARE(scene.controlCount(), before);
        // A drag on the canvas moves a control: one step, snapped to the grid.
        QQuickItem *amount = scene.item(str("formControl_amount"));
        QVERIFY(amount != nullptr);
        const QPoint from = scene.centre(amount).toPoint();
        QTest::mousePress(scene.window, Qt::LeftButton, {}, from);
        for (int step = 1; step <= 8; ++step) {
            QTest::mouseMove(scene.window, from + QPoint(step * 5, step * 2));
            QTest::qWait(16);
        }
        QTest::mouseRelease(scene.window, Qt::LeftButton, {}, from + QPoint(40, 16));
        QTRY_VERIFY(doc->property("undoText").toString().startsWith(str("Move or size")));
        QVariantMap moved;
        for (const QVariant &v : scene.designer()->property("controls").toList()) {
            if (v.toMap().value(str("id")) == str("amount")) moved = v.toMap();
        }
        QCOMPARE(moved.value(str("x")).toInt() % 8, 0);
        QVERIFY(moved.value(str("x")).toInt() > 128);
        QMetaObject::invokeMethod(scene.form(), "undo");
        QTRY_VERIFY(!doc->property("undoText").toString().startsWith(str("Move or size")));
        // A calculated text box shows the engine's value in Form view.
        QVariant id;
        const QVariantMap doubled{{str("name"), str("Doubled")}, {str("expression"), str("=[amount] * 2")}};
        QMetaObject::invokeMethod(scene.form(), "addControl", Q_RETURN_ARG(QVariant, id), Q_ARG(QVariant, str("textBox")),
                                  Q_ARG(QVariant, doubled));
        QVERIFY(id.toString().size() > 0);
        QCOMPARE(scene.controlCount(), before + 1);
        scene.designer()->setProperty("mode", str("edit"));
        scene.settle();
        const qint64 cents = scene.host.base()->file()->db().scalar(str("SELECT amount FROM donations ORDER BY rowid LIMIT 1")).toLongLong();
        QTRY_COMPARE(scene.item(str("formLive_") + id.toString())->property("text").toString(),
                     QindaOffice::Base::formatMoney(cents * 2, 2, str("$")));
        QindaOffice::Test::saveGrab(scene.window, str("qindabase_m89_calculated_1280"));
        // Add Button: a command button running one macro action.
        scene.designer()->setProperty("mode", str("design"));
        QVERIFY(scene.host.actions()->trigger(str("baseFormAddButtonAction")));
        QObject *dialog = scene.window->property("formControlDialog").value<QObject *>();
        QTRY_VERIFY(dialog->property("opened").toBool());
        dialog->setProperty("caption", str("All donations"));
        dialog->setProperty("step", QVariantMap{{str("action"), str("openTable")},
                                                {str("arguments"), QVariantMap{{str("table"), str("donations")}}}});
        scene.settle();
        QindaOffice::Test::saveGrab(scene.window, str("qindabase_m89_buttondialog_1280"));
        QMetaObject::invokeMethod(dialog, "accept");
        QTRY_COMPARE(scene.controlCount(), before + 2);
        const QVariantMap button = scene.designer()->property("controls").toList().last().toMap();
        QCOMPARE(button.value(str("type")).toString(), str("commandButton"));
        QCOMPARE(button.value(str("actionId")).toString(), str("openTable"));
        QCOMPARE(button.value(str("arguments")).toMap().value(str("table")).toString(), str("donations"));
        QCOMPARE(doc->property("undoText").toString(), str("Add command button"));
    }
};

BASE_APP_TEST_MAIN(BaseUiM89Test)
#include "tst_qindabase_ui_m89.moc"
