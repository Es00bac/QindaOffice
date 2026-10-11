// SPDX-License-Identifier: GPL-3.0-or-later
#include "calc_solver_support.h"

class CalcSolverTest : public QObject {
    Q_OBJECT
private slots:
    void productMixSimplex();
    void transportationWithRangeConstraints();
    void dietMinimizesCost();
    void binaryKnapsack();
    void allDifferentAssignment();
    void rosenbrockWithBounds();
    void rosenbrockOnABound();
    void constrainedQuadratics();
    void evolutionaryMinimumAndBounds();
    void infeasibleModel();
    void unboundedModel();
    void nonlinearModelSentToSimplex();
    void stopFromProgress();
    void refusals();
};

void CalcSolverTest::productMixSimplex()
{
    Workbook wb;
    const SolverModel model = productMix(wb);
    QVERIFY(solverModelProblem(wb, 0, model).isEmpty());
    const SolverOutcome out = solveModel(wb, 0, model);
    QVERIFY2(out.started, qPrintable(out.message));
    QCOMPARE(out.status, SolverStatus::Optimal);
    QVERIFY(out.changed);
    QCOMPARE(number(wb, "B2"), 2.0);
    QCOMPARE(number(wb, "C2"), 6.0);
    QCOMPARE(number(wb, "E2"), 36.0);
    QCOMPARE(out.originalValues, (QVector<double>{0, 0}));
    QCOMPARE(out.values, (QVector<double>{2, 6}));
    QCOMPARE(toNumber(out.finalObjective).value_or(-1), 36.0);
    QCOMPARE(out.message, solverStatusMessage(SolverStatus::Optimal));
    // D4 = 2 of 4 (slack 2); D5 = 12 and D6 = 18 bind.
    QCOMPARE(out.constraints.size(), 3);
    QCOMPARE(out.constraints.at(0).formula, QStringLiteral("$D$4<=$E$4"));
    QVERIFY(!out.constraints.at(0).binding);
    QCOMPARE(out.constraints.at(0).slack, 2.0);
    QVERIFY(out.constraints.at(1).binding);
    QVERIFY(out.constraints.at(2).binding);
}

void CalcSolverTest::transportationWithRangeConstraints()
{
    // Two plants (20, 30) ship to three markets (10, 25, 15); unit costs
    // 8 6 10 / 9 12 13. Optimum 465: plant 1 sends 20 to market 2, plant 2
    // sends 10, 5 and 15.
    Workbook wb;
    wb.addSheet(QStringLiteral("Ship"));
    const char *costs[2][3] = {{"8", "6", "10"}, {"9", "12", "13"}};
    const char *costCells[2][3] = {{"B3", "C3", "D3"}, {"B4", "C4", "D4"}};
    for (int r = 0; r < 2; ++r) {
        for (int c = 0; c < 3; ++c) {
            put(wb, costCells[r][c], QString::fromLatin1(costs[r][c]));
        }
    }
    put(wb, "E7", QStringLiteral("=SUM(B7:D7)"));
    put(wb, "E8", QStringLiteral("=SUM(B8:D8)"));
    put(wb, "F7", QStringLiteral("20"));
    put(wb, "F8", QStringLiteral("30"));
    put(wb, "B9", QStringLiteral("=SUM(B7:B8)"));
    put(wb, "C9", QStringLiteral("=SUM(C7:C8)"));
    put(wb, "D9", QStringLiteral("=SUM(D7:D8)"));
    put(wb, "B10", QStringLiteral("10"));
    put(wb, "C10", QStringLiteral("25"));
    put(wb, "D10", QStringLiteral("15"));
    put(wb, "B12", QStringLiteral("=SUMPRODUCT(B3:D4,B7:D8)"));
    SolverModel model;
    model.objective = QStringLiteral("Ship!$B$12");
    model.goal = SolverGoal::Min;
    model.variables = QStringLiteral("Ship!$B$7:$D$8");
    model.constraints = {rule("Ship!$E$7:$E$8", SolverRelation::LessEqual, "Ship!$F$7:$F$8"),
                         rule("Ship!$B$9:$D$9", SolverRelation::GreaterEqual, "Ship!$B$10:$D$10")};
    model.method = SolverMethod::SimplexLp;
    const SolverOutcome out = solveModel(wb, 0, model);
    QCOMPARE(out.status, SolverStatus::Optimal);
    QCOMPARE(number(wb, "B12"), 465.0);
    QCOMPARE(number(wb, "C7"), 20.0);
    QCOMPARE(number(wb, "B8"), 10.0);
    QCOMPARE(number(wb, "C8"), 5.0);
    QCOMPARE(number(wb, "D8"), 15.0);
    QCOMPARE(number(wb, "B7") + number(wb, "D7"), 0.0);
    QCOMPARE(out.constraints.size(), 5); // two supplies, three demands
    QCOMPARE(out.constraints.at(2).formula, QStringLiteral("$B$9>=$B$10"));
}

