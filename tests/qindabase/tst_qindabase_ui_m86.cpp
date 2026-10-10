// SPDX-License-Identifier: GPL-3.0-or-later
// M86 (D-094) through the real window, on the Donors & Donations starter
// seeded with 40 donors and 120 donations: the query wizards from the
// ribbon; a union query in the designer; calculated and value-list fields,
// the Indexes dialog and a table event in the designers; hyperlink cells
// and their dialog; Access XML import, linked tables, their notice and
// manager; Group, Sort and Total past three levels with Force New Page and
// Keep Together on the pages; the application log; AutoExec as a database
// opens. At 1280 and 520 px; QQO_GRAB_DIR saves PNGs.

#include "base_app_test_support.h"

#include "support/quick_dump.h"
#include "support/ribbon_gate.h"

#include <QFile>
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

    explicit Scene(const QSize &size, bool seeded = true)
    {
        if (seeded && (!host.baseDocument()->createAt(dir.filePath(str("Community Donors.qbase")), str("donors"), &error)
                       || !seedDonors(*host.base()->file()))) {
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
    void call(QObject *target, const char *method, const QVariant &a, const QVariant &b) const
    {
        QVERIFY2(QMetaObject::invokeMethod(target, method, Q_ARG(QVariant, a), Q_ARG(QVariant, b)), method);
    }
    void call(QObject *target, const char *method, const QVariant &a) const
    {
        QVERIFY2(QMetaObject::invokeMethod(target, method, Q_ARG(QVariant, a)), method);
    }
    void select(const char *kind, const char *name)
    {
        host.setSelectedKind(str(kind));
        host.setSelectedName(str(name));
    }
    void open(const char *kind, const char *name)
    {
        select(kind, name);
        call(commands(), "openObject", str(kind), str(name));
    }
    void design(const char *kind, const char *name)
    {
        select(kind, name);
        call(commands(), "designObject", str(kind), str(name));
    }
    bool trigger(const char *id) { return host.actions()->trigger(str(id)); }
    void settle(int ms = 250) const { QTest::qWait(ms); }
    [[nodiscard]] bool visible(const char *name) const
    {
        QQuickItem *i = item(str(name));
        return i != nullptr && i->isVisible();
    }
    [[nodiscard]] QVariant scalar(const char *sql) const { return host.base()->file()->db().scalar(str(sql)); }
    void grab(const char *name, int width) const { QindaOffice::Test::saveGrab(window, str("%1_%2").arg(str(name)).arg(width)); }
};

// Waits for a dialog to open, settles it, grabs the window.
bool shown(Scene &scene, QObject *dialog, const char *grab, int width)
{
    if (dialog == nullptr || !QTest::qWaitFor([dialog] { return dialog->property("opened").toBool(); })) {
        return false;
    }
    scene.settle(300);
    scene.grab(grab, width);
    return true;
}

#define SCENE(width)                                                                                                   \
    Scene scene(QSize(width, (width) == 1280 ? 800 : 700));                                                            \
    QVERIFY2(scene.window != nullptr, qPrintable(scene.error));                                                        \
    QVERIFY(QTest::qWaitForWindowExposed(scene.window))

} // namespace

class BaseUiM86Test final : public QObject {
    Q_OBJECT
private slots:
    void queryWizardsFromTheRibbon()
    {
        for (const int width : {1280, 520}) {
            SCENE(width);
            scene.select("table", "donations");
            QVERIFY(scene.trigger("baseCrosstabWizardAction"));
            QObject *wizard = scene.view("queryWizard");
            wizard->setProperty("across", str("given"));
            QVERIFY(shown(scene, wizard, "qindabase_wizard_crosstab", width));
            QCOMPARE(wizard->property("kind").toString(), str("crosstab"));
            QCOMPARE(wizard->property("table").toString(), str("donations"));
            if (width == 1280) {
                wizard->setProperty("rows", QStringList{str("method"), QString(), QString()});
                wizard->setProperty("valueField", str("amount"));
                QMetaObject::invokeMethod(wizard, "accept");
                QTRY_COMPARE(scene.host.currentView(), str("datasheet"));
                QCOMPARE(scene.host.base()->datasheet()->recordCount(), 4);
                QVERIFY(scene.host.base()->queryNames().contains(str("Donations_Crosstab")));
                scene.settle(300);
                scene.grab("qindabase_wizard_crosstab_result", width);
            } else {
                QMetaObject::invokeMethod(wizard, "reject");
            }
            QTRY_VERIFY(!wizard->property("visible").toBool());
            scene.select("table", "donors");
            QVERIFY(scene.trigger("baseFindDuplicatesWizardAction"));
            wizard->setProperty("chosen", QStringList{str("city")});
            QVERIFY(shown(scene, wizard, "qindabase_wizard_duplicates", width));
            QMetaObject::invokeMethod(wizard, "accept");
            QTRY_COMPARE(scene.host.currentName(), str("Find duplicates for Donors"));
            QCOMPARE(scene.host.base()->datasheet()->recordCount(),
                     scene.scalar("SELECT count(*) FROM (SELECT city FROM donors GROUP BY city HAVING count(*) > 1)").toInt());
        }
    }

