// SPDX-License-Identifier: GPL-3.0-or-later
// M80 (D-089) through the real window, on the Donors & Donations starter
// seeded with 40 donors and 120 donations: the query designer's crosstab,
// Access SQL view, parameters and make-table plan; validation and masks in
// the table designer; the macro designer and a macro run; a form's lookup
// combo boxes, subform and command button; Group, Sort and Total with the
// engine's exact totals and a PDF; mailing labels and their wizard. At
// 1280 and 520 px; QQO_GRAB_DIR saves PNGs.

#include "base_app_test_support.h"

#include "support/quick_dump.h"
#include "support/ribbon_gate.h"

#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>

#include <memory>

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
    [[nodiscard]] QObject *view(const char *name) const { return window->property(name).value<QObject *>(); }
    void call(QObject *target, const char *method, const QVariant &a = {}, const QVariant &b = {}) const
    {
        QMetaObject::invokeMethod(target, method, Q_ARG(QVariant, a), Q_ARG(QVariant, b));
    }
    void open(const char *kind, const char *name)
    {
        host.setSelectedKind(str(kind));
        host.setSelectedName(str(name));
        call(commands(), "openObject", str(kind), str(name));
    }
    void design(const char *kind, const char *name)
    {
        host.setSelectedKind(str(kind));
        host.setSelectedName(str(name));
        call(commands(), "designObject", str(kind), str(name));
    }
    bool trigger(const char *id) { return host.actions()->trigger(str(id)); }
    void settle(int ms = 250) const { QTest::qWait(ms); }
    [[nodiscard]] bool visible(const char *name) const
    {
        QQuickItem *i = item(str(name));
        return i != nullptr && i->isVisible();
    }
};

QString money(qint64 minor)
{
    return QindaOffice::Base::formatMoney(minor, 2, str("$"));
}

#define SCENE(width)                                                                                                   \
    Scene scene(QSize(width, (width) == 1280 ? 800 : 700));                                                            \
    QVERIFY2(scene.window != nullptr, qPrintable(scene.error));                                                        \
    QVERIFY(QTest::qWaitForWindowExposed(scene.window))

} // namespace