void CalcSolverTest::dietMinimizesCost()
{
    // Cost 2x + 3y; protein 3x + y >= 6, vitamins x + 4y >= 8, fat x + y <= 10.
    // Optimum x = 16/11, y = 18/11, cost 86/11.
    Workbook wb;
    wb.addSheet(QStringLiteral("Diet"));
    put(wb, "B2", QStringLiteral("2"));
    put(wb, "C2", QStringLiteral("3"));
    put(wb, "B3", QStringLiteral("3"));
    put(wb, "C3", QStringLiteral("1"));
    put(wb, "B4", QStringLiteral("1"));
    put(wb, "C4", QStringLiteral("4"));
    put(wb, "B5", QStringLiteral("1"));
    put(wb, "C5", QStringLiteral("1"));
    put(wb, "D3", QStringLiteral("=SUMPRODUCT(B3:C3,$B$8:$C$8)"));
    put(wb, "D4", QStringLiteral("=SUMPRODUCT(B4:C4,$B$8:$C$8)"));
    put(wb, "D5", QStringLiteral("=SUMPRODUCT(B5:C5,$B$8:$C$8)"));
    put(wb, "E3", QStringLiteral("6"));
    put(wb, "E4", QStringLiteral("8"));
    put(wb, "E5", QStringLiteral("10"));
    put(wb, "D2", QStringLiteral("=SUMPRODUCT(B2:C2,B8:C8)"));
    SolverModel model;
    model.objective = QStringLiteral("Diet!$D$2");
    model.goal = SolverGoal::Min;
    model.variables = QStringLiteral("Diet!$B$8:$C$8");
    model.constraints = {rule("Diet!$D$3:$D$4", SolverRelation::GreaterEqual, "Diet!$E$3:$E$4"),
                         rule("Diet!$D$5", SolverRelation::LessEqual, "Diet!$E$5")};
    model.method = SolverMethod::SimplexLp;
    const SolverOutcome out = solveModel(wb, 0, model);
    QCOMPARE(out.status, SolverStatus::Optimal);
    QVERIFY(std::abs(number(wb, "B8") - 16.0 / 11.0) < 1e-9);
    QVERIFY(std::abs(number(wb, "C8") - 18.0 / 11.0) < 1e-9);
    QVERIFY(std::abs(number(wb, "D2") - 86.0 / 11.0) < 1e-9);
    // The same model through GRG Nonlinear lands on the same vertex.
    wb.setCellValue(0, at("B8"), Value());
    wb.setCellValue(0, at("C8"), Value());
    SolverModel smooth = model;
    smooth.method = SolverMethod::GrgNonlinear;
    const SolverOutcome grg = solveModel(wb, 0, smooth);
    QVERIFY2(solverStatusFeasible(grg.status), qPrintable(grg.message));
    QVERIFY(std::abs(number(wb, "D2") - 86.0 / 11.0) < 1e-4);
}