    void unionQueryInTheDesigner()
    {
        for (const int width : {1280, 520}) {
            SCENE(width);
            BaseService *base = scene.host.base();
            const QVariantMap parsed = base->parseAccessSql(str("SELECT city FROM donors UNION SELECT method FROM donations ORDER BY city;"));
            QVERIFY(ok(base->saveQuery(str("Places and Methods"), parsed.value(str("design")).toMap())));
            scene.design("query", "Places and Methods");
            scene.settle(300);
            QVERIFY(scene.visible("unionPart_0") && scene.visible("unionPart_1"));
            QVERIFY(scene.host.actions()->entry(str("baseQueryTypeUnionAction"))->isChecked());
            scene.grab("qindabase_union", width);
            QObject *view = scene.view("queryDesignView");
            // A part opens in the grid like any select, then goes back.
            scene.call(view, "enterPart", 1);
            scene.settle();
            QVERIFY(scene.visible("queryUnionPart"));
            scene.grab("qindabase_union_part", width);
            QMetaObject::invokeMethod(view, "leavePart");
            scene.settle();
            QCOMPARE(view->property("design").toMap().value(str("parts")).toList().size(), 2);
            QVERIFY(scene.trigger("baseQueryRunAction"));
            QTRY_COMPARE(scene.host.currentView(), str("datasheet"));
            QCOMPARE(base->datasheet()->recordCount(),
                     scene.scalar("SELECT count(*) FROM (SELECT city FROM donors UNION SELECT method FROM donations)").toInt());
        }
    }

