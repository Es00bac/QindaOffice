// SPDX-License-Identifier: GPL-3.0-or-later
// M90 (D-098) through the real QindaCalc window: Data ▸ Analyze ▸ Solver —
// Solver Parameters prefilled from the cursor, a constraint added through
// Add Constraint, Simplex LP chosen, Solve on the worker with the progress
// dialog, Solver Results with the solution showing behind it; Keep with the
// Answer Report is ONE undo step (values, the model's solver_* names and
// the report sheet) and undo takes all of it back; Restore keeps only the
// model; Close keeps the parameters ("Solver Parameters"); a model Excel
// would refuse reopens the dialog with Excel's message; Options round
// trips; the recovery payload carries the model. QQO_GRAB_DIR saves the
// dialogs at 1280 and 520.

#include "calc_m78_support.h"

#include "sheet.h"
#include "solver_model.h"

#include <QUndoStack>

using namespace CalcTest;
using namespace CalcTest::M78;
using QindaOffice::Calc::Value;
using QindaOffice::Calc::solverModelOf;

class CalcSolverUiTest : public QObject {
    Q_OBJECT

    static double number(Scene &scene, int sheet, int row, int col)
    {
        return QindaOffice::Calc::toNumber(cell(scene, sheet, row, col)).value_or(-1e300);
    }
    // Maximize E2 = 3·B2 + 5·C2 with D4:D6 <= E4:E6 (x = 2, y = 6).
    static void productMix(Scene &scene)
    {
        type(scene, 0, 0, QStringLiteral("Units"));
        type(scene, 0, 1, QStringLiteral("Doors"));
        type(scene, 0, 2, QStringLiteral("Windows"));
        type(scene, 0, 4, QStringLiteral("Profit"));
        type(scene, 1, 4, QStringLiteral("=3*B2+5*C2"));
        type(scene, 3, 3, QStringLiteral("=B2"));
        type(scene, 4, 3, QStringLiteral("=2*C2"));
        type(scene, 5, 3, QStringLiteral("=3*B2+2*C2"));
        type(scene, 3, 4, QStringLiteral("4"));
        type(scene, 4, 4, QStringLiteral("12"));
        type(scene, 5, 4, QStringLiteral("18"));
    }
    static void fillParameters(Scene &scene)
    {
        QVERIFY(setText(scene, "solverVariables", QStringLiteral("$B$2:$C$2")));
        QMetaObject::invokeMethod(scene.item(QStringLiteral("solverAdd")), "clicked");
        QVERIFY(dialogOpened(scene, "solverConstraintDialog"));
        QVERIFY(setText(scene, "solverConstraintLeft", QStringLiteral("$D$4:$D$6")));
        QVERIFY(setText(scene, "solverConstraintRight", QStringLiteral("$E$4:$E$6")));
        QVERIFY(answer(scene, "solverConstraintDialog", true));
        QTRY_VERIFY(scene.item(QStringLiteral("solverConstraint_0")) != nullptr);
        QCOMPARE(scene.item(QStringLiteral("solverConstraint_0"))->property("text").toString(),
                 QStringLiteral("$D$4:$D$6 <= $E$4:$E$6"));
        scene.item(QStringLiteral("solverMethod"))->setProperty("currentIndex", 1); // Simplex LP
    }

private slots:
    void solveKeepAndReport()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        productMix(scene);
        const int steps = scene.doc->undoStack()->count();
        scene.grid->setCursorCell(1, 4);
        trigger(scene, ActionId::kCalcSolver);
        QVERIFY(dialogOpened(scene, "solverDialog"));
        QCOMPARE(text(scene, "solverObjective"), QStringLiteral("$E$2"));
        fillParameters(scene);
        grab(scene, QStringLiteral("qindacalc_m90_solver_1280"));
        QVERIFY(answer(scene, "solverDialog", true));
        QVERIFY(dialogOpened(scene, "solverResultsDialog"));
        QVERIFY2(text(scene, "solverResultMessage").startsWith(QStringLiteral("Solver found a solution")),
                 qPrintable(text(scene, "solverResultMessage")));
        QVERIFY(!scene.host.solver()->running());
        // The solution shows behind Solver Results.
        QCOMPARE(number(scene, 0, 1, 1), 2.0);
        QCOMPARE(number(scene, 0, 1, 2), 6.0);
        QCOMPARE(number(scene, 0, 1, 4), 36.0);
        scene.item(QStringLiteral("solverAnswerReport"))->setProperty("checked", true);
        grab(scene, QStringLiteral("qindacalc_m90_solver_results_1280"));
        QVERIFY(answer(scene, "solverResultsDialog", true));
        QCOMPARE(scene.doc->undoStack()->count(), steps + 1);
        QCOMPARE(scene.doc->undoStack()->undoText(), QStringLiteral("Solver"));
        QCOMPARE(scene.doc->workbook()->sheetCount(), 2);
        QCOMPARE(scene.doc->workbook()->sheetName(0), QStringLiteral("Answer Report 1"));
        QCOMPARE(scene.host.sheetNames().first(), QStringLiteral("Answer Report 1"));
        QCOMPARE(number(scene, 1, 1, 4), 36.0);
        QVERIFY(solverModelOf(*scene.doc->workbook(), 1).has_value());
        QCOMPARE(solverModelOf(*scene.doc->workbook(), 1)->variables, QStringLiteral("Sheet1!$B$2:$C$2"));
        grab(scene, QStringLiteral("qindacalc_m90_answer_report_1280"));
        // The recovery payload carries the model.
        SpreadsheetDocument restored;
        QString error;
        QVERIFY2(restored.loadRecovery(scene.doc->recoverySnapshot(), &error), qPrintable(error));
        QCOMPARE(solverModelOf(*restored.workbook(), 1), solverModelOf(*scene.doc->workbook(), 1));
        // One undo takes back the values, the model and the report.
        scene.doc->undoStack()->undo();
        QCOMPARE(scene.doc->workbook()->sheetCount(), 1);
        QVERIFY(isBlank(cell(scene, 0, 1, 1)));
        QVERIFY(!solverModelOf(*scene.doc->workbook(), 0).has_value());
        // The model comes back with Solver: the dialog opens on it.
        scene.doc->undoStack()->redo();
        scene.host.setActiveSheet(1);
        trigger(scene, ActionId::kCalcSolver);
        QVERIFY(dialogOpened(scene, "solverDialog"));
        QCOMPARE(text(scene, "solverVariables"), QStringLiteral("$B$2:$C$2"));
        QCOMPARE(scene.item(QStringLiteral("solverMethod"))->property("currentIndex").toInt(), 1);
        QVERIFY(answer(scene, "solverDialog", false));
    }

    void restoreRefusalsAndClose()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        productMix(scene);
        scene.grid->setCursorCell(1, 4);
        // A range as the objective: Excel's refusal, the dialog reopens.
        trigger(scene, ActionId::kCalcSolver);
        QVERIFY(dialogOpened(scene, "solverDialog"));
        fillParameters(scene);
        QVERIFY(setText(scene, "solverObjective", QStringLiteral("$E$2:$E$3")));
        QMetaObject::invokeMethod(popup(scene, "solverDialog"), "accept");
        QVERIFY(dialogOpened(scene, "solverDialog"));
        QCOMPARE(text(scene, "solverError"), QStringLiteral("Objective Cell must be a single cell on the active sheet."));
        // Restore Original Values: the cells go back, the model stays (one step).
        QVERIFY(setText(scene, "solverObjective", QStringLiteral("$E$2")));
        const int steps = scene.doc->undoStack()->count();
        QVERIFY(answer(scene, "solverDialog", true));
        QVERIFY(dialogOpened(scene, "solverResultsDialog"));
        QCOMPARE(number(scene, 0, 1, 4), 36.0);
        scene.item(QStringLiteral("solverRestore"))->setProperty("checked", true);
        scene.item(QStringLiteral("solverKeep"))->setProperty("checked", false);
        QVERIFY(answer(scene, "solverResultsDialog", true));
        QVERIFY(isBlank(cell(scene, 0, 1, 1)));
        QCOMPARE(number(scene, 0, 1, 4), 0.0);
        QCOMPARE(scene.doc->undoStack()->count(), steps + 1);
        QVERIFY(solverModelOf(*scene.doc->workbook(), 0).has_value());
        // Close keeps edited parameters as their own step; Options round trip.
        trigger(scene, ActionId::kCalcSolver);
        QVERIFY(dialogOpened(scene, "solverDialog"));
        QMetaObject::invokeMethod(scene.item(QStringLiteral("solverOptions")), "clicked");
        QVERIFY(dialogOpened(scene, "solverOptionsDialog"));
        QVERIFY(setText(scene, "solverMaxTime", QStringLiteral("45")));
        grab(scene, QStringLiteral("qindacalc_m90_solver_options_1280"));
        QVERIFY(answer(scene, "solverOptionsDialog", true));
        scene.item(QStringLiteral("solverNonNegative"))->setProperty("checked", false);
        QVERIFY(answer(scene, "solverDialog", false));
        QCOMPARE(scene.doc->undoStack()->count(), steps + 2);
        QCOMPARE(scene.doc->undoStack()->undoText(), QStringLiteral("Solver Parameters"));
        const auto model = solverModelOf(*scene.doc->workbook(), 0);
        QVERIFY(model.has_value());
        QVERIFY(!model->nonNegative);
        QCOMPARE(model->options.maxTime, 45.0);
        // Closing again without a change adds no step.
        trigger(scene, ActionId::kCalcSolver);
        QVERIFY(dialogOpened(scene, "solverDialog"));
        QVERIFY(answer(scene, "solverDialog", false));
        QCOMPARE(scene.doc->undoStack()->count(), steps + 2);
    }

    void interveningEditDiscardsWorkerResult()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        productMix(scene);
        QVariantMap parameters = scene.host.solver()->defaultParameters();
        parameters.insert(QStringLiteral("objective"), QStringLiteral("E2"));
        parameters.insert(QStringLiteral("variables"), QStringLiteral("B2:C2"));
        parameters.insert(QStringLiteral("method"), 2);
        parameters.insert(QStringLiteral("constraints"), QVariantList{QVariantMap{
            {QStringLiteral("left"), QStringLiteral("D4:D6")}, {QStringLiteral("relation"), QStringLiteral("<=")},
            {QStringLiteral("right"), QStringLiteral("E4:E6")}}});
        QVERIFY(scene.host.solver()->solve(parameters).isEmpty());
        type(scene, 1, 1, QStringLiteral("99"));
        QTRY_VERIFY(!scene.host.solver()->running());
        QCOMPARE(number(scene, 0, 1, 1), 99.0);
        scene.host.solver()->finish(true, false);
        QCOMPARE(number(scene, 0, 1, 1), 99.0);
    }

    void dialogsSqueezedTo520()
    {
        Scene scene(QSize(520, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        productMix(scene);
        scene.grid->setCursorCell(1, 4);
        trigger(scene, ActionId::kCalcSolver);
        QVERIFY(dialogOpened(scene, "solverDialog"));
        fillParameters(scene);
        grab(scene, QStringLiteral("qindacalc_m90_solver_520"));
        QMetaObject::invokeMethod(scene.item(QStringLiteral("solverAdd")), "clicked");
        QVERIFY(dialogOpened(scene, "solverConstraintDialog"));
        grab(scene, QStringLiteral("qindacalc_m90_solver_constraint_520"));
        QVERIFY(answer(scene, "solverConstraintDialog", false));
        QVERIFY(answer(scene, "solverDialog", true));
        QVERIFY(dialogOpened(scene, "solverResultsDialog"));
        grab(scene, QStringLiteral("qindacalc_m90_solver_results_520"));
        QVERIFY(answer(scene, "solverResultsDialog", false));
    }
};

CALC_TEST_MAIN(CalcSolverUiTest)

#include "tst_calc_solver_ui.moc"
