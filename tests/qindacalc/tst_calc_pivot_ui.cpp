// SPDX-License-Identifier: GPL-3.0-or-later
// M74 (D-083) through the real QindaCalc window: Insert ▸ PivotTable over
// typed data (its dialog, a new sheet before the active one, Excel's
// placeholder, the contextual tabs and the PivotTable Fields pane); fields
// checked in the pane, placed through its drop path and dragged there with
// the mouse — one undo step each; Value Field Settings and Report Layout;
// Refresh after the source changed, with GETPIVOTDATA following; a write
// into the output refused; Show Details; the recovery payload carrying the
// pivots; Excel's pivot edited and saved; an unmodellable one read-only
// with its note. QQO_GRAB_DIR saves the windows at 1280 and 520.

#include "calc_test_support.h"

#include "calc_io/pivot_fixtures.h"
#include "standard_actions.h"
#include "support/quick_dump.h"

#include "pivot_edit.h"
#include "sheet.h"

#include <QFile>

using namespace CalcTest;
using namespace QindaOffice::Calc;
namespace ActionId = QindaOffice::Quick::ActionId;

class CalcPivotUiTest : public QObject {
    Q_OBJECT

    static void trigger(Scene &scene, const char *id) { scene.host.actions()->trigger(QString::fromLatin1(id)); }
    static QObject *popup(Scene &scene, const char *name) { return scene.window->findChild<QObject *>(QString::fromLatin1(name)); }
    // A Tk.Dialog is a Popup: wait for its enter transition before a grab.
    static bool dialogOpened(Scene &scene, const char *name)
    {
        QObject *dialog = popup(scene, name);
        if (dialog == nullptr || !QTest::qWaitFor([dialog] { return dialog->property("opened").toBool(); })) {
            return false;
        }
        QTest::qWait(300);
        return true;
    }
    static void closeDialog(Scene &scene, const char *name, bool accept)
    {
        QMetaObject::invokeMethod(popup(scene, name), accept ? "accept" : "reject");
        QTRY_VERIFY(!popup(scene, name)->property("visible").toBool());
    }
    static Value cell(Scene &scene, int sheet, int row, int col) { return scene.doc->workbook()->cellValue(sheet, row, col); }
    // AGENT-GUARD: never hold a PivotTable& across an undo — the snapshot
    // restore replaces the workbook.
    static const PivotTable &pivot(Scene &scene, int sheet = 0) { return scene.doc->workbook()->sheetAt(sheet).pivots().first(); }
    static QQuickItem *pane(Scene &scene) { return scene.item(QStringLiteral("pivotFieldsPane")); }
    static bool paneShown(Scene &scene)
    {
        return QTest::qWaitFor([&scene] { return pane(scene) != nullptr && pane(scene)->isVisible() && pane(scene)->width() > 0; });
    }
    static QPoint center(QQuickItem *item)
    {
        return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    }
    // A click on a pane control once it is laid out (its delegates come
    // and go with every change to the pivot).
    static bool clickNamed(Scene &scene, const QString &name)
    {
        if (!QTest::qWaitFor([&scene, &name] {
                QQuickItem *item = scene.item(name);
                return item != nullptr && item->isVisible() && item->width() > 0 && item->height() > 0;
            })) {
            return false;
        }
        QTest::qWait(50);
        QTest::mouseClick(scene.window, Qt::LeftButton, Qt::NoModifier, center(scene.item(name)));
        return true;
    }
    // Region, Product, Sales, Qty: tests/calc_engine/pivot_fixture.h's
    // records without their dates.
    static void fillSales(SpreadsheetDocument *doc)
    {
        const QStringList rows{QStringLiteral("Region,Product,Sales,Qty"), QStringLiteral("East,Apple,10,1"),
                               QStringLiteral("West,Pear,20,2"),          QStringLiteral("East,Pear,30,3"),
                               QStringLiteral("North,Apple,5,4"),         QStringLiteral("West,Apple,7,5"),
                               QStringLiteral("East,Apple,8,6")};
        for (int r = 0; r < rows.size(); ++r) {
            const QStringList cells = rows.at(r).split(QLatin1Char(','));
            for (int c = 0; c < cells.size(); ++c) {
                doc->applyInput({r, c}, cells.at(c));
            }
        }
    }
    // Insert ▸ PivotTable through its dialog, from a cursor in the data.
    static bool insertThroughTheDialog(Scene &scene)
    {
        fillSales(scene.doc);
        scene.grid->setCursorCell(1, 1);
        trigger(scene, ActionId::kCalcInsertPivotTable);
        if (!dialogOpened(scene, "pivotCreateDialog")) {
            return false;
        }
        if (scene.item(QStringLiteral("pivotCreateSource"))->property("text").toString() != QStringLiteral("Sheet1!$A$1:$D$7")) {
            qWarning() << scene.item(QStringLiteral("pivotCreateSource"))->property("text");
            return false;
        }
        QObject *dialog = popup(scene, "pivotCreateDialog");
        QMetaObject::invokeMethod(dialog, "accept");
        // The modal dialog's scrim takes clicks until its exit transition ends.
        if (!QTest::qWaitFor([dialog] { return !dialog->property("visible").toBool(); })) {
            return false;
        }
        return scene.doc->workbook()->sheetCount() == 2 && scene.doc->workbook()->sheetAt(0).pivots().size() == 1;
    }
    // The drop path the pane's drags take (PivotFieldsPane.place / addField).
    static void addField(Scene &scene, int field, const char *area, int position = -1)
    {
        QMetaObject::invokeMethod(pane(scene), "addField", Q_ARG(QVariant, field), Q_ARG(QVariant, QString::fromLatin1(area)),
                                  Q_ARG(QVariant, position));
    }
    static void build(Scene &scene, const QString &grabPrefix)
    {
        QVERIFY(insertThroughTheDialog(scene));
        QCOMPARE(scene.host.sheetNames(), (QStringList{QStringLiteral("Sheet2"), QStringLiteral("Sheet1")}));
        QCOMPARE(scene.doc->activeSheet(), 0);
        QVERIFY(scene.host.pivotActive());
        QVERIFY(scene.host.ribbonContexts().contains(QStringLiteral("pivot")));
        QVERIFY(pivot(scene).render && pivot(scene).render->empty); // Excel's placeholder
        QVERIFY(paneShown(scene));
        QindaOffice::Test::saveGrab(scene.window, grabPrefix + QStringLiteral("_empty"));
        // Region checked: text goes to Rows; Sales checked: numbers to Values.
        QVERIFY(clickNamed(scene, QStringLiteral("pivotField_0")));
        QTRY_COMPARE(pivot(scene).rowFields, (QList<qint32>{0}));
        QVERIFY(clickNamed(scene, QStringLiteral("pivotField_2")));
        QTRY_COMPARE(pivot(scene).dataFields.size(), 1);
        QCOMPARE(cell(scene, 0, 2, 0), Value(QStringLiteral("Row Labels")));
        QCOMPARE(cell(scene, 0, 2, 1), Value(QStringLiteral("Sum of Sales")));
        QCOMPARE(cell(scene, 0, 3, 0), Value(QStringLiteral("East")));
        QCOMPARE(cell(scene, 0, 3, 1), Value(48.0));
        QCOMPARE(cell(scene, 0, 6, 0), Value(QStringLiteral("Grand Total")));
        QCOMPARE(cell(scene, 0, 6, 1), Value(80.0));
        // Product through the drop path, onto Columns.
        addField(scene, 1, "columns");
        QCOMPARE(pivot(scene).colFields, (QList<qint32>{1}));
        QCOMPARE(cell(scene, 0, 3, 1), Value(QStringLiteral("Apple")));
        QCOMPARE(cell(scene, 0, 4, 1), Value(18.0));
        QCOMPARE(cell(scene, 0, 7, 3), Value(80.0));
        QindaOffice::Test::saveGrab(scene.window, grabPrefix + QStringLiteral("_compact"));
    }

private slots:
    void insertAndBuildThroughThePane()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        build(scene, QStringLiteral("qindacalc_m74_pivot_1280"));
        if (QTest::currentTestFailed()) {
            return;
        }
        // Qty dragged with the mouse from the field list onto Values: two
        // data fields bring Σ Values onto Columns.
        QTRY_VERIFY(scene.item(QStringLiteral("pivotField_3")) != nullptr
                    && scene.item(QStringLiteral("pivotField_3"))->width() > 0);
        QQuickItem *qty = scene.item(QStringLiteral("pivotField_3"));
        QQuickItem *values = scene.item(QStringLiteral("pivotArea_values"));
        QVERIFY(values != nullptr);
        QTest::qWait(50);
        const QPoint from = center(qty);
        const QPoint to = center(values);
        QTest::mousePress(scene.window, Qt::LeftButton, Qt::NoModifier, from);
        for (int step = 1; step <= 12; ++step) {
            QTest::mouseMove(scene.window, from + (to - from) * step / 12);
            QTest::qWait(16);
        }
        QTest::mouseRelease(scene.window, Qt::LeftButton, Qt::NoModifier, to);
        QTRY_COMPARE(pivot(scene).dataFields.size(), 2);
        QCOMPARE(pivot(scene).dataFields.at(1).name, QStringLiteral("Sum of Qty"));
        QVERIFY(pivot(scene).colFields.contains(kPivotValuesField));
        QTRY_VERIFY(scene.item(QStringLiteral("pivotAreaRow_values_1")) != nullptr);
        // One undo step takes it back out.
        scene.doc->undoStack()->undo();
        QCOMPARE(pivot(scene).dataFields.size(), 1);
        QVERIFY(!pivot(scene).colFields.contains(kPivotValuesField));
        // Report Layout ▸ Tabular, from the Design tab's entry.
        scene.grid->setCursorCell(4, 1);
        trigger(scene, ActionId::kCalcPivotLayoutTabular);
        QCOMPARE(pivot(scene).options.form, PivotForm::Tabular);
        QVERIFY(scene.host.actions()->entry(QString::fromLatin1(ActionId::kCalcPivotLayoutTabular))->isChecked());
        QCOMPARE(cell(scene, 0, 3, 0), Value(QStringLiteral("Region")));
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindacalc_m74_pivot_1280_tabular"));
        // Inside the pivot the grid's menu is the PivotTable menu.
        QCOMPARE(popup(scene, "calcGridContextMenu")->property("node").toMap().value(QStringLiteral("id")).toString(),
                 QStringLiteral("calcPivotContextMenu"));
        // Value Field Settings on a value: Average.
        scene.grid->setCursorCell(4, 1);
        trigger(scene, ActionId::kCalcPivotValueFieldSettings);
        QVERIFY(dialogOpened(scene, "pivotValueFieldDialog"));
        QCOMPARE(scene.item(QStringLiteral("pivotValueSource"))->property("text").toString(), QStringLiteral("Sales"));
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindacalc_m74_value_field_settings_1280"));
        scene.item(QStringLiteral("pivotValueFunction"))->setProperty("currentIndex", 2);
        closeDialog(scene, "pivotValueFieldDialog", true);
        QCOMPARE(pivot(scene).dataFields.first().function, PivotFunction::Average);
        QCOMPARE(pivot(scene).dataFields.first().name, QStringLiteral("Average of Sales"));
        QCOMPARE(cell(scene, 0, 4, 1), Value(9.0)); // East Apple: (10 + 8) / 2
        // Every pivot command is undoable; Insert PivotTable's step removes the sheet.
        while (scene.doc->undoStack()->canUndo() && scene.doc->workbook()->sheetCount() > 1) {
            scene.doc->undoStack()->undo();
        }
        QCOMPARE(scene.doc->workbook()->sheetCount(), 1);
        QCOMPARE(scene.doc->workbook()->pivotCaches.size(), 0);
    }

    void squeezedTo520()
    {
        Scene scene(QSize(520, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        build(scene, QStringLiteral("qindacalc_m74_pivot_520"));
        if (QTest::currentTestFailed()) {
            return;
        }
        scene.grid->setCursorCell(4, 1);
        trigger(scene, ActionId::kCalcPivotValueFieldSettings);
        QVERIFY(dialogOpened(scene, "pivotValueFieldDialog"));
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindacalc_m74_value_field_settings_520"));
        closeDialog(scene, "pivotValueFieldDialog", false);
        trigger(scene, ActionId::kCalcPivotFieldSettings);
        QVERIFY(dialogOpened(scene, "pivotFieldDialog"));
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindacalc_m74_field_settings_520"));
        closeDialog(scene, "pivotFieldDialog", false);
    }

    void refreshRefusalDetailsAndRecovery()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QVERIFY(insertThroughTheDialog(scene));
        QVERIFY(paneShown(scene));
        addField(scene, 0, "rows");
        addField(scene, 2, "values");
        QCOMPARE(cell(scene, 0, 3, 1), Value(48.0));
        scene.grid->setCursorCell(2, 5);
        scene.host.commitFormulaBar(QStringLiteral("=GETPIVOTDATA(\"Sales\",$A$3,\"Region\",\"East\")"));
        QCOMPARE(cell(scene, 0, 2, 5), Value(48.0));
        // The source changes; the pivot waits for Refresh, as in Excel.
        scene.host.setActiveSheet(1);
        scene.doc->applyInput({1, 2}, QStringLiteral("100"));
        scene.host.setActiveSheet(0);
        QCOMPARE(cell(scene, 0, 3, 1), Value(48.0));
        scene.grid->setCursorCell(3, 1);
        trigger(scene, ActionId::kCalcPivotRefresh);
        QCOMPARE(cell(scene, 0, 3, 1), Value(138.0));
        QCOMPARE(cell(scene, 0, 2, 5), Value(138.0));
        // A write into the output is refused with Excel's message.
        scene.grid->setCursorCell(3, 1);
        scene.host.commitFormulaBar(QStringLiteral("5"));
        QVERIFY2(scene.host.statusMessage().contains(QStringLiteral("PivotTable")), qPrintable(scene.host.statusMessage()));
        QCOMPARE(cell(scene, 0, 3, 1), Value(138.0));
        // Show Details: East's records on a new sheet.
        trigger(scene, ActionId::kCalcPivotShowDetails);
        QCOMPARE(scene.doc->workbook()->sheetCount(), 3);
        const int details = scene.doc->activeSheet();
        QCOMPARE(cell(scene, details, 0, 0), Value(QStringLiteral("Region")));
        QCOMPARE(cell(scene, details, 3, 0), Value(QStringLiteral("East")));
        QVERIFY(isBlank(cell(scene, details, 4, 0)));
        // The recovery payload carries the cache and the pivot.
        const QByteArray payload = scene.doc->recoverySnapshot();
        SpreadsheetDocument restored;
        QString error;
        QVERIFY2(restored.loadRecovery(payload, &error), qPrintable(error));
        const qint32 pivotSheet = restored.workbook()->sheetIndex(QStringLiteral("Sheet2"));
        QVERIFY(pivotSheet >= 0);
        QCOMPARE(restored.workbook()->pivotCaches.size(), 1);
        QCOMPARE(restored.workbook()->sheetAt(pivotSheet).pivots().size(), 1);
        const PivotTable &back = restored.workbook()->sheetAt(pivotSheet).pivots().first();
        QCOMPARE(back.rowFields, (QList<qint32>{0}));
        QVERIFY(back.render && !back.render->empty);
        QCOMPARE(restored.workbook()->cellValue(pivotSheet, 3, 1), Value(138.0));
        // Clear All empties it back to the placeholder (the details sheet
        // went in before the pivot's, as in Excel).
        const int sheet = scene.doc->workbook()->sheetIndex(QStringLiteral("Sheet2"));
        QCOMPARE(sheet, details + 1);
        scene.host.setActiveSheet(sheet);
        scene.grid->setCursorCell(3, 1);
        trigger(scene, ActionId::kCalcPivotClear);
        QVERIFY(pivot(scene, sheet).render->empty);
        QVERIFY(isBlank(cell(scene, sheet, 3, 1)));
    }

    void excelPivotEditedAndSaved()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("regions.xlsx"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QindaOffice::Test::regionsByProductBook().build());
        file.close();
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QVERIFY(scene.host.loadPath(path));
        QTRY_VERIFY_WITH_TIMEOUT(!scene.host.isLoading(), 60000);
        scene.host.setActiveSheet(1);
        scene.grid->setCursorCell(4, 1);
        QVERIFY(scene.host.pivotActive());
        QVERIFY(paneShown(scene));
        const QVariantList fields = scene.host.pivotPane().value(QStringLiteral("fields")).toList();
        QCOMPARE(fields.size(), 5);
        QVERIFY(fields.at(0).toMap().value(QStringLiteral("checked")).toBool());
        QVERIFY(clickNamed(scene, QStringLiteral("pivotField_4")));
        QTRY_COMPARE(pivot(scene, 1).dataFields.size(), 2);
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindacalc_m74_excel_pivot_1280"));
        const QString saved = dir.filePath(QStringLiteral("saved.xlsx"));
        QString error;
        QVERIFY2(scene.doc->save(saved, &error), qPrintable(error));
        SpreadsheetDocument reloaded;
        QVERIFY2(reloaded.load(saved, &error), qPrintable(error));
        QCOMPARE(reloaded.workbook()->sheetAt(1).pivots().size(), 1);
        const PivotTable &back = reloaded.workbook()->sheetAt(1).pivots().first();
        QVERIFY(back.modelled);
        QCOMPARE(back.dataFields.size(), 2);
        QCOMPARE(back.dataFields.at(1).name, QStringLiteral("Sum of Qty"));
    }

    void dialogsAndTheAreaMenu()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QVERIFY(insertThroughTheDialog(scene));
        QVERIFY(paneShown(scene));
        addField(scene, 0, "rows");
        addField(scene, 2, "values");
        // Filter on Region's label: North unchecked.
        scene.grid->setCursorCell(3, 0);
        trigger(scene, ActionId::kCalcPivotFilter);
        QVERIFY(dialogOpened(scene, "pivotFilterDialog"));
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindacalc_m74_filter_1280"));
        QVERIFY(clickNamed(scene, QStringLiteral("pivotFilterItem_1")));
        closeDialog(scene, "pivotFilterDialog", true);
        QCOMPARE(cell(scene, 0, 4, 0), Value(QStringLiteral("West")));
        QCOMPARE(cell(scene, 0, 5, 1), Value(75.0)); // Grand Total without North
        // Top 10 ▸ Top 1 item by Sum of Sales.
        trigger(scene, ActionId::kCalcPivotFilter);
        QVERIFY(dialogOpened(scene, "pivotFilterDialog"));
        scene.item(QStringLiteral("pivotFilterKind"))->setProperty("currentIndex", 3);
        scene.item(QStringLiteral("pivotFilterCount"))->setProperty("text", QStringLiteral("1"));
        closeDialog(scene, "pivotFilterDialog", true);
        QCOMPARE(cell(scene, 0, 3, 0), Value(QStringLiteral("East")));
        QCOMPARE(cell(scene, 0, 4, 0), Value(QStringLiteral("Grand Total")));
        trigger(scene, ActionId::kCalcPivotClearFilter);
        QCOMPARE(cell(scene, 0, 6, 1), Value(80.0));
        // Sales on Rows too, grouped by 10 from its smallest value.
        addField(scene, 2, "rows", 0);
        scene.grid->setCursorCell(3, 0);
        trigger(scene, ActionId::kCalcPivotGroup);
        QVERIFY(dialogOpened(scene, "pivotGroupDialog"));
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindacalc_m74_grouping_1280"));
        scene.item(QStringLiteral("pivotGroupBy"))->setProperty("text", QStringLiteral("10"));
        closeDialog(scene, "pivotGroupDialog", true);
        QCOMPARE(cell(scene, 0, 3, 0), Value(QStringLiteral("5-14")));
        QCOMPARE(cell(scene, 0, 3, 1), Value(30.0)); // 10 + 5 + 7 + 8
        trigger(scene, ActionId::kCalcPivotUngroup);
        QCOMPARE(cell(scene, 0, 3, 0), Value(5.0));
        scene.doc->undoStack()->undo(); // the ungroup
        QCOMPARE(cell(scene, 0, 3, 0), Value(QStringLiteral("5-14")));
        // The area menu: Sales leaves Rows, Region goes to Columns.
        QVERIFY(clickNamed(scene, QStringLiteral("pivotAreaRow_rows_0")));
        QTRY_VERIFY(popup(scene, "pivotAreaMenu")->property("opened").toBool());
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindacalc_m74_area_menu_1280"));
        QVERIFY(clickNamed(scene, QStringLiteral("pivotAreaRemove")));
        QTRY_COMPARE(pivot(scene).rowFields, (QList<qint32>{0}));
        QVERIFY(clickNamed(scene, QStringLiteral("pivotAreaRow_rows_0")));
        QTRY_VERIFY(popup(scene, "pivotAreaMenu")->property("opened").toBool());
        QVERIFY(clickNamed(scene, QStringLiteral("pivotAreaToColumns")));
        QTRY_COMPARE(pivot(scene).colFields, (QList<qint32>{0}));
        QVERIFY(pivot(scene).rowFields.isEmpty());
        // PivotTable Options: a name, no grand total column.
        scene.grid->setCursorCell(2, 0);
        trigger(scene, ActionId::kCalcPivotOptions);
        QVERIFY(dialogOpened(scene, "pivotOptionsDialog"));
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindacalc_m74_options_1280"));
        scene.item(QStringLiteral("pivotOptionsName"))->setProperty("text", QStringLiteral("Sales Summary"));
        scene.item(QStringLiteral("pivotOptionsRowGrand"))->setProperty("checked", false);
        closeDialog(scene, "pivotOptionsDialog", true);
        QCOMPARE(pivot(scene).name, QStringLiteral("Sales Summary"));
        QVERIFY(!pivot(scene).options.colGrandTotals);
    }

    void unmodelledPivotsSayWhy()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("calculated.xlsx"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QindaOffice::Test::calculatedFieldBook(false).build());
        file.close();
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QVERIFY(scene.host.loadPath(path));
        QTRY_VERIFY_WITH_TIMEOUT(!scene.host.isLoading(), 60000);
        scene.host.setActiveSheet(1);
        scene.grid->setCursorCell(4, 1);
        QVERIFY(!scene.host.pivotActive());
        QVERIFY2(scene.host.statusMessage().contains(QStringLiteral("read-only")), qPrintable(scene.host.statusMessage()));
        QVERIFY(!scene.host.actions()->entry(QString::fromLatin1(ActionId::kCalcPivotRefresh))->isEnabled());
        QVERIFY(paneShown(scene));
        QTRY_VERIFY(scene.item(QStringLiteral("pivotFieldsReadOnly")) != nullptr
                    && scene.item(QStringLiteral("pivotFieldsReadOnly"))->isVisible());
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindacalc_m74_read_only_1280"));
        // Its cells are still Excel's output: writes are refused.
        scene.host.commitFormulaBar(QStringLiteral("1"));
        QCOMPARE(cell(scene, 1, 4, 1), Value(18.0));
    }
};

CALC_TEST_MAIN(CalcPivotUiTest)
#include "tst_calc_pivot_ui.moc"