    void tableDesignFieldsIndexesAndEvents()
    {
        for (const int width : {1280, 520}) {
            SCENE(width);
            scene.design("table", "donations");
            QObject *view = scene.view("tableDesignView");
            scene.trigger("baseDesignAddFieldAction");
            scene.settle();
            const int added = view->property("current").toInt();
            scene.call(view, "setField", str("name"), str("tier"));
            scene.call(view, "setField", str("lookupValues"), QStringList{str("Gold"), str("Silver"), str("Bronze")});
            scene.call(view, "setField", str("limitToList"), true);
            scene.settle();
            QVERIFY(scene.visible("fieldLookupValues") && scene.visible("fieldLimitToList"));
            scene.item(str("fieldLookupValues"))->forceActiveFocus(); // the pane scrolls to it
            scene.settle();
            scene.grab("qindabase_valuelist", width);
            scene.trigger("baseDesignAddFieldAction");
            scene.call(view, "setField", str("name"), str("with_fee"));
            scene.call(view, "setField", str("type"), str("calculated"));
            scene.call(view, "setField", str("expression"), str("[amount] + 100"));
            scene.settle();
            QVERIFY(scene.visible("fieldExpression"));
            QVERIFY(!scene.visible("fieldDefault"));
            scene.grab("qindabase_calculated", width);
            QVERIFY(added >= 0);
            // The Indexes dialog: donor and date, newest first.
            QVERIFY(scene.trigger("baseDesignIndexesAction"));
            QObject *indexes = view->findChild<QObject *>(str("indexesDialog"));
            QVERIFY(indexes != nullptr);
            QTRY_VERIFY(indexes->property("opened").toBool());
            QMetaObject::invokeMethod(indexes, "addIndex");
            QVariantList list = indexes->property("indexes").toList();
            QVariantMap index = list.first().toMap();
            index.insert(str("name"), str("GiftsByDonor"));
            index.insert(str("fields"), QVariantList{QVariantMap{{str("field"), str("donor_id")}, {str("descending"), false}},
                                                     QVariantMap{{str("field"), str("given")}, {str("descending"), true}}});
            list[0] = index;
            indexes->setProperty("indexes", list);
            scene.settle(300);
            scene.grab("qindabase_indexes", width);
            QMetaObject::invokeMethod(indexes, "accept");
            QTRY_VERIFY(!indexes->property("visible").toBool());
            QCOMPARE(view->property("design").toMap().value(str("indexes")).toList().size(), 1);
            if (width == 1280) {
                QVERIFY(scene.trigger("baseDesignApplyAction"));
                QObject *plan = scene.view("planDialog");
                QTRY_VERIFY(plan->property("opened").toBool());
                QVERIFY(plan->property("plan").toMap().value(str("ok")).toBool());
                QMetaObject::invokeMethod(plan, "accept");
                QTRY_COMPARE(scene.scalar("SELECT count(*) FROM sqlite_schema WHERE name = 'qbi:donations:GiftsByDonor'").toInt(), 1);
                QTRY_VERIFY(!scene.host.designDirty());
                QCOMPARE(scene.scalar("SELECT typeof(with_fee) FROM donations LIMIT 1").toString(), str("integer"));
            } else {
                scene.host.setDesignDirty(false);
                scene.trigger("baseDesignDiscardAction");
            }
            // A table event in the macro designer.
            QVERIFY(scene.host.actions()->entry(str("baseDataMacroAfterInsertAction"))->isEnabled());
            QVERIFY(scene.trigger("baseDataMacroAfterInsertAction"));
            QTRY_COMPARE(scene.host.currentView(), str("macroDesign"));
            QObject *macros = scene.view("macroDesignView");
            QCOMPARE(macros->property("dataMacro").toMap().value(str("event")).toString(), str("afterInsert"));
            QVERIFY(!scene.visible("macroRun"));
            QVERIFY(!scene.host.actions()->entry(str("baseMacroRunAction"))->isEnabled());
            QVERIFY(scene.trigger("baseMacroAddActionAction"));
            scene.settle(300);
            QCOMPARE(macros->property("macro").toMap().value(str("actions")).toList().first().toMap().value(str("action")).toString(),
                     str("setField"));
            scene.grab("qindabase_datamacro", width);
            if (width == 1280) {
                QVariantMap logged{{str("description"), QString()},
                                   {str("actions"), QVariantList{QVariantMap{{str("action"), str("logEvent")},
                                                                            {str("arguments"), QVariantMap{{str("description"), str("New gift")}}}}}}};
                macros->setProperty("macro", logged);
                QVERIFY(QMetaObject::invokeMethod(macros, "save"));
                QTRY_COMPARE(scene.scalar("SELECT count(*) FROM sqlite_schema WHERE name = 'qbm:donations:ai'").toInt(), 1);
                QVERIFY(scene.host.base()->file()->db().exec(str("INSERT INTO donations(donor_id, amount, given, method) "
                                                                 "VALUES(1, 500, '2026-05-01', 'Cash')")).ok);
                QVERIFY(scene.trigger("baseApplicationLogAction"));
                QTRY_COMPARE(scene.host.currentName(), str("Application Log"));
                QCOMPARE(scene.host.base()->datasheet()->recordCount(), 1);
                scene.settle(300);
                scene.grab("qindabase_applicationlog", width);
            }
        }
    }