void CalcSolverTest::binaryKnapsack()
{
    // Weights 12 2 1 1 4, values 4 2 1 2 10, capacity 15: items 2–5 (15).
    Workbook wb;
    wb.addSheet(QStringLiteral("Pack"));
    const char *weights[] = {"12", "2", "1", "1", "4"};
    const char *values[] = {"4", "2", "1", "2", "10"};
    const char *weightCells[] = {"B2", "C2", "D2", "E2", "F2"};
    const char *valueCells[] = {"B3", "C3", "D3", "E3", "F3"};
    for (int i = 0; i < 5; ++i) {
        put(wb, weightCells[i], QString::fromLatin1(weights[i]));
        put(wb, valueCells[i], QString::fromLatin1(values[i]));
    }
    put(wb, "G2", QStringLiteral("=SUMPRODUCT(B2:F2,B4:F4)"));
    put(wb, "G3", QStringLiteral("=SUMPRODUCT(B3:F3,B4:F4)"));
    SolverModel model;
    model.objective = QStringLiteral("Pack!$G$3");
    model.variables = QStringLiteral("Pack!$B$4:$F$4");
    model.constraints = {rule("Pack!$G$2", SolverRelation::LessEqual, "15"), rule("Pack!$B$4:$F$4", SolverRelation::Binary)};
    model.method = SolverMethod::SimplexLp;
    model.options.integerOptimality = 0;
    const SolverOutcome out = solveModel(wb, 0, model);
    QCOMPARE(out.status, SolverStatus::Optimal);
    QCOMPARE(number(wb, "G3"), 15.0);
    QCOMPARE(out.values, (QVector<double>{0, 1, 1, 1, 1}));
    QCOMPARE(out.variableKinds, (QList<int>{2, 2, 2, 2, 2}));
    QVERIFY(out.subproblems >= 1);
    QCOMPARE(out.constraints.last().formula, QStringLiteral("$F$4=binary"));
    // GRG Nonlinear gets there too, through its own relaxations.
    for (const char *cell : {"B4", "C4", "D4", "E4", "F4"}) {
        wb.setCellValue(0, at(cell), Value());
    }
    model.method = SolverMethod::GrgNonlinear;
    const SolverOutcome grg = solveModel(wb, 0, model);
    QVERIFY2(solverStatusFeasible(grg.status), qPrintable(grg.message));
    QCOMPARE(number(wb, "G3"), 15.0);
    // Ignore Integer Constraints: the relaxation's 15 + 4·2/12.
    model.method = SolverMethod::SimplexLp;
    model.options.ignoreIntegers = true;
    const SolverOutcome relaxed = solveModel(wb, 0, model);
    QCOMPARE(relaxed.status, SolverStatus::Optimal);
    QVERIFY(std::abs(number(wb, "G3") - (15.0 + 4.0 * 7.0 / 12.0)) < 1e-9);
}

void CalcSolverTest::allDifferentAssignment()
{
    // x1..x3 a permutation of 1..3 minimizing x1 + 2·x2 + 3·x3: 3, 2, 1 (10).
    Workbook wb;
    wb.addSheet(QStringLiteral("Order"));
    put(wb, "E2", QStringLiteral("=B2+2*C2+3*D2"));
    SolverModel model;
    model.objective = QStringLiteral("Order!$E$2");
    model.goal = SolverGoal::Min;
    model.variables = QStringLiteral("Order!$B$2:$D$2");
    model.constraints = {rule("Order!$B$2:$D$2", SolverRelation::AllDifferent)};
    model.method = SolverMethod::SimplexLp;
    model.options.integerOptimality = 0;
    const SolverOutcome out = solveModel(wb, 0, model);
    QCOMPARE(out.status, SolverStatus::Optimal);
    QCOMPARE(out.values, (QVector<double>{3, 2, 1}));
    QCOMPARE(number(wb, "E2"), 10.0);
}

void CalcSolverTest::rosenbrockWithBounds()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Rosen"));
    put(wb, "B2", QStringLiteral("-1.2"));
    put(wb, "C2", QStringLiteral("1"));
    put(wb, "D2", QStringLiteral("=(1-B2)^2+100*(C2-B2^2)^2"));
    SolverModel model;
    model.objective = QStringLiteral("Rosen!$D$2");
    model.goal = SolverGoal::Min;
    model.variables = QStringLiteral("Rosen!$B$2:$C$2");
    model.constraints = {rule("Rosen!$B$2", SolverRelation::LessEqual, "2"), rule("Rosen!$B$2", SolverRelation::GreaterEqual, "-2"),
                         rule("Rosen!$C$2", SolverRelation::LessEqual, "3"), rule("Rosen!$C$2", SolverRelation::GreaterEqual, "-1")};
    model.nonNegative = false;
    model.method = SolverMethod::GrgNonlinear;
    const SolverOutcome out = solveModel(wb, 0, model);
    QVERIFY2(solverStatusFeasible(out.status), qPrintable(out.message));
    QVERIFY2(std::abs(number(wb, "B2") - 1.0) < 1e-3, qPrintable(QString::number(number(wb, "B2"))));
    QVERIFY2(std::abs(number(wb, "C2") - 1.0) < 2e-3, qPrintable(QString::number(number(wb, "C2"))));
    QVERIFY(number(wb, "D2") < 1e-5);
    QVERIFY(out.iterations > 5);
    // Central derivatives get as close.
    wb.setCellValue(0, at("B2"), Value(-1.2));
    wb.setCellValue(0, at("C2"), Value(1.0));
    model.options.centralDerivatives = true;
    const SolverOutcome central = solveModel(wb, 0, model);
    QVERIFY2(solverStatusFeasible(central.status), qPrintable(central.message));
    QVERIFY(std::abs(number(wb, "B2") - 1.0) < 1e-3);
}

