// SPDX-License-Identifier: GPL-3.0-or-later
// M78 (D-087) through the real QindaCalc window: Data ▸ What-If Analysis —
// Goal Seek through its dialog and status (OK keeps the value as one undo
// step, Cancel puts it back, a set cell without a formula is refused with
// Excel's message), the Scenario Manager (add two, show one, the summary
// sheet, delete), Data Table (a one-variable table that follows its
// formula, refuses a write into part of it and shows {=TABLE(,B1)}), and
// the recovery payload carrying scenarios and tables. QQO_GRAB_DIR saves
// the dialogs at 1280 and 520.

#include "calc_m78_support.h"

#include "sheet.h"

#include <QUndoStack>

using namespace CalcTest;
using namespace CalcTest::M78;
using QindaOffice::Calc::Value;

class CalcWhatIfUiTest : public QObject {
    Q_OBJECT

    static double number(Scene &scene, int row, int col)
    {
        return QindaOffice::Calc::toNumber(cell(scene, 0, row, col)).value_or(-1e300);
    }

private slots:
    void goalSeekThroughItsDialogs()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        type(scene, 0, 0, QStringLiteral("10"));            // A1: the monthly amount
        type(scene, 0, 1, QStringLiteral("=A1*12+5"));      // B1: the year
        const int steps = scene.doc->undoStack()->count();
        scene.grid->setCursorCell(0, 1);
        trigger(scene, ActionId::kCalcGoalSeek);
        QVERIFY(dialogOpened(scene, "goalSeekDialog"));
        QCOMPARE(text(scene, "goalSeekSetCell"), QStringLiteral("$B$1"));
        QVERIFY(setText(scene, "goalSeekTarget", QStringLiteral("305")));
        QVERIFY(setText(scene, "goalSeekChangingCell", QStringLiteral("A1")));
        grab(scene, QStringLiteral("qindacalc_m78_goal_seek_1280"));
        QVERIFY(answer(scene, "goalSeekDialog", true));
        QVERIFY(dialogOpened(scene, "goalSeekStatusDialog"));
        QVERIFY2(text(scene, "goalSeekMessage").contains(QStringLiteral("found a solution")), qPrintable(text(scene, "goalSeekMessage")));
        grab(scene, QStringLiteral("qindacalc_m78_goal_seek_status_1280"));
        QVERIFY(qAbs(number(scene, 0, 0) - 25.0) < 0.001);
        QVERIFY(answer(scene, "goalSeekStatusDialog", true));
        QCOMPARE(scene.doc->undoStack()->count(), steps + 1);
        QCOMPARE(scene.doc->undoStack()->undoText(), QStringLiteral("Goal Seek"));
        // Cancel puts the value back and leaves no step.
        trigger(scene, ActionId::kCalcGoalSeek);
        QVERIFY(dialogOpened(scene, "goalSeekDialog"));
        QVERIFY(setText(scene, "goalSeekTarget", QStringLiteral("125")));
        QVERIFY(setText(scene, "goalSeekChangingCell", QStringLiteral("A1")));
        QVERIFY(answer(scene, "goalSeekDialog", true));
        QVERIFY(dialogOpened(scene, "goalSeekStatusDialog"));
        QVERIFY(qAbs(number(scene, 0, 0) - 10.0) < 0.001);
        QVERIFY(answer(scene, "goalSeekStatusDialog", false));
        QVERIFY(qAbs(number(scene, 0, 0) - 25.0) < 0.001);
        QCOMPARE(scene.doc->undoStack()->count(), steps + 1);
        // A set cell without a formula: Excel's refusal, the dialog stays.
        scene.grid->setCursorCell(0, 0);
        trigger(scene, ActionId::kCalcGoalSeek);
        QVERIFY(dialogOpened(scene, "goalSeekDialog"));
        QVERIFY(setText(scene, "goalSeekTarget", QStringLiteral("1")));
        QVERIFY(setText(scene, "goalSeekChangingCell", QStringLiteral("A1")));
        QMetaObject::invokeMethod(popup(scene, "goalSeekDialog"), "accept");
        QVERIFY(dialogOpened(scene, "goalSeekDialog"));
        QVERIFY2(text(scene, "goalSeekError").contains(QStringLiteral("formula")), qPrintable(text(scene, "goalSeekError")));
        QVERIFY(answer(scene, "goalSeekDialog", false));
    }

    void scenariosThroughTheManager()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        type(scene, 0, 1, QStringLiteral("100"));     // B1 price
        type(scene, 1, 1, QStringLiteral("3"));       // B2 units
        type(scene, 3, 1, QStringLiteral("=B1*B2"));  // B4 revenue
        scene.grid->setCursorCell(0, 1);
        scene.grid->setCursorCell(1, 1, true);
        trigger(scene, ActionId::kCalcScenarioManager);
        QVERIFY(dialogOpened(scene, "scenarioManagerDialog"));
        for (const auto &[name, values] : {std::pair{QStringLiteral("Best"), QVariantList{QStringLiteral("120"), QStringLiteral("5")}},
                                           std::pair{QStringLiteral("Worst"), QVariantList{QStringLiteral("80"), QStringLiteral("2")}}}) {
            QMetaObject::invokeMethod(scene.item(QStringLiteral("scenarioAdd")), "clicked");
            QVERIFY(dialogOpened(scene, "scenarioEditDialog"));
            QCOMPARE(text(scene, "scenarioCells"), QStringLiteral("$B$1,$B$2"));
            QVERIFY(setText(scene, "scenarioName", name));
            popup(scene, "scenarioEditDialog")->setProperty("values", values);
            if (name == QLatin1String("Best")) {
                grab(scene, QStringLiteral("qindacalc_m78_scenario_add_1280"));
            }
            QVERIFY(answer(scene, "scenarioEditDialog", true));
        }
        QCOMPARE(scene.doc->workbook()->sheetAt(0).scenarios().scenarios.size(), 2);
        QTRY_VERIFY(scene.item(QStringLiteral("scenario_1")) != nullptr);
        grab(scene, QStringLiteral("qindacalc_m78_scenario_manager_1280"));
        // Show "Best": its values go into B1:B2, one undo step.
        popup(scene, "scenarioManagerDialog")->setProperty("selected", 0);
        QMetaObject::invokeMethod(scene.item(QStringLiteral("scenarioShow")), "clicked");
        QCOMPARE(cell(scene, 0, 3, 1), Value(600.0));
        scene.doc->undoStack()->undo();
        QCOMPARE(cell(scene, 0, 3, 1), Value(300.0));
        // Summary for B4 on a new sheet, the workbook left as it was.
        QMetaObject::invokeMethod(scene.item(QStringLiteral("scenarioSummary")), "clicked");
        QVERIFY(dialogOpened(scene, "scenarioSummaryDialog"));
        popup(scene, "scenarioSummaryDialog")->setProperty("text", QStringLiteral("$B$4"));
        QVERIFY(answer(scene, "scenarioSummaryDialog", true));
        QCOMPARE(scene.doc->workbook()->sheetCount(), 2);
        QCOMPARE(scene.doc->workbook()->sheetName(0), QStringLiteral("Scenario Summary"));
        QCOMPARE(cell(scene, 1, 3, 1), Value(300.0));
        QCOMPARE(cell(scene, 1, 0, 1), Value(100.0));
        grab(scene, QStringLiteral("qindacalc_m78_scenario_summary_1280"));
        // The recovery payload carries the scenarios.
        SpreadsheetDocument restored;
        QString error;
        QVERIFY2(restored.loadRecovery(scene.doc->recoverySnapshot(), &error), qPrintable(error));
        QCOMPARE(restored.workbook()->sheetAt(1).scenarios().scenarios.size(), 2);
        QCOMPARE(restored.workbook()->sheetAt(1).scenarios().scenarios.at(1).values,
                 (QStringList{QStringLiteral("80"), QStringLiteral("2")}));
    }

    void dataTableFollowsItsFormula()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        type(scene, 0, 1, QStringLiteral("0.05"));    // B1: the rate (column input)
        type(scene, 1, 1, QStringLiteral("1000"));    // B2: the amount
        type(scene, 3, 1, QStringLiteral("=B2*B1"));  // B4: the formula
        type(scene, 4, 0, QStringLiteral("0.03"));    // A5:A7: the rates to try
        type(scene, 5, 0, QStringLiteral("0.04"));
        type(scene, 6, 0, QStringLiteral("0.05"));
        scene.grid->setCursorCell(3, 0);
        scene.grid->setCursorCell(6, 1, true);
        trigger(scene, ActionId::kCalcDataTable);
        QVERIFY(dialogOpened(scene, "dataTableDialog"));
        QVERIFY(setText(scene, "dataTableColumnInput", QStringLiteral("B1")));
        grab(scene, QStringLiteral("qindacalc_m78_data_table_1280"));
        QVERIFY(answer(scene, "dataTableDialog", true));
        QCOMPARE(cell(scene, 0, 4, 1), Value(30.0));
        QCOMPARE(cell(scene, 0, 5, 1), Value(40.0));
        QCOMPARE(cell(scene, 0, 6, 1), Value(50.0));
        QCOMPARE(scene.doc->inputTextAt({5, 1}), QStringLiteral("{=TABLE(,B1)}"));
        // The input cell keeps its value; the table follows the formula's precedents.
        QCOMPARE(cell(scene, 0, 0, 1), Value(0.05));
        type(scene, 1, 1, QStringLiteral("2000"));
        QCOMPARE(cell(scene, 0, 4, 1), Value(60.0));
        // Part of a data table cannot change (Excel's message).
        scene.grid->setCursorCell(5, 1);
        scene.host.commitFormulaBar(QStringLiteral("7"));
        QVERIFY2(scene.host.statusMessage().contains(QStringLiteral("data table")), qPrintable(scene.host.statusMessage()));
        QCOMPARE(cell(scene, 0, 5, 1), Value(80.0));
        // The recovery payload carries the table; undo takes it away.
        SpreadsheetDocument restored;
        QString error;
        QVERIFY2(restored.loadRecovery(scene.doc->recoverySnapshot(), &error), qPrintable(error));
        QCOMPARE(restored.workbook()->sheetAt(0).dataTables().size(), 1);
        QCOMPARE(restored.workbook()->cellValue(0, 6, 1), Value(100.0));
        while (scene.doc->undoStack()->canUndo() && !scene.doc->workbook()->sheetAt(0).dataTables().isEmpty()) {
            scene.doc->undoStack()->undo();
        }
        QVERIFY(scene.doc->workbook()->sheetAt(0).dataTables().isEmpty());
    }

    void dialogsSqueezedTo520()
    {
        Scene scene(QSize(520, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        trigger(scene, ActionId::kCalcGoalSeek);
        QVERIFY(dialogOpened(scene, "goalSeekDialog"));
        grab(scene, QStringLiteral("qindacalc_m78_goal_seek_520"));
        QVERIFY(answer(scene, "goalSeekDialog", false));
        trigger(scene, ActionId::kCalcScenarioManager);
        QVERIFY(dialogOpened(scene, "scenarioManagerDialog"));
        grab(scene, QStringLiteral("qindacalc_m78_scenario_manager_520"));
        QVERIFY(answer(scene, "scenarioManagerDialog", true));
        trigger(scene, ActionId::kCalcDataTable);
        QVERIFY(dialogOpened(scene, "dataTableDialog"));
        grab(scene, QStringLiteral("qindacalc_m78_data_table_520"));
        QVERIFY(answer(scene, "dataTableDialog", false));
    }
};

CALC_TEST_MAIN(CalcWhatIfUiTest)

#include "tst_calc_whatif_ui.moc"