    void hyperlinkCellsAndTheirDialog()
    {
        for (const int width : {1280, 520}) {
            SCENE(width);
            BaseService *base = scene.host.base();
            QVariantMap design = base->tableDesign(str("donors"));
            QVariantList fields = design.value(str("fields")).toList();
            fields.append(QVariantMap{{str("name"), str("website")}, {str("type"), str("hyperlink")}});
            design.insert(str("fields"), fields);
            QVERIFY(ok(base->applyTableDesign(design)));
            QVERIFY(base->file()->db().exec(str("UPDATE donors SET website = 'Ada''s page#https://example.org/ada#' WHERE id = 1")).ok);
            scene.open("table", "donors");
            scene.settle(200);
            QObject *grid = scene.view("datasheetView")->property("grid").value<QObject *>();
            const int column = int(base->datasheet()->columns().size()) - 1;
            QMetaObject::invokeMethod(grid, "setCurrent", Q_ARG(QVariant, 0), Q_ARG(QVariant, column), Q_ARG(QVariant, false));
            QMetaObject::invokeMethod(grid, "ensureCurrentVisible");
            QTRY_COMPARE(scene.host.hyperlinkTarget().value(str("field")).toString(), str("website"));
            QCOMPARE(base->datasheet()->cellText(str("r1"), str("website")), str("Ada's page"));
            QVERIFY(scene.host.actions()->entry(str("baseOpenHyperlinkAction"))->isEnabled());
            QVERIFY(scene.host.actions()->entry(str("baseEditHyperlinkAction"))->isEnabled());
            QVariant url;
            QMetaObject::invokeMethod(scene.view("accessCommands"), "hyperlinkUrl", Q_RETURN_ARG(QVariant, url),
                                      Q_ARG(QVariant, scene.host.hyperlinkTarget().value(str("text"))));
            QCOMPARE(url.toString(), str("https://example.org/ada"));
            QVERIFY(scene.trigger("baseEditHyperlinkAction"));
            QObject *dialog = scene.view("hyperlinkDialog");
            QVERIFY(shown(scene, dialog, "qindabase_hyperlink", width));
            QCOMPARE(dialog->property("address").toString(), str("https://example.org/ada"));
            dialog->setProperty("display", str("Ada Lovelace"));
            dialog->setProperty("screenTip", str("Her page"));
            QMetaObject::invokeMethod(dialog, "accept");
            QTRY_COMPARE(scene.scalar("SELECT website FROM donors WHERE id = 1").toString(),
                         str("Ada Lovelace#https://example.org/ada##Her page"));
            QTRY_COMPARE(scene.host.hyperlinkTarget().value(str("text")).toString(), str("Ada Lovelace#https://example.org/ada##Her page"));
        }
    }

    void xmlImportAndLinkedTables()
    {
        for (const int width : {1280, 520}) {
            SCENE(width);
            BaseService *base = scene.host.base();
            QVERIFY(ok(base->exportObject(str("table"), str("campaigns"), QUrl::fromLocalFile(scene.dir.filePath(str("campaigns.xml"))),
                                          str("xml"))));
            QObject *import = scene.view("accessImportDialog");
            scene.call(import, "openFor", QUrl::fromLocalFile(scene.dir.filePath(str("campaigns.xml"))), str("xml"));
            QVERIFY(shown(scene, import, "qindabase_xmlimport", width));
            QCOMPARE(import->property("tables").toStringList(), QStringList{str("campaigns")});
            QMetaObject::invokeMethod(import, "accept"); // into campaigns1: the name is taken
            QTRY_VERIFY(import->property("opened").toBool() && import->property("reporting").toBool());
            scene.settle(300);
            scene.grab("qindabase_xmlreport", width);
            QMetaObject::invokeMethod(import, "accept");
            QCOMPARE(scene.scalar("SELECT count(*) FROM sqlite_schema WHERE type = 'table' AND name LIKE 'campaigns%'").toInt(), 2);
            // A link to a CSV file, chosen in the dialog.
            QFile csv(scene.dir.filePath(str("volunteers.csv")));
            QVERIFY(csv.open(QIODevice::WriteOnly));
            csv.write("name,hours\nAda,12\nGrace,3\nAlan,7\n");
            csv.close();
            QObject *link = scene.view("linkTableDialog");
            scene.call(link, "openFor", QUrl::fromLocalFile(csv.fileName()));
            QVERIFY(shown(scene, link, "qindabase_linktable", width));
            QCOMPARE(link->property("kind").toString(), str("csv"));
            QCOMPARE(link->property("name").toString(), str("volunteers"));
            QMetaObject::invokeMethod(link, "accept");
            QTRY_VERIFY(base->linkInfo(str("volunteers")).value(str("linked")).toBool());
            scene.open("table", "volunteers");
            scene.settle(300);
            QVERIFY(scene.visible("datasheetLinkNotice"));
            QVERIFY(!base->datasheet()->editable());
            scene.grab("qindabase_linked", width);
            QVERIFY(scene.trigger("baseLinkedTableManagerAction"));
            QObject *manager = scene.view("linkedTableManager");
            QVERIFY(shown(scene, manager, "qindabase_linkmanager", width));
            QCOMPARE(manager->property("links").toList().size(), 1);
            scene.call(manager, "refresh", str("volunteers"));
            QMetaObject::invokeMethod(manager, "accept");
            QTRY_VERIFY(!manager->property("visible").toBool());
            QVERIFY(scene.trigger("baseRefreshLinksAction"));
            QCOMPARE(scene.scalar("SELECT count(*) FROM volunteers").toInt(), 3);
        }
    }