void CalcSolverTest::rosenbrockOnABound()
{
    // x <= 0.5 cuts the valley: the optimum is (0.5, 0.25), 0.25.
    Workbook wb;
    wb.addSheet(QStringLiteral("Rosen"));
    put(wb, "B2", QStringLiteral("-1"));
    put(wb, "C2", QStringLiteral("2"));
    put(wb, "D2", QStringLiteral("=(1-B2)^2+100*(C2-B2^2)^2"));
    SolverModel model;
    model.objective = QStringLiteral("Rosen!$D$2");
    model.goal = SolverGoal::Min;
    model.variables = QStringLiteral("Rosen!$B$2,Rosen!$C$2");
    model.constraints = {rule("Rosen!$B$2", SolverRelation::LessEqual, "0.5")};
    model.nonNegative = false;
    const SolverOutcome out = solveModel(wb, 0, model);
    QVERIFY2(solverStatusFeasible(out.status), qPrintable(out.message));
    QCOMPARE(number(wb, "B2"), 0.5);
    QVERIFY(std::abs(number(wb, "C2") - 0.25) < 1e-3);
    QVERIFY(std::abs(number(wb, "D2") - 0.25) < 1e-5);
    QVERIFY(out.constraints.at(0).binding);
}

void CalcSolverTest::constrainedQuadratics()
{
    // Min (x−1)² + (y−2)² with x + y <= 2, x, y >= 0: (0.5, 1.5), 0.5.
    Workbook wb;
    wb.addSheet(QStringLiteral("Q"));
    put(wb, "D2", QStringLiteral("=(B2-1)^2+(C2-2)^2"));
    put(wb, "E2", QStringLiteral("=B2+C2"));
    SolverModel model;
    model.objective = QStringLiteral("Q!$D$2");
    model.goal = SolverGoal::Min;
    model.variables = QStringLiteral("Q!$B$2:$C$2");
    model.constraints = {rule("Q!$E$2", SolverRelation::LessEqual, "2")};
    const SolverOutcome out = solveModel(wb, 0, model);
    QVERIFY2(solverStatusFeasible(out.status), qPrintable(out.message));
    QVERIFY2(std::abs(number(wb, "B2") - 0.5) < 1e-4, qPrintable(QString::number(number(wb, "B2"))));
    QVERIFY(std::abs(number(wb, "C2") - 1.5) < 1e-4);
    QVERIFY(std::abs(number(wb, "D2") - 0.5) < 1e-4);
    QVERIFY(number(wb, "E2") <= 2.0 + 1e-6);
    // Min x² + y² with x + y = 1 (an equality): (0.5, 0.5).
    put(wb, "D2", QStringLiteral("=B2^2+C2^2"));
    model.constraints = {rule("Q!$E$2", SolverRelation::Equal, "1")};
    model.nonNegative = false;
    const SolverOutcome equal = solveModel(wb, 0, model);
    QVERIFY2(solverStatusFeasible(equal.status), qPrintable(equal.message));
    QVERIFY(std::abs(number(wb, "B2") - 0.5) < 1e-4);
    QVERIFY(std::abs(number(wb, "C2") - 0.5) < 1e-4);
    // Value Of: x² + y² = 8 with x = y (both 2).
    put(wb, "F2", QStringLiteral("=B2-C2"));
    model.goal = SolverGoal::ValueOf;
    model.valueOf = 8;
    model.constraints = {rule("Q!$F$2", SolverRelation::Equal, "0")};
    model.nonNegative = true;
    wb.setCellValue(0, at("B2"), Value(1.0));
    wb.setCellValue(0, at("C2"), Value(1.0));
    const SolverOutcome valueOf = solveModel(wb, 0, model);
    QVERIFY2(solverStatusFeasible(valueOf.status), qPrintable(valueOf.message));
    QVERIFY(std::abs(number(wb, "D2") - 8.0) < 1e-4);
    QVERIFY(std::abs(number(wb, "B2") - 2.0) < 1e-4);
}

