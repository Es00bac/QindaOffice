// SPDX-License-Identifier: GPL-3.0-or-later
#include "calc_solver_support.h"

class CalcSolverModelTest : public QObject {
    Q_OBJECT
private slots:
    void namesRoundTripAsExcelWritesThem();
    void namesFollowStructuralEdits();
    void answerReportLayout();
};

void CalcSolverModelTest::namesRoundTripAsExcelWritesThem()
{
    Workbook wb;
    SolverModel model = productMix(wb);
    model.constraints.append(rule("Mix!$B$2:$C$2", SolverRelation::Integer));
    model.constraints.append(rule("Mix!$B$2", SolverRelation::GreaterEqual, "Mix!$E$6/9"));
    model.options.maxTime = 30;
    model.options.precision = 0.000001;
    model.options.integerOptimality = 0.05;
    model.options.centralDerivatives = true;
    model.nonNegative = false;
    setSolverModel(wb, 0, model);
    QHash<QString, QString> names;
    for (const DefinedName &name : wb.definedNames()) {
        QCOMPARE(name.sheet, 0);
        QVERIFY(name.hidden);
        names.insert(name.name, name.formula);
    }
    QCOMPARE(names.value(QStringLiteral("solver_opt")), QStringLiteral("Mix!$E$2"));
    QCOMPARE(names.value(QStringLiteral("solver_typ")), QStringLiteral("1"));
    QCOMPARE(names.value(QStringLiteral("solver_val")), QStringLiteral("0"));
    QCOMPARE(names.value(QStringLiteral("solver_adj")), QStringLiteral("Mix!$B$2:$C$2"));
    QCOMPARE(names.value(QStringLiteral("solver_num")), QStringLiteral("3"));
    QCOMPARE(names.value(QStringLiteral("solver_lhs1")), QStringLiteral("Mix!$D$4:$D$6"));
    QCOMPARE(names.value(QStringLiteral("solver_rel1")), QStringLiteral("1"));
    QCOMPARE(names.value(QStringLiteral("solver_rhs1")), QStringLiteral("Mix!$E$4:$E$6"));
    QCOMPARE(names.value(QStringLiteral("solver_rel2")), QStringLiteral("4"));
    QCOMPARE(names.value(QStringLiteral("solver_rhs2")), QStringLiteral("integer"));
    QCOMPARE(names.value(QStringLiteral("solver_rel3")), QStringLiteral("3"));
    QCOMPARE(names.value(QStringLiteral("solver_neg")), QStringLiteral("2"));
    QCOMPARE(names.value(QStringLiteral("solver_eng")), QStringLiteral("2"));
    QCOMPARE(names.value(QStringLiteral("solver_pre")), QStringLiteral("0.000001"));
    QCOMPARE(names.value(QStringLiteral("solver_tim")), QStringLiteral("30"));
    QCOMPARE(names.value(QStringLiteral("solver_itr")), QStringLiteral("2147483647"));
    QCOMPARE(names.value(QStringLiteral("solver_tol")), QStringLiteral("0.05"));
    QCOMPARE(names.value(QStringLiteral("solver_drv")), QStringLiteral("2"));
    QCOMPARE(names.value(QStringLiteral("solver_ver")), QStringLiteral("3"));
    QCOMPARE(names.size(), 36);
    QCOMPARE(wb.definedNames().first().name, QStringLiteral("solver_adj")); // Excel's order
    const std::optional<SolverModel> read = solverModelOf(wb, 0);
    QVERIFY(read.has_value());
    SolverModel expected = model;
    expected.constraints[1].right.clear(); // an int constraint keeps no right side
    QCOMPARE(*read, expected);
    // A second save replaces the set; Reset All removes it.
    model.constraints.clear();
    setSolverModel(wb, 0, model);
    QCOMPARE(solverModelOf(wb, 0)->constraints.size(), 0);
    clearSolverModel(wb, 0);
    QVERIFY(!solverModelOf(wb, 0).has_value());
    QVERIFY(wb.definedNames().isEmpty());
}

void CalcSolverModelTest::namesFollowStructuralEdits()
{
    Workbook wb;
    SolverModel model = productMix(wb);
    model.variables = QStringLiteral("Mix!$B$2,Mix!$C$2");
    setSolverModel(wb, 0, model);
    applyStructureEdit(wb, {StructureOp::InsertRows, 0, 0, 2});
    const std::optional<SolverModel> moved = solverModelOf(wb, 0);
    QVERIFY(moved.has_value());
    QCOMPARE(moved->objective, QStringLiteral("Mix!$E$4"));
    QCOMPARE(moved->variables, QStringLiteral("Mix!$B$4,Mix!$C$4"));
    QCOMPARE(moved->constraints.at(0).left, QStringLiteral("Mix!$D$6:$D$8"));
    QCOMPARE(moved->constraints.at(0).right, QStringLiteral("Mix!$E$6:$E$8"));
    for (const DefinedName &name : wb.definedNames()) {
        QVERIFY2(!name.formula.startsWith(QLatin1Char('(')), qPrintable(name.formula)); // Excel's spelling of a union
    }
    // The model still solves where it went.
    const SolverOutcome out = solveModel(wb, 0, *moved);
    QCOMPARE(out.status, SolverStatus::Optimal);
    QCOMPARE(number(wb, "E4"), 36.0);
    // A renamed sheet is followed too.
    QVERIFY(renameSheet(wb, 0, QStringLiteral("Plan B")));
    QCOMPARE(solverModelOf(wb, 0)->objective, QStringLiteral("'Plan B'!$E$4"));
}