    void reportLevelsAndPageControl()
    {
        for (const int width : {1280, 520}) {
            SCENE(width);
            scene.open("report", "Donations by Donor");
            scene.settle(600);
            QObject *output = scene.view("reportView")->property("output").value<QObject *>();
            QVERIFY2(output->property("built").toBool(), qPrintable(output->property("errorText").toString()));
            QVERIFY(scene.trigger("baseReportGroupSortAction"));
            scene.settle(300);
            QQuickItem *panel = scene.item(str("reportGroupsPanel"));
            QVERIFY(panel != nullptr && panel->isVisible());
            // Four levels, one more than the toolkit's designer holds.
            const QVariantList four{QVariantMap{{str("field"), str("given")}, {str("interval"), QVariantMap{{str("by"), str("year")}}}},
                                    QVariantMap{{str("field"), str("given")}, {str("interval"), QVariantMap{{str("by"), str("quarter")}}}},
                                    QVariantMap{{str("field"), str("method")}},
                                    QVariantMap{{str("field"), str("Donor")},
                                                {str("header"), QVariantMap{{str("newPage"), str("before")}}}}};
            QVariant done;
            QMetaObject::invokeMethod(panel, "setLevels", Q_RETURN_ARG(QVariant, done), Q_ARG(QVariant, four));
            QVERIFY(done.toBool());
            QTRY_VERIFY(scene.visible("reportGroup_3"));
            QVERIFY(scene.visible("reportLevelsNote"));
            QTRY_VERIFY(output->property("pageBreaks").toInt() > 0);
            scene.settle(300);
            scene.grab("qindabase_reportlevels", width);
            // Keep each donor's gifts on one page.
            const QVariantList whole{QVariantMap{{str("field"), str("Donor")}, {str("keepTogether"), str("wholeGroup")},
                                                 {str("totals"), QVariantList{QVariantMap{{str("field"), str("amount")},
                                                                                          {str("function"), str("sum")}}}}}};
            QMetaObject::invokeMethod(panel, "setLevels", Q_RETURN_ARG(QVariant, done), Q_ARG(QVariant, whole));
            QTRY_VERIFY(output->property("groupsMoved").toInt() > 0);
            QTRY_VERIFY(scene.visible("reportGroupKeep_0"));
            scene.settle(300);
            scene.grab("qindabase_reportkeep", width);
        }
    }

    void autoExecRunsAsTheDatabaseOpens()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(str("Startup.qbase"));
        {
            BaseDocument document;
            BaseService service(&document);
            QString error;
            QVERIFY2(document.createAt(path, str("donors"), &error), qPrintable(error));
            const QVariantMap step{{str("action"), str("openTable")}, {str("arguments"), QVariantMap{{str("table"), str("campaigns")}}}};
            QVERIFY(ok(service.saveMacro(str("AutoExec"), {{str("description"), QString()}, {str("actions"), QVariantList{step}}})));
        }
        Scene scene(QSize(1280, 800), false);
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QVERIFY(scene.host.loadPath(path));
        QTRY_COMPARE(scene.host.currentView(), str("datasheet"));
        QCOMPARE(scene.host.currentName(), str("campaigns"));
        scene.settle(300);
        scene.grab("qindabase_autoexec", 1280);
    }
};

BASE_APP_TEST_MAIN(BaseUiM86Test)

#include "tst_qindabase_ui_m86.moc"