void CalcSolverTest::evolutionaryMinimumAndBounds()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Evo"));
    put(wb, "D2", QStringLiteral("=(B2-3)^2+(C2+1)^2+ABS(B2-3)"));
    SolverModel model;
    model.objective = QStringLiteral("Evo!$D$2");
    model.goal = SolverGoal::Min;
    model.variables = QStringLiteral("Evo!$B$2:$C$2");
    model.nonNegative = false;
    model.method = SolverMethod::Evolutionary;
    model.options.randomSeed = 7;
    model.options.populationSize = 40;
    model.options.convergence = 1e-6;
    // Without bounds and with Require Bounds on Variables: Excel's refusal.
    SolverOutcome out = solveModel(wb, 0, model);
    QCOMPARE(out.status, SolverStatus::NeedsBounds);
    QVERIFY(!out.changed);
    model.constraints = {rule("Evo!$B$2:$C$2", SolverRelation::LessEqual, "10"),
                         rule("Evo!$B$2:$C$2", SolverRelation::GreaterEqual, "-10")};
    out = solveModel(wb, 0, model);
    QVERIFY2(solverStatusFeasible(out.status), qPrintable(out.message));
    QVERIFY2(std::abs(number(wb, "B2") - 3.0) < 0.05, qPrintable(QString::number(number(wb, "B2"))));
    QVERIFY(std::abs(number(wb, "C2") + 1.0) < 0.05);
}

void CalcSolverTest::infeasibleModel()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("No"));
    put(wb, "B2", QStringLiteral("1"));
    put(wb, "C2", QStringLiteral("2"));
    put(wb, "D2", QStringLiteral("=B2+C2"));
    SolverModel model;
    model.objective = QStringLiteral("No!$D$2");
    model.variables = QStringLiteral("No!$B$2:$C$2");
    model.constraints = {rule("No!$D$2", SolverRelation::GreaterEqual, "10"), rule("No!$D$2", SolverRelation::LessEqual, "5")};
    model.method = SolverMethod::SimplexLp;
    const SolverOutcome out = solveModel(wb, 0, model);
    QCOMPARE(out.status, SolverStatus::Infeasible);
    QCOMPARE(out.message, QStringLiteral("Solver could not find a feasible solution."));
    QVERIFY(!out.changed);
    QCOMPARE(number(wb, "B2"), 1.0); // the cells keep their own values
    QCOMPARE(number(wb, "C2"), 2.0);
    // Conflicting bounds on the variables themselves too.
    model.constraints = {rule("No!$B$2", SolverRelation::GreaterEqual, "5"), rule("No!$B$2", SolverRelation::LessEqual, "3")};
    QCOMPARE(solveModel(wb, 0, model).status, SolverStatus::Infeasible);
    // GRG Nonlinear reports it as well.
    model.constraints = {rule("No!$D$2", SolverRelation::GreaterEqual, "10"), rule("No!$D$2", SolverRelation::LessEqual, "5")};
    model.method = SolverMethod::GrgNonlinear;
    QCOMPARE(solveModel(wb, 0, model).status, SolverStatus::Infeasible);
}

void CalcSolverTest::unboundedModel()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Up"));
    put(wb, "D2", QStringLiteral("=B2+C2"));
    put(wb, "E2", QStringLiteral("=B2-C2"));
    SolverModel model;
    model.objective = QStringLiteral("Up!$D$2");
    model.goal = SolverGoal::Max;
    model.variables = QStringLiteral("Up!$B$2:$C$2");
    model.constraints = {rule("Up!$E$2", SolverRelation::LessEqual, "1")};
    model.method = SolverMethod::SimplexLp;
    const SolverOutcome out = solveModel(wb, 0, model);
    QCOMPARE(out.status, SolverStatus::NotConverging);
    QCOMPARE(out.message, QStringLiteral("The Objective Cell values do not converge."));
    QVERIFY(!out.changed);
    QVERIFY(isBlank(wb.cellValue(0, 1, 1))); // B2 stays empty
}

void CalcSolverTest::nonlinearModelSentToSimplex()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Curve"));
    put(wb, "B2", QStringLiteral("1"));
    put(wb, "C2", QStringLiteral("1"));
    put(wb, "D2", QStringLiteral("=B2*C2"));
    put(wb, "E2", QStringLiteral("=B2+C2"));
    SolverModel model;
    model.objective = QStringLiteral("Curve!$D$2");
    model.variables = QStringLiteral("Curve!$B$2:$C$2");
    model.constraints = {rule("Curve!$E$2", SolverRelation::LessEqual, "4")};
    model.method = SolverMethod::SimplexLp;
    const SolverOutcome out = solveModel(wb, 0, model);
    QCOMPARE(out.status, SolverStatus::NotLinear);
    QCOMPARE(out.message, QStringLiteral("The linearity conditions required by this LP Solver are not satisfied."));
    QVERIFY(!out.changed);
    QCOMPARE(number(wb, "B2"), 1.0);
    // GRG Nonlinear solves it: x = y = 2.
    model.method = SolverMethod::GrgNonlinear;
    const SolverOutcome grg = solveModel(wb, 0, model);
    QVERIFY2(solverStatusFeasible(grg.status), qPrintable(grg.message));
    QVERIFY2(std::abs(number(wb, "D2") - 4.0) < 1e-4,
             qPrintable(QStringLiteral("%1 at %2, %3 (%4)").arg(number(wb, "D2")).arg(number(wb, "B2")).arg(number(wb, "C2")).arg(grg.message)));
}

