// SPDX-License-Identifier: GPL-3.0-or-later
// QindaBase through the real window (M49): the Donors & Donations starter
// seeded with 40 donors, 4 campaigns and 120 donations. The navigation
// pane and a datasheet with totals, the table designer with a
// relationship, the query designer with a join and totals, a form and a
// report preview render at 1280 and 520 px; the main interactions work;
// the Excel import, the Access report and file fields render (M65); the
// ribbon gate holds; structural fixtures. QQO_GRAB_DIR saves PNGs.

#include "base_app_test_support.h"

#include "support/quick_dump.h"
#include "support/ribbon_gate.h"

#include "base_records.h"

#include <workbook.h>
#include <xlsx_bridge.h>

#include <QBuffer>
#include <QFile>
#include <QImage>
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
        if (seeded) {
            if (!host.baseDocument()->createAt(dir.filePath(str("Community Donors.qbase")), str("donors"), &error)
                || !seedDonors(*host.base()->file())) {
                error = error.isEmpty() ? str("seeding failed") : error;
                return;
            }
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
    void settle(int ms = 250) const { QTest::qWait(ms); }
};

} // namespace

class BaseUiTest final : public QObject {
    Q_OBJECT
private slots:
    void startPageOffersTheTemplates()
    {
        Scene scene(QSize(1280, 800), false);
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        scene.settle();
        QVERIFY(scene.item(str("template_donors")) != nullptr);
        QindaOffice::Test::saveGrab(scene.window, str("qindabase_start_1280"));
        QVERIFY(ok(scene.host.base()->createUntitled(str("volunteers"))));
        scene.settle();
        QVERIFY(scene.item(str("navigationItem_table_volunteers")) != nullptr);
        QVERIFY(scene.host.isDirty()); // untitled until Save As
    }

    void datasheetShowsAndEditsRecords()
    {
        for (const int width : {1280, 520}) {
            Scene scene(QSize(width, width == 1280 ? 800 : 700));
            QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
            QVERIFY(QTest::qWaitForWindowExposed(scene.window));
            scene.open("table", "donations");
            DatasheetModel *sheet = scene.host.base()->datasheet();
            QCOMPARE(sheet->recordCount(), 120);
            QCOMPARE(scene.host.currentView(), str("datasheet"));
            sheet->setTotalsVisible(true);
            scene.settle();
            QVERIFY(scene.item(str("navigationItem_table_donors")) != nullptr);
            QVERIFY(!sheet->totals().value(str("amount")).toMap().value(str("text")).toString().isEmpty());
            QVERIFY(sheet->cellText(str("r1"), str("donor_id")).contains(QLatin1Char(' '))); // a name, not a number
            QindaOffice::Test::saveGrab(scene.window, str("qindabase_datasheet_%1").arg(width));
            if (width != 1280) {
                continue;
            }
            // An edit is one undo step through the engine.
            QVERIFY(ok(sheet->setCell(str("r1"), str("amount"), str("1,234.50"))));
            QCOMPARE(sheet->cellText(str("r1"), str("amount")), str("$1,234.50"));
            QVERIFY(scene.host.actions()->trigger(str("editUndoAction")));
            QTRY_VERIFY(sheet->cellText(str("r1"), str("amount")) != str("$1,234.50"));
            // Filter by selection and the PivotTable.
            QVERIFY(sheet->filterBySelection(str("r1"), str("method"), false));
            QVERIFY(sheet->recordCount() < 120);
            sheet->clearFilters();
            QCOMPARE(sheet->recordCount(), 120);
            QObject *view = scene.view("datasheetView");
            view->setProperty("mode", str("pivot"));
            sheet->preparePivot();
            scene.settle(600);
            QindaOffice::Test::saveGrab(scene.window, str("qindabase_pivot_1280"));
            QVERIFY(sheet->pivotSchema().value(str("fields")).toList().size() >= 7);
        }
    }

    void tableDesignShowsTheRelationship()
    {
        for (const int width : {1280, 520}) {
            Scene scene(QSize(width, width == 1280 ? 800 : 700));
            QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
            QVERIFY(QTest::qWaitForWindowExposed(scene.window));
            scene.design("table", "donations");
            QObject *view = scene.view("tableDesignView");
            view->setProperty("current", 1); // donor_id
            scene.settle();
            QCOMPARE(scene.host.currentView(), str("tableDesign"));
            QVERIFY(scene.host.ribbonContexts().contains(str("tableDesign")));
            QVERIFY(scene.item(str("fieldRelatedTable")) != nullptr);
            // A pending change: the plan and its SQL show before anything runs.
            scene.host.actions()->trigger(str("baseDesignAddFieldAction"));
            view->setProperty("current", 1); // back to donor_id and its relationship
            QTRY_VERIFY(view->property("plan").toMap().value(str("sql")).toString().contains(str("ADD COLUMN")));
            scene.settle();
            QindaOffice::Test::saveGrab(scene.window, str("qindabase_tabledesign_%1").arg(width));
            QVERIFY(scene.host.designDirty());
        }
    }