class BaseUiM80Test final : public QObject {
    Q_OBJECT
private slots:
    // Every QindaBase command's icon, and the icons M80's views name, are
    // QindaTK icons (the ribbon gate checks only the layout's overrides).
    void iconsExist()
    {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral(QQO_QINDATK_QML_ROOT));
        QQmlComponent probe(&engine);
        probe.setData("import QtQuick\nimport QindaTK as Tk\nQtObject { function has(n) { return Tk.Icons.has(n) } }",
                      QUrl(str("qrc:/icons-probe.qml")));
        std::unique_ptr<QObject> icons(probe.create());
        QVERIFY2(icons != nullptr, qPrintable(probe.errorString()));
        BaseHost host;
        QStringList names{str("workflow"), str("tag"), str("layers"), str("mouse-pointer-2"), str("list-tree"),
                          str("sigma"), str("grid-3x3"), str("layout-grid"), str("chevrons-up-down"), str("layout-list"),
                          str("wand-sparkles"), str("arrow-up-a-z"), str("arrow-down-z-a"), str("play")};
        for (QindaOffice::Quick::ActionEntry *e : host.actions()->entries()) {
            if (e->id().startsWith(str("base")) && !e->iconName().isEmpty()) {
                names.append(e->iconName());
            }
        }
        QStringList missing;
        for (const QString &name : std::as_const(names)) {
            QVariant has;
            QMetaObject::invokeMethod(icons.get(), "has", Q_RETURN_ARG(QVariant, has), Q_ARG(QVariant, name));
            if (!has.toBool()) {
                missing.append(name);
            }
        }
        QVERIFY2(missing.isEmpty(), qPrintable(missing.join(str(", "))));
    }

    void queryDesignerCrosstabSqlAndMakeTable()
    {
        for (const int width : {1280, 520}) {
            SCENE(width);
            scene.design("query", "Donations by Method and Year");
            scene.settle();
            QObject *view = scene.view("queryDesignView");
            const QVariantMap compiled = view->property("compiled").toMap();
            QVERIFY2(compiled.value(str("ok")).toBool(), qPrintable(compiled.value(str("error")).toString()));
            QVERIFY(compiled.value(str("crosstab")).toBool());
            QVERIFY(scene.visible("queryCrosstab_0"));
            QVERIFY(scene.host.action(str("baseQueryTypeCrosstabAction"))->isChecked());
            QindaOffice::Test::saveGrab(scene.window, str("qindabase_crosstab_%1").arg(width));
            QMetaObject::invokeMethod(view, "showSql");
            scene.settle();
            QVERIFY(scene.item(str("querySql"))->property("text").toString().startsWith(str("TRANSFORM Sum(donations.amount)")));
            QindaOffice::Test::saveGrab(scene.window, str("qindabase_accesssql_%1").arg(width));
            if (width != 1280) {
                continue;
            }
            // Access SQL edited in the SQL view goes back into the grid.
            QString sql = view->property("sqlText").toString();
            sql.replace(str("\nGROUP BY"), str("\nWHERE donations.amount > 100\nGROUP BY"));
            view->setProperty("sqlText", sql);
            QMetaObject::invokeMethod(view, "showDesign");
            scene.settle();
            QCOMPARE(view->property("mode").toString(), str("design"));
            QVERIFY(view->property("compiled").toMap().value(str("sql")).toString().contains(str("10000")));
            // What the grid cannot hold is said, and can stay SQLite SQL.
            QMetaObject::invokeMethod(view, "showSql");
            view->setProperty("sqlText", str("SELECT name FROM donors UNION SELECT name FROM campaigns"));
            QMetaObject::invokeMethod(view, "showDesign");
            scene.settle();
            QCOMPARE(view->property("mode").toString(), str("sql"));
            QVERIFY(view->property("sqlError").toString().contains(str("UNION")));
            QVERIFY(scene.visible("queryKeepSqlite"));
            QindaOffice::Test::saveGrab(scene.window, str("qindabase_sqlrefused_1280"));
            QMetaObject::invokeMethod(view, "keepAsSqlite");
            QCOMPARE(view->property("design").toMap().value(str("kind")).toString(), str("sql"));
            QVERIFY(scene.trigger("baseQueryRunAction"));
            scene.settle();
            QCOMPARE(scene.host.currentView(), str("datasheet"));
            QVERIFY(scene.host.base()->datasheet()->recordCount() >= 40);
            // Query Parameters, on a new query (the SQL one is left unsaved).
            scene.host.setDesignDirty(false);
            scene.trigger("baseQueryDesignAction");
            scene.settle();
            QVERIFY(scene.trigger("baseQueryParametersAction"));
            QObject *parameters = findNamed(scene.window->contentItem(), str("queryParametersDialog"));
            QObject *dialog = parameters != nullptr ? parameters : view->findChild<QObject *>(str("queryParametersDialog"));
            QVERIFY(dialog != nullptr);
            QTRY_VERIFY(dialog->property("opened").toBool());
            scene.settle(300);
            QindaOffice::Test::saveGrab(scene.window, str("qindabase_queryparameters_1280"));
            QMetaObject::invokeMethod(dialog, "reject");
            QTRY_VERIFY(!dialog->property("visible").toBool());
            // A make-table query shows its plan before anything is made.
            QMetaObject::invokeMethod(view, "addTable", Q_ARG(QVariant, str("donations")));
            QMetaObject::invokeMethod(view, "addColumn", Q_ARG(QVariant, str("donations")), Q_ARG(QVariant, str("amount")));
            QVERIFY(scene.trigger("baseQueryTypeMakeTableAction"));
            scene.settle();
            QVERIFY(scene.visible("queryMakeTable"));
            QVERIFY(scene.trigger("baseQueryRunAction"));
            QObject *plan = scene.view("planDialog");
            QTRY_VERIFY(plan->property("opened").toBool());
            scene.settle(300);
            QindaOffice::Test::saveGrab(scene.window, str("qindabase_maketable_1280"));
            QMetaObject::invokeMethod(plan, "accept");
            QTRY_VERIFY(scene.host.base()->tableNames().contains(str("Query table")));
        }
    }

    void tableDesignerValidation()
    {
        for (const int width : {1280, 520}) {
            SCENE(width);
            scene.design("table", "donations");
            QObject *view = scene.view("tableDesignView");
            view->setProperty("current", 3); // amount
            scene.settle();
            QCOMPARE(scene.item(str("fieldValidationRule"))->property("text").toString(), str("> 0"));
            QVERIFY(scene.visible("fieldInputMask"));
            QVERIFY(scene.visible("tableValidationRule"));
            QindaOffice::Test::saveGrab(scene.window, str("qindabase_validation_%1").arg(width));
            if (width != 1280) {
                continue;
            }
            QVERIFY(scene.trigger("baseDesignTestRulesAction"));
            QObject *confirm = scene.window->findChild<QObject *>(str("baseConfirm"));
            QTRY_VERIFY(confirm->property("opened").toBool());
            QVERIFY(confirm->property("text").toString().contains(str("meets its validation rules")));
            QMetaObject::invokeMethod(confirm, "accept");
            // A record rule in the design shows in the plan with its trigger.
            scene.call(view, "setTableProperty", str("validationRule"), str("[given] >= #1/1/2020#"));
            QTRY_VERIFY(view->property("plan").toMap().value(str("sql")).toString().contains(str("CREATE TRIGGER")));
        }
    }

    void macroDesignerAndRun()
    {
        for (const int width : {1280, 520}) {
            SCENE(width);
            scene.design("macro", "New Donation");
            scene.settle();
            QCOMPARE(scene.host.currentView(), str("macroDesign"));
            QVERIFY(scene.host.ribbonContexts().contains(str("macroDesign")));
            QVERIFY(scene.visible("macroAction_0"));
            QVERIFY(scene.trigger("baseMacroAddActionAction"));
            scene.settle();
            QVERIFY(scene.visible("macroAction_1"));
            QindaOffice::Test::saveGrab(scene.window, str("qindabase_macro_%1").arg(width));
            if (width != 1280) {
                continue;
            }
            QObject *view = scene.view("macroDesignView");
            QVERIFY(scene.trigger("baseMacroDeleteActionAction"));
            QCOMPARE(view->property("macro").toMap().value(str("actions")).toList().size(), 1);
            scene.host.setDesignDirty(false);
            // Open on a macro runs it: Donation Entry on a new record.
            scene.open("macro", "New Donation");
            QTRY_COMPARE(scene.host.currentView(), str("form"));
            QObject *form = scene.view("formView");
            QCOMPARE(form->property("formName").toString(), str("Donation Entry"));
            QTRY_COMPARE(form->property("position").toInt(), -1);
        }
    }

    void formLookupsSubformAndButton()
    {
        for (const int width : {1280, 520}) {
            SCENE(width);
            scene.open("form", "Donor");
            scene.settle(400);
            QVERIFY(scene.visible("formSubform_subform1"));
            QVERIFY(scene.visible("formButton_button1"));
            const int gifts = scene.host.base()->file()->db().scalar(str("SELECT count(*) FROM donations WHERE donor_id = 1")).toInt();
            QCOMPARE(scene.host.base()->subformSheet(0)->recordCount(), gifts);
            QindaOffice::Test::saveGrab(scene.window, str("qindabase_formcontrols_%1").arg(width));
            scene.open("form", "Donation Entry");
            scene.settle(400);
            QVERIFY(scene.visible("formCombo_donor_id"));
            QVERIFY(scene.item(str("formCombo_donor_id"))->property("currentText").toString().contains(QLatin1Char(' ')));
            QindaOffice::Test::saveGrab(scene.window, str("qindabase_formlookups_%1").arg(width));
            if (width != 1280) {
                continue;
            }
            // A combo box choice is saved at once, one undo step.
            QObject *form = scene.view("formView");
            const QVariantList choices = scene.host.base()->lookupChoices(str("donations"), str("donor_id"));
            const qint64 rowid = form->property("rowIds").toList().first().toLongLong();
            scene.call(form, "setLookup", str("donor_id"), choices.last().toMap().value(str("value")));
            QCOMPARE(scene.host.base()->file()->db().scalar(str("SELECT donor_id FROM donations WHERE id = ?"), {rowid}),
                     choices.last().toMap().value(str("value")));
            // The Donor form's button runs its macro.
            scene.open("form", "Donor");
            scene.settle(300);
            QMetaObject::invokeMethod(scene.item(str("formButton_button1")), "clicked");
            QTRY_COMPARE(form->property("formName").toString(), str("Donation Entry"));
            QTRY_COMPARE(form->property("position").toInt(), -1);
            // Add Button: the dialog, then one undo step.
            scene.open("form", "Donor");
            scene.settle(300);
            QVERIFY(scene.trigger("baseFormAddButtonAction"));
            QObject *dialog = scene.view("formControlDialog");
            QTRY_VERIFY(dialog->property("opened").toBool());
            dialog->setProperty("caption", str("All donations"));
            dialog->setProperty("step", QVariantMap{{str("action"), str("openTable")},
                                                    {str("arguments"), QVariantMap{{str("table"), str("donations")}}}});
            scene.settle(300);
            QindaOffice::Test::saveGrab(scene.window, str("qindabase_formbuttondialog_1280"));
            QMetaObject::invokeMethod(dialog, "accept");
            QTRY_COMPARE(form->property("controls").toList().size(), 3);
        }
    }

    void reportGroupsTotalsAndPdf()
    {
        for (const int width : {1280, 520}) {
            SCENE(width);
            scene.open("report", "Donations by Donor");
            scene.settle(600);
            QObject *report = scene.view("reportView");
            QObject *output = report->property("output").value<QObject *>();
            QVERIFY2(output->property("built").toBool(), qPrintable(output->property("errorText").toString()));
            QCOMPARE(output->property("rowCount").toInt(), 120);
            QVERIFY(scene.trigger("baseReportGroupSortAction"));
            scene.settle(300);
            QVERIFY(scene.visible("reportGroupsPanel"));
            QindaOffice::Test::saveGrab(scene.window, str("qindabase_reportgroups_%1").arg(width));
            if (width != 1280) {
                continue;
            }
            // The report's totals are the engine's, to the cent.
            const qint64 sum = scene.host.base()->file()->db().scalar(str("SELECT sum(amount) FROM donations")).toLongLong();
            QString footer;
            for (const QVariant &b : output->property("bands").toList()) {
                if (b.toMap().value(str("kind")).toString() == str("reportFooter")) {
                    footer += b.toMap().value(str("text")).toString();
                }
            }
            QVERIFY2(footer.contains(money(sum)), qPrintable(footer));
            // A second level from Group, Sort and Total.
            const int groups = output->property("groupCount").toInt();
            QMetaObject::invokeMethod(scene.item(str("reportGroupsPanel")), "addGroup");
            QTRY_VERIFY(output->property("groupCount").toInt() > groups);
            scene.settle(300);
            QindaOffice::Test::saveGrab(scene.window, str("qindabase_reportgroups2_1280"));
            const QString pdf = scene.dir.filePath(str("report.pdf"));
            QVERIFY(ok(scene.host.exportReportPdf(QUrl::fromLocalFile(pdf))));
            QFile file(pdf);
            QVERIFY(file.open(QIODevice::ReadOnly) && file.read(5) == QByteArray("%PDF-"));
            if (const QString keep = qEnvironmentVariable("QQO_GRAB_DIR"); !keep.isEmpty()) {
                QFile::remove(keep + str("/qindabase_report.pdf"));
                QFile::copy(pdf, keep + str("/qindabase_report.pdf"));
            }
        }
    }

    void labelsAndTheirWizard()
    {
        for (const int width : {1280, 520}) {
            SCENE(width);
            scene.open("report", "Donor Mailing Labels");
            scene.settle(500);
            QObject *report = scene.view("reportView");
            QVERIFY(report->property("labels").toBool());
            QVERIFY(scene.visible("labelPage_0"));
            QVERIFY(scene.visible("labelPage_1"));
            QVERIFY(scene.host.printable());
            QindaOffice::Test::saveGrab(scene.window, str("qindabase_labels_%1").arg(width));
            if (width != 1280) {
                continue;
            }
            const QString pdf = scene.dir.filePath(str("labels.pdf"));
            QVERIFY(ok(scene.host.exportReportPdf(QUrl::fromLocalFile(pdf))));
            QFile file(pdf);
            QVERIFY(file.open(QIODevice::ReadOnly) && file.read(5) == QByteArray("%PDF-"));
            if (const QString keep = qEnvironmentVariable("QQO_GRAB_DIR"); !keep.isEmpty()) {
                QFile::remove(keep + str("/qindabase_labels.pdf"));
                QFile::copy(pdf, keep + str("/qindabase_labels.pdf"));
            }
            QObject *wizard = scene.view("labelWizard");
            scene.call(wizard, "openFor", str("table"), str("donors"));
            QTRY_VERIFY(wizard->property("opened").toBool());
            QTRY_VERIFY(wizard->property("preview").toMap().value(str("ok")).toBool());
            scene.settle(300);
            QindaOffice::Test::saveGrab(scene.window, str("qindabase_labelwizard_1280"));
            QMetaObject::invokeMethod(wizard, "accept");
            QTRY_COMPARE(scene.host.currentName(), str("Labels donors"));
            QVERIFY(report->property("labels").toBool());
        }
    }
};

BASE_APP_TEST_MAIN(BaseUiM80Test)

#include "tst_qindabase_ui_m80.moc"