void CalcSolverTest::stopFromProgress()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Rosen"));
    put(wb, "B2", QStringLiteral("-1.2"));
    put(wb, "C2", QStringLiteral("1"));
    put(wb, "D2", QStringLiteral("=(1-B2)^2+100*(C2-B2^2)^2"));
    SolverModel model;
    model.objective = QStringLiteral("Rosen!$D$2");
    model.goal = SolverGoal::Min;
    model.variables = QStringLiteral("Rosen!$B$2:$C$2");
    model.nonNegative = false;
    int calls = 0;
    SolverCallbacks callbacks;
    callbacks.progress = [&calls](const SolverProgress &progress) {
        ++calls;
        return progress.trials < 3;
    };
    const SolverOutcome out = solveModel(wb, 0, model, callbacks);
    QCOMPARE(out.status, SolverStatus::Stopped);
    QCOMPARE(out.message, QStringLiteral("Solver stopped at user's request."));
    QCOMPARE(calls, 3);
    QVERIFY(out.changed); // the last trial is offered (Keep / Restore)
    // The iteration limit stops it the same way, with its own status.
    model.options.iterations = 2;
    wb.setCellValue(0, at("B2"), Value(-1.2));
    QCOMPARE(solveModel(wb, 0, model).status, SolverStatus::IterationLimit);
}

void CalcSolverTest::refusals()
{
    Workbook wb;
    const SolverModel model = productMix(wb);
    wb.addSheet(QStringLiteral("Other"));
    SolverModel bad = model;
    bad.objective = QStringLiteral("Mix!$E$2:$E$3");
    QCOMPARE(solverModelProblem(wb, 0, bad), QStringLiteral("Objective Cell must be a single cell on the active sheet."));
    bad.objective = QStringLiteral("Other!$A$1");
    QCOMPARE(solverModelProblem(wb, 0, bad), QStringLiteral("Objective Cell must be a single cell on the active sheet."));
    bad = model;
    bad.variables = QStringLiteral("Other!$B$2");
    QCOMPARE(solverModelProblem(wb, 0, bad), QStringLiteral("Variable Cells must be on the active sheet."));
    bad.variables = QStringLiteral("Mix!$D$4");
    QVERIFY(solverModelProblem(wb, 0, bad).startsWith(QStringLiteral("Variable cells cannot contain formulas")));
    bad.variables.clear();
    QVERIFY(!solverModelProblem(wb, 0, bad).isEmpty());
    bad = model;
    bad.constraints = {rule("Mix!$D$4", SolverRelation::Integer)};
    QCOMPARE(solverModelProblem(wb, 0, bad),
             QStringLiteral("Integer, binary and alldifferent constraints can only be applied to variable cells."));
    QCOMPARE(solverConstraintProblem(wb, 0, model, rule("Mix!$D$4", SolverRelation::LessEqual, "\"text\"")),
             QStringLiteral("Constraint must be a number, a simple reference, or a formula with a numeric value."));
    QCOMPARE(solverConstraintProblem(wb, 0, model, rule("Mix!$D$4:$D$6", SolverRelation::LessEqual, "Mix!$E$4:$E$5")),
             QStringLiteral("The Constraint must be one cell or a range the size of the Cell Reference."));
    QVERIFY(solverConstraintProblem(wb, 0, model, rule("Mix!$D$4:$D$6", SolverRelation::LessEqual, "SUM(Mix!$E$4:$E$5)/2")).isEmpty());
    const SolverOutcome refused = solveModel(wb, 0, bad);
    QVERIFY(!refused.started);
    QCOMPARE(refused.status, SolverStatus::ModelError);
}

QTEST_GUILESS_MAIN(CalcSolverTest)
#include "tst_calc_solver.moc"