    void queryDesignJoinsAndTotals()
    {
        for (const int width : {1280, 520}) {
            Scene scene(QSize(width, width == 1280 ? 800 : 700));
            QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
            QVERIFY(QTest::qWaitForWindowExposed(scene.window));
            scene.design("query", "Totals by Donor");
            scene.settle();
            QObject *view = scene.view("queryDesignView");
            const QVariantMap compiled = view->property("compiled").toMap();
            QVERIFY2(compiled.value(str("ok")).toBool(), qPrintable(compiled.value(str("error")).toString()));
            QVERIFY(compiled.value(str("sql")).toString().contains(str("GROUP BY")));
            QVERIFY(scene.item(str("queryJoin_0")) != nullptr);
            QindaOffice::Test::saveGrab(scene.window, str("qindabase_querydesign_%1").arg(width));
            if (width == 1280) {
                // Run it: the datasheet shows one row per donor who gave.
                scene.host.actions()->trigger(str("baseQueryRunAction"));
                scene.settle();
                QCOMPARE(scene.host.currentView(), str("datasheet"));
                QVERIFY(scene.host.base()->datasheet()->recordCount() > 20);
                QindaOffice::Test::saveGrab(scene.window, str("qindabase_queryresult_1280"));
            }
        }
    }

    void formEditsOneRecord()
    {
        for (const int width : {1280, 520}) {
            Scene scene(QSize(width, width == 1280 ? 800 : 700));
            QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
            QVERIFY(QTest::qWaitForWindowExposed(scene.window));
            scene.open("form", "Donation Entry");
            scene.settle(400);
            QCOMPARE(scene.host.currentView(), str("form"));
            QObject *view = scene.view("formView");
            QCOMPARE(view->property("rowIds").toList().size(), 120);
            QVERIFY(scene.item(str("formValue_amount")) != nullptr);
            QindaOffice::Test::saveGrab(scene.window, str("qindabase_form_%1").arg(width));
        }
    }

    void reportPreviews()
    {
        for (const int width : {1280, 520}) {
            Scene scene(QSize(width, width == 1280 ? 800 : 700));
            QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
            QVERIFY(QTest::qWaitForWindowExposed(scene.window));
            scene.open("report", "Donations by Donor");
            scene.settle(600);
            QCOMPARE(scene.host.currentView(), str("report"));
            QObject *output = scene.view("reportView")->property("output").value<QObject *>();
            QVERIFY(output != nullptr);
            QVERIFY2(output->property("built").toBool(), qPrintable(output->property("errorText").toString()));
            QCOMPARE(output->property("rowCount").toInt(), 120);
            QVERIFY(scene.host.printable());
            QindaOffice::Test::saveGrab(scene.window, str("qindabase_report_%1").arg(width));
            if (width == 1280) {
                scene.view("reportView")->setProperty("designing", true);
                scene.settle(400);
                QindaOffice::Test::saveGrab(scene.window, str("qindabase_reportdesign_1280"));
                const QString pdf = scene.dir.filePath(str("report.pdf"));
                QVERIFY(ok(scene.host.exportReportPdf(QUrl::fromLocalFile(pdf))));
                QVERIFY(QFileInfo(pdf).size() > 1000);
            }
        }
    }