void CalcSolverModelTest::answerReportLayout()
{
    Workbook wb;
    const SolverModel model = productMix(wb);
    setSolverModel(wb, 0, model);
    const SolverOutcome out = solveModel(wb, 0, model);
    QCOMPARE(out.status, SolverStatus::Optimal);
    const qint32 report = addAnswerReport(wb, 0, model, out, QStringLiteral("Book1.xlsx"),
                                          QDateTime(QDate(2026, 10, 9), QTime(15, 15, 42)));
    QCOMPARE(report, 0);
    QCOMPARE(wb.sheetName(0), QStringLiteral("Answer Report 1"));
    QCOMPARE(wb.sheetName(1), QStringLiteral("Mix"));
    QVERIFY(solverModelOf(wb, 1).has_value()); // the model's names moved with their sheet
    QVERIFY(!solverModelOf(wb, 0).has_value());
    QCOMPARE(text(wb, 0, 0, 0), QStringLiteral("QindaCalc Answer Report"));
    QCOMPARE(text(wb, 0, 1, 0), QStringLiteral("Worksheet: [Book1.xlsx]Mix"));
    QCOMPARE(text(wb, 0, 2, 0), QStringLiteral("Report Created: 10/9/2026 3:15:42 PM"));
    QCOMPARE(text(wb, 0, 3, 0), QStringLiteral("Result: ") + solverStatusMessage(SolverStatus::Optimal));
    QCOMPARE(text(wb, 0, 5, 1), QStringLiteral("Engine: Simplex LP"));
    // Find the tables by their titles.
    const auto rowOf = [&wb](const QString &title) {
        for (qint32 r = 0; r < 60; ++r) {
            if (text(wb, 0, r, 0) == title) {
                return r;
            }
        }
        return -1;
    };
    const qint32 objective = rowOf(QStringLiteral("Objective Cell (Max)"));
    QVERIFY(objective > 0);
    QCOMPARE(text(wb, 0, objective + 1, 1), QStringLiteral("Cell"));
    QCOMPARE(text(wb, 0, objective + 2, 1), QStringLiteral("$E$2"));
    QCOMPARE(text(wb, 0, objective + 2, 2), QStringLiteral("Units Profit")); // the label left of it, the one above
    QCOMPARE(toNumber(wb.cellValue(0, objective + 2, 3)).value_or(-1), 0.0);
    QCOMPARE(toNumber(wb.cellValue(0, objective + 2, 4)).value_or(-1), 36.0);
    const qint32 variables = rowOf(QStringLiteral("Variable Cells"));
    QVERIFY(variables > objective);
    QCOMPARE(text(wb, 0, variables + 2, 1), QStringLiteral("$B$2"));
    QCOMPARE(text(wb, 0, variables + 2, 2), QStringLiteral("Units Doors"));
    QCOMPARE(toNumber(wb.cellValue(0, variables + 2, 4)).value_or(-1), 2.0);
    QCOMPARE(text(wb, 0, variables + 2, 5), QStringLiteral("Contin"));
    QCOMPARE(text(wb, 0, variables + 3, 2), QStringLiteral("Units Windows"));
    const qint32 constraints = rowOf(QStringLiteral("Constraints"));
    QVERIFY(constraints > variables);
    QCOMPARE(text(wb, 0, constraints + 1, 6), QStringLiteral("Slack"));
    QCOMPARE(text(wb, 0, constraints + 2, 1), QStringLiteral("$D$4"));
    QCOMPARE(text(wb, 0, constraints + 2, 2), QStringLiteral("Plant 1 Hours"));
    QCOMPARE(toNumber(wb.cellValue(0, constraints + 2, 3)).value_or(-1), 2.0);
    QCOMPARE(text(wb, 0, constraints + 2, 4), QStringLiteral("$D$4<=$E$4"));
    QCOMPARE(text(wb, 0, constraints + 2, 5), QStringLiteral("Not Binding"));
    QCOMPARE(toNumber(wb.cellValue(0, constraints + 2, 6)).value_or(-1), 2.0);
    QCOMPARE(text(wb, 0, constraints + 3, 5), QStringLiteral("Binding"));
    // A second report is "Answer Report 2", before the model again.
    QCOMPARE(addAnswerReport(wb, 1, model, out, QStringLiteral("Book1.xlsx"), QDateTime::currentDateTime()), 1);
    QCOMPARE(wb.sheetName(1), QStringLiteral("Answer Report 2"));
}

QTEST_GUILESS_MAIN(CalcSolverModelTest)
#include "tst_calc_solver_model.moc"
