// SPDX-License-Identifier: GPL-3.0-or-later
// M69 (D-078) through the real QindaCalc window: Insert ▸ Table over typed
// data, a column typed next to it joining it, a formula filling a
// calculated column, the Total Row toggled on, a retyped header renaming
// every reference — one undo step each; an array formula entered with
// Ctrl+Shift+Enter's braces over a selection, shown in braces, refusing
// edits to part of it; a workbook with a chart sheet opening with the
// chart in the grid's place. QQO_GRAB_DIR saves the windows.

#include "calc_test_support.h"

#include "calc_io/m69_fixtures.h"
#include "standard_actions.h"
#include "support/quick_dump.h"

#include "sheet.h"

#include <QFile>

using namespace CalcTest;
using namespace QindaOffice::Calc;
namespace ActionId = QindaOffice::Quick::ActionId;

class CalcTablesUiTest : public QObject {
    Q_OBJECT

    static void trigger(Scene &scene, const char *id) { scene.host.actions()->trigger(QString::fromLatin1(id)); }
    static void type(Scene &scene, int row, int col, const QString &text)
    {
        scene.grid->setCursorCell(row, col);
        scene.host.commitFormulaBar(text);
    }
    static Value value(Scene &scene, int row, int col) { return scene.doc->workbook()->cellValue(0, row, col); }

private slots:
    void tablesThroughTheWindow()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        const QStringList rows{QStringLiteral("Region,Qty,Price"), QStringLiteral("North,2,10"),
                               QStringLiteral("South,5,4"), QStringLiteral("East,3,7")};
        for (int r = 0; r < rows.size(); ++r) {
            const QStringList cells = rows.at(r).split(QLatin1Char(','));
            for (int c = 0; c < cells.size(); ++c) {
                type(scene, r, c, cells.at(c));
            }
        }
        scene.grid->setCursorCell(1, 1);
        trigger(scene, ActionId::kCalcInsertTable);
        QCOMPARE(scene.doc->workbook()->sheetAt(0).tables().size(), 1);
        QCOMPARE(scene.doc->workbook()->sheetAt(0).tables().first().range, (CellRange{{0, 0}, {3, 2}}));
        QVERIFY(scene.host.statusMessage().contains(QStringLiteral("Table1")));
        // A header typed beside the table makes a new column of it; a
        // formula under it fills the whole column.
        type(scene, 0, 3, QStringLiteral("Total"));
        QCOMPARE(scene.doc->workbook()->sheetAt(0).tables().first().range.bottomRight.col, 3);
        type(scene, 1, 3, QStringLiteral("=[@Qty]*[@Price]"));
        QCOMPARE(value(scene, 2, 3), Value(20.0));
        QCOMPARE(value(scene, 3, 3), Value(21.0));
        scene.grid->setCursorCell(3, 3);
        QCOMPARE(scene.host.formulaText(), QStringLiteral("=[@Qty]*[@Price]"));
        // The Total Row.
        scene.grid->setCursorCell(2, 1);
        QVERIFY(scene.host.actions()->entry(QString::fromLatin1(ActionId::kCalcTableTotalRow))->isEnabled());
        trigger(scene, ActionId::kCalcTableTotalRow);
        QVERIFY(scene.doc->workbook()->sheetAt(0).tables().first().totalsRow);
        QCOMPARE(value(scene, 4, 0), Value(QStringLiteral("Total")));
        QCOMPARE(value(scene, 4, 3), Value(61.0));
        QVERIFY(scene.host.actions()->entry(QString::fromLatin1(ActionId::kCalcTableTotalRow))->isChecked());
        // A retyped header renames the column everywhere.
        type(scene, 6, 0, QStringLiteral("=SUM(Table1[Qty])"));
        type(scene, 0, 1, QStringLiteral("Units"));
        scene.grid->setCursorCell(2, 3);
        QCOMPARE(scene.host.formulaText(), QStringLiteral("=[@Units]*[@Price]"));
        scene.grid->setCursorCell(6, 0);
        QCOMPARE(scene.host.formulaText(), QStringLiteral("=SUM(Table1[Units])"));
        QCOMPARE(value(scene, 6, 0), Value(10.0));
        scene.grid->setCursorCell(2, 2);
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindacalc_m69_table"));
        // One undo step brings the old name back.
        scene.doc->undoStack()->undo();
        scene.grid->setCursorCell(6, 0);
        QCOMPARE(scene.host.formulaText(), QStringLiteral("=SUM(Table1[Qty])"));
    }

    void arrayFormulasThroughTheWindow()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        for (int r = 0; r < 3; ++r) {
            type(scene, r, 0, QString::number(r + 1));
        }
        // Ctrl+Shift+Enter over B1:B3 (the editor wraps the formula in braces).
        scene.grid->setCursorCell(0, 1);
        scene.grid->setCursorCell(2, 1, true);
        scene.host.commitFormulaBar(QStringLiteral("{=A1:A3*10}"));
        QCOMPARE(value(scene, 1, 1), Value(20.0));
        QCOMPARE(scene.doc->workbook()->arrayBlockAt(0, {1, 1}), std::optional<CellRange>(CellRange{{0, 1}, {2, 1}}));
        scene.grid->setCursorCell(1, 1);
        QCOMPARE(scene.host.formulaText(), QStringLiteral("{=A1:A3*10}"));
        // Part of it cannot change; the whole can.
        scene.host.commitFormulaBar(QStringLiteral("5"));
        QVERIFY(scene.host.statusMessage().contains(QStringLiteral("part of an array")));
        QCOMPARE(value(scene, 1, 1), Value(20.0));
        scene.doc->insertRows(1, 1);
        QCOMPARE(value(scene, 2, 0), Value(3.0)); // refused: nothing moved
        scene.host.commitFormulaBar(QStringLiteral("{=A1:A3*100}"));
        QCOMPARE(value(scene, 2, 1), Value(300.0));
        scene.doc->clearCells({{0, 1}, {2, 1}});
        QVERIFY(isBlank(value(scene, 1, 1)));
        QVERIFY(!scene.doc->workbook()->arrayBlockAt(0, {1, 1}).has_value());
    }

    void chartSheetsShowTheirChart()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("chart-sheet.xlsx"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QindaOffice::Test::chartBook().build());
        file.close();
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QVERIFY(scene.host.loadPath(path));
        QTRY_VERIFY_WITH_TIMEOUT(!scene.host.isLoading(), 60000);
        QCOMPARE(scene.host.sheetNames(), (QStringList{QStringLiteral("Q1 Data"), QStringLiteral("Sales Chart"),
                                                        QStringLiteral("Summary")}));
        QVERIFY(scene.host.charts().isEmpty()); // no floating window for it
        QVERIFY(!scene.host.chartSheetActive());
        QQuickItem *view = scene.item(QStringLiteral("chartSheetView"));
        QVERIFY(view != nullptr && !view->isVisible());
        scene.host.setActiveSheet(1);
        QVERIFY(scene.host.chartSheetActive());
        QTRY_VERIFY(view->isVisible());
        const QVariantMap data = scene.host.chartSheetData();
        QCOMPARE(data.value(QStringLiteral("title")).toString(), QStringLiteral("Quarter sales"));
        QCOMPARE(data.value(QStringLiteral("series")).toList().size(), 1);
        QQuickItem *chart = scene.item(QStringLiteral("chartSheetChart"));
        QVERIFY(chart != nullptr && chart->width() > 800);
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindacalc_m69_chart_sheet"));
        // It has no cells to type into.
        scene.doc->applyInput({0, 0}, QStringLiteral("x"));
        QVERIFY(scene.host.statusMessage().contains(QStringLiteral("chart sheet")));
        QVERIFY(scene.doc->workbook()->sheetAt(1).cells().isEmpty());
        scene.host.setActiveSheet(0);
        QTRY_VERIFY(!view->isVisible());
    }

    // The Insert tab's Tables group, wide and squeezed (the ribbon gate in
    // tst_calc_ui checks every item; this is for the eye).
    void insertTabTablesGroup()
    {
        for (const int width : {1280, 520}) {
            Scene scene(QSize(width, 700));
            QVERIFY2(scene.ready(), qPrintable(scene.error));
            QVERIFY(QTest::qWaitForWindowExposed(scene.window));
            QQuickItem *ribbon = scene.item(QStringLiteral("officeRibbon"));
            QVERIFY(ribbon != nullptr);
            QVariant ok;
            QMetaObject::invokeMethod(ribbon, "activateTab", Q_RETURN_ARG(QVariant, ok),
                                      Q_ARG(QVariant, QStringLiteral("insert")));
            QVERIFY(ok.toBool());
            QTest::qWait(250);
            for (const char *id : {ActionId::kCalcInsertTable, ActionId::kCalcTableTotalRow}) {
                QQuickItem *button = findItem(ribbon, QStringLiteral("ribbonAction_") + QString::fromLatin1(id));
                QVERIFY2(button != nullptr && button->isVisible(), id);
            }
            QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindacalc_m69_ribbon_insert_%1").arg(width));
        }
    }
};

CALC_TEST_MAIN(CalcTablesUiTest)
#include "tst_calc_tables_ui.moc"