    void relationshipsAndSql()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        scene.host.actions()->trigger(str("baseRelationshipsAction"));
        scene.settle(400);
        QCOMPARE(scene.host.currentView(), str("relationships"));
        QVERIFY(scene.item(str("relationship_1")) != nullptr);
        QindaOffice::Test::saveGrab(scene.window, str("qindabase_relationships_1280"));
        scene.host.actions()->trigger(str("baseSqlConsoleAction"));
        scene.settle(300);
        QCOMPARE(scene.host.currentView(), str("sql"));
    }

    void dialogsRender()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        // The start page of an open database.
        scene.settle();
        QindaOffice::Test::saveGrab(scene.window, str("qindabase_home_1280"));
        // Apply a table change: the plan dialog shows the SQL first.
        scene.design("table", "donations");
        scene.host.actions()->trigger(str("baseDesignAddFieldAction"));
        scene.host.actions()->trigger(str("baseDesignApplyAction"));
        QObject *plan = scene.view("planDialog");
        QTRY_VERIFY(plan->property("opened").toBool());
        scene.settle(300);
        QindaOffice::Test::saveGrab(scene.window, str("qindabase_plandialog_1280"));
        QMetaObject::invokeMethod(plan, "reject");
        QTRY_VERIFY(!plan->property("visible").toBool());
        scene.host.actions()->trigger(str("baseDesignDiscardAction"));
        // CSV import: the proposal with detected types.
        const QString csv = scene.dir.filePath(str("volunteers.csv"));
        QFile file(csv);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("Name,Email,Phone,Hours,Joined,Active\nAda Lovelace,ada@example.org,503-555-0101,12.5,2026-01-05,yes\n"
                   "Grace Hopper,grace@example.org,503-555-0102,3,2/1/2026,no\nAlan Turing,alan@example.org,,7.25,2026-02-11,yes\n");
        file.close();
        QObject *import = scene.view("importCsvDialog");
        QMetaObject::invokeMethod(import, "openFor", Q_ARG(QVariant, QUrl::fromLocalFile(csv)), Q_ARG(QVariant, str("csv")));
        QTRY_VERIFY(import->property("opened").toBool());
        scene.settle(300);
        QindaOffice::Test::saveGrab(scene.window, str("qindabase_import_1280"));
        QMetaObject::invokeMethod(import, "reject");
        QTRY_VERIFY(!import->property("visible").toBool());
        // The report wizard.
        QObject *wizard = scene.view("reportWizard");
        scene.call(wizard, "openFor", str("table"), str("donations"));
        QTRY_VERIFY(wizard->property("opened").toBool());
        wizard->setProperty("groupField", str("method"));
        wizard->setProperty("totalField", str("amount"));
        scene.settle(300);
        QindaOffice::Test::saveGrab(scene.window, str("qindabase_reportwizard_1280"));
        QMetaObject::invokeMethod(wizard, "accept");
        scene.settle(600);
        QCOMPARE(scene.host.currentView(), str("report"));
        QindaOffice::Test::saveGrab(scene.window, str("qindabase_wizardreport_1280"));
        // The SQL console with rows.
        scene.host.actions()->trigger(str("baseSqlConsoleAction"));
        QQuickItem *editor = scene.item(str("sqlEditor"));
        QVERIFY(editor != nullptr);
        editor->setProperty("text", str("SELECT method, count(*) AS gifts, sum(amount) / 100.0 AS total FROM donations GROUP BY method ORDER BY total DESC"));
        QMetaObject::invokeMethod(scene.view("sqlView"), "run", Q_ARG(QVariant, QVariantMap()));
        scene.settle(300);
        QCOMPARE(scene.item(str("sqlResults"))->property("count").toInt(), 4);
        QindaOffice::Test::saveGrab(scene.window, str("qindabase_sql_1280"));
    }

    void excelAccessAndFilesRender()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        // Excel import: the sheet chooser, cell-typed columns, the first rows.
        QindaOffice::Calc::Workbook book;
        book.addSheet(str("Volunteers"));
        QindaOffice::Calc::CellStyle dateStyle;
        dateStyle.numberFormat = str("m/d/yyyy");
        const qint32 date = book.addStyle(dateStyle);
        const QStringList header{str("Name"), str("Hours"), str("Joined"), str("Active")};
        for (int c = 0; c < header.size(); ++c) {
            book.setCellValue(0, {0, c}, QindaOffice::Calc::Value(header.at(c)));
        }
        const char *const names[] = {"Ada Lovelace", "Grace Hopper", "Alan Turing"};
        for (int r = 1; r <= 3; ++r) {
            book.setCellValue(0, {r, 0}, QindaOffice::Calc::Value(str(names[r - 1])));
            book.setCellValue(0, {r, 1}, QindaOffice::Calc::Value(2.5 * r));
            book.setCellValue(0, {r, 2}, QindaOffice::Calc::Value(46020.0 + r * 9));
            book.sheetAt(0).setStyleIdAt({r, 2}, date);
            book.setCellValue(0, {r, 3}, QindaOffice::Calc::Value(r != 2));
        }
        const QString xlsx = scene.dir.filePath(str("volunteers.xlsx"));
        QVERIFY(QindaOffice::Calc::IO::writeXlsxFile(book, xlsx));
        QObject *import = scene.view("importCsvDialog");
        QMetaObject::invokeMethod(import, "openFor", Q_ARG(QVariant, QUrl::fromLocalFile(xlsx)), Q_ARG(QVariant, str("xlsx")));
        QTRY_VERIFY(import->property("opened").toBool());
        scene.settle(300);
        QCOMPARE(import->property("sheet").toString(), str("Volunteers"));
        QindaOffice::Test::saveGrab(scene.window, str("qindabase_excelimport_1280"));
        QMetaObject::invokeMethod(import, "reject");
        QTRY_VERIFY(!import->property("visible").toBool());
        // The Access import report.
        QObject *access = scene.view("accessImportDialog");
        const QVariantMap report{
            {str("ok"), true},
            {str("tables"), QVariantList{QVariantMap{{str("source"), str("Members")}, {str("table"), str("Members")}, {str("rows"), 3}},
                                         QVariantMap{{str("source"), str("Dues")}, {str("table"), str("Dues")}, {str("rows"), 3}}}},
            {str("relationships"), QStringList{str("Dues.MemberID → Members.MemberID")}},
            {str("queries"), QStringList{str("Active members"), str("Big dues")}},
            {str("notes"), QStringList{str("Events.Fee holds fractions of a cent, so it became a decimal number.")}},
            {str("skipped"), QStringList{str("Members.Badges: an attachment or multi-valued field, which mdb-tools cannot read."),
                                         str("Query Overdue: it joins 2 tables, and mdb-queries does not give the join.")}}};
        QMetaObject::invokeMethod(access, "showReport", Q_ARG(QVariant, report));
        QTRY_VERIFY(access->property("opened").toBool());
        scene.settle(300);
        QindaOffice::Test::saveGrab(scene.window, str("qindabase_accessreport_1280"));
        QMetaObject::invokeMethod(access, "accept");
        QTRY_VERIFY(!access->property("visible").toBool());
        // A file field: the datasheet's panel shows the picture; the form lists it.
        QindaOffice::Base::BaseFile &file = *scene.host.base()->file();
        QVERIFY(file.db().exec(str("CREATE TABLE badges(id INTEGER PRIMARY KEY AUTOINCREMENT, holder TEXT, photo FILE)")).ok);
        QVERIFY(file.db().run(str("INSERT INTO badges(holder) VALUES('Ada Lovelace'), ('Grace Hopper')")).ok);
        QImage picture(160, 120, QImage::Format_RGB32);
        picture.fill(QColor(220, 140, 40));
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        picture.save(&buffer, "PNG");
        QVERIFY(QindaOffice::Base::Records::setFile(file, str("badges"), 1, str("photo"), png, nullptr).ok);
        emit scene.host.base()->objectsChanged();
        scene.open("table", "badges");
        scene.settle(200); // a fresh source puts the cursor on its first cell first
        QObject *grid = scene.view("datasheetView")->property("grid").value<QObject *>();
        QMetaObject::invokeMethod(grid, "setCurrent", Q_ARG(QVariant, 0), Q_ARG(QVariant, 2), Q_ARG(QVariant, false));
        QTRY_VERIFY(scene.host.fileTarget().value(str("hasFile")).toBool());
        QTRY_COMPARE(scene.item(str("datasheetFilePanel"))->property("visible").toBool(), true);
        QTRY_COMPARE(findNamed(scene.item(str("datasheetFilePanel")), str("fileFieldThumbnail"))->property("loadState").toString(),
                     str("ready"));
        QVERIFY(scene.host.actions()->entry(str("baseFileSaveAction"))->isEnabled());
        scene.settle(200);
        QindaOffice::Test::saveGrab(scene.window, str("qindabase_filefield_1280"));
        QVERIFY(ok(scene.host.base()->createForm(str("Badge"), str("badges"), str("Badge"), {})));
        scene.open("form", "Badge");
        scene.settle(400);
        QVERIFY(scene.item(str("formFiles")) != nullptr && scene.item(str("formFiles"))->isVisible());
        QTRY_VERIFY(scene.host.fileTarget().value(str("hasFile")).toBool());
        QindaOffice::Test::saveGrab(scene.window, str("qindabase_formfiles_1280"));
    }

    void ribbonGateHolds()
    {
        for (const int width : {1280, 520}) {
            Scene scene(QSize(width, 800));
            QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
            QVERIFY(QTest::qWaitForWindowExposed(scene.window));
            scene.settle(150);
            const QStringList problems = QindaOffice::Test::ribbonProblems(
                scene.window, scene.host.menuTree(),
                // Replace: records are edited cell by cell (Find + type);
                // Save As lives on File ▸ Document (StandardRibbon) already.
                {str("editReplaceAction")});
            QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
        }
    }

    void structuralFixtures()
    {
        for (const int width : {1280, 520}) {
            Scene scene(QSize(width, width == 1280 ? 800 : 700));
            QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
            QVERIFY(QTest::qWaitForWindowExposed(scene.window));
            scene.open("table", "donors");
            scene.settle(200);
            QString diff;
            const QString path = QStringLiteral(QQO_FIXTURE_DIR "/qindabase_%1.txt").arg(width);
            QVERIFY2(QindaOffice::Test::matchesFixture(scene.window, path, &diff), qPrintable(diff));
        }
    }
};

BASE_APP_TEST_MAIN(BaseUiTest)

#include "tst_qindabase_ui.moc"
