// SPDX-License-Identifier: GPL-3.0-or-later
// M78 (D-087) what-if analysis in the engine: Goal Seek (a linear, two
// nonlinear and a no-solution case, Excel's refusals, the changing cell's
// style and a goal already met) and the Scenario Manager (Excel's checks,
// Show, the Scenario Summary — current values, which scenario sets what,
// every changing cell given back, formulas included — the report's layout,
// and scenarios through structural edits). Data tables are
// tst_calc_datatables.cpp.

#include "goal_seek.h"
#include "scenario.h"
#include "structure.h"
#include "workbook.h"

#include <QtTest>

#include <cmath>

using namespace QindaOffice::Calc;

namespace {

double number(const Workbook &wb, qint32 sheet, const char *a1)
{
    const QString text = QString::fromLatin1(a1);
    const qint32 col = text.at(0).unicode() - 'A';
    const qint32 row = text.mid(1).toInt() - 1;
    return toNumber(wb.cellValue(sheet, row, col)).value_or(std::nan(""));
}

CellAddr at(const char *a1)
{
    const QString text = QString::fromLatin1(a1);
    return {text.mid(1).toInt() - 1, text.at(0).unicode() - 'A'};
}

void put(Workbook &wb, const char *a1, const QString &input)
{
    QVERIFY(wb.setCellInput(0, at(a1), input));
}

} // namespace

class CalcWhatIfTest : public QObject {
    Q_OBJECT
private slots:
    void goalSeekLinear();
    void goalSeekNonlinear();
    void goalSeekNoSolution();
    void goalSeekRefusals();
    void goalSeekKeepsStyleAndStopsAtTheGoal();
    void scenarioProblemsAndInputs();
    void showScenarioEntersValues();
    void summaryGivesEveryCellBack();
    void summaryReportLayout();
    void scenariosFollowStructuralEdits();
};

void CalcWhatIfTest::goalSeekLinear()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Model"));
    put(wb, "A1", QStringLiteral("1"));
    put(wb, "B1", QStringLiteral("=A1*3+4"));
    const GoalSeekResult result = goalSeek(wb, 0, at("B1"), 19, at("A1"));
    QVERIFY(result.started);
    QVERIFY(result.found);
    QVERIFY(result.error.isEmpty());
    QVERIFY(std::abs(number(wb, 0, "A1") - 5.0) < 1e-6);
    QVERIFY(std::abs(number(wb, 0, "B1") - 19.0) <= 0.001);
    QCOMPARE(result.changingValue, number(wb, 0, "A1"));
    QVERIFY2(result.iterations <= 3, qPrintable(QString::number(result.iterations)));
}

void CalcWhatIfTest::goalSeekNonlinear()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Model"));
    put(wb, "A1", QStringLiteral("1"));
    put(wb, "B1", QStringLiteral("=A1^2"));
    const GoalSeekResult root = goalSeek(wb, 0, at("B1"), 2, at("A1"));
    QVERIFY(root.found);
    QVERIFY(std::abs(number(wb, 0, "B1") - 2.0) <= 0.001);
    QVERIFY(std::abs(number(wb, 0, "A1") - std::sqrt(2.0)) < 0.001);
    QVERIFY(root.iterations <= 100);
    // The rate that doubles 1000 in ten years, from a blank changing cell.
    put(wb, "D1", QStringLiteral("=1000*(1+C1)^10"));
    const GoalSeekResult rate = goalSeek(wb, 0, at("D1"), 2000, at("C1"));
    QVERIFY(rate.found);
    QVERIFY2(std::abs(number(wb, 0, "C1") - (std::pow(2.0, 0.1) - 1.0)) < 1e-4,
             qPrintable(QString::number(number(wb, 0, "C1"), 'g', 12)));
    QVERIFY(std::abs(number(wb, 0, "D1") - 2000) <= 0.001);
}

void CalcWhatIfTest::goalSeekNoSolution()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Model"));
    put(wb, "A1", QStringLiteral("0"));
    put(wb, "B1", QStringLiteral("=A1^2"));
    const GoalSeekResult result = goalSeek(wb, 0, at("B1"), -1, at("A1"));
    QVERIFY(result.started);
    QVERIFY(!result.found);
    QVERIFY(result.error.isEmpty());
    QCOMPARE(result.iterations, 100);
    // Nothing came closer than A1 = 0 (B1 = 0, one away): it is left there.
    QCOMPARE(number(wb, 0, "A1"), 0.0);
    QCOMPARE(number(wb, 0, "B1"), 0.0);
    QCOMPARE(result.setValue, 0.0);
}

void CalcWhatIfTest::goalSeekRefusals()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Model"));
    put(wb, "A1", QStringLiteral("2"));
    put(wb, "B1", QStringLiteral("7"));
    put(wb, "C1", QStringLiteral("=A1*2"));
    put(wb, "D1", QStringLiteral("=IF(A1>0,\"big\",\"small\")"));
    put(wb, "E1", QStringLiteral("label"));
    GoalSeekResult result = goalSeek(wb, 0, at("B1"), 10, at("A1"));
    QVERIFY(!result.started);
    QCOMPARE(result.error, QStringLiteral("Cell must contain a formula."));
    result = goalSeek(wb, 0, at("C1"), 10, at("C1"));
    QCOMPARE(result.error, QStringLiteral("Cell must contain a value."));
    result = goalSeek(wb, 0, at("C1"), 10, at("E1"));
    QCOMPARE(result.error, QStringLiteral("Cell must contain a value."));
    result = goalSeek(wb, 0, at("D1"), 10, at("A1"));
    QVERIFY(!result.started);
    QVERIFY(!result.error.isEmpty());
    QCOMPARE(number(wb, 0, "A1"), 2.0); // nothing tried, nothing changed
}

void CalcWhatIfTest::goalSeekKeepsStyleAndStopsAtTheGoal()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Model"));
    put(wb, "A1", QStringLiteral("10"));
    put(wb, "B1", QStringLiteral("=A1/4"));
    CellStyle money;
    money.numberFormat = QStringLiteral("#,##0.00");
    const qint32 style = wb.addStyle(money);
    wb.sheetAt(0).setStyleIdAt(at("A1"), style);
    GoalSeekResult result = goalSeek(wb, 0, at("B1"), 2.5, at("A1"));
    QVERIFY(result.found);
    QCOMPARE(result.iterations, 0); // already there: nothing tried
    QCOMPARE(number(wb, 0, "A1"), 10.0);
    result = goalSeek(wb, 0, at("B1"), 7, at("A1"));
    QVERIFY(result.found);
    QVERIFY(std::abs(number(wb, 0, "A1") - 28.0) < 1e-6);
    QCOMPARE(wb.sheetAt(0).styleIdAt(at("A1")), style);
}

void CalcWhatIfTest::scenarioProblemsAndInputs()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Model"));
    put(wb, "A1", QStringLiteral("0.05"));
    put(wb, "A2", QStringLiteral("Yes"));
    put(wb, "A3", QStringLiteral("TRUE"));
    put(wb, "A4", QStringLiteral("=A1*2"));
    QCOMPARE(scenarioInputs(wb, 0, {at("A1"), at("A2"), at("A3"), at("A4"), at("A5")}),
             (QStringList{QStringLiteral("0.05"), QStringLiteral("Yes"), QStringLiteral("TRUE"), QStringLiteral("0.1"),
                          QString()}));
    ScenarioSet set;
    Scenario best;
    best.name = QStringLiteral("Best");
    best.cells = {at("A1")};
    best.values = {QStringLiteral("0.07")};
    QVERIFY(scenarioProblem(set, best).isEmpty());
    set.scenarios.append(best);
    Scenario again = best;
    again.name = QStringLiteral(" best ");
    QCOMPARE(scenarioProblem(set, again), QStringLiteral("Scenario name already exists."));
    QVERIFY(scenarioProblem(set, again, 0).isEmpty()); // editing Best itself
    again.name = QString();
    QCOMPARE(scenarioProblem(set, again), QStringLiteral("Enter a name for the scenario."));
    Scenario many;
    many.name = QStringLiteral("Many");
    for (qint32 r = 0; r < 33; ++r) {
        many.cells.append({r, 5});
        many.values.append(QStringLiteral("1"));
    }
    QCOMPARE(scenarioProblem(set, many), QStringLiteral("Too many changing cells."));
    many.cells = {};
    QCOMPARE(scenarioProblem(set, many), QStringLiteral("Changing cells reference is not valid."));
    many.cells = {at("A1"), at("A2")};
    many.values = {QStringLiteral("1")};
    QVERIFY(!scenarioProblem(set, many).isEmpty());
}

void CalcWhatIfTest::showScenarioEntersValues()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Model"));
    put(wb, "A1", QStringLiteral("10"));
    put(wb, "A2", QStringLiteral("3"));
    put(wb, "A3", QStringLiteral("=A1*A2"));
    ScenarioSet set;
    set.scenarios.append({QStringLiteral("Low"), {at("A1"), at("A2")}, {QStringLiteral("1"), QStringLiteral("2")}, {}, {}, true, false});
    set.scenarios.append({QStringLiteral("High"), {at("A1"), at("A2")}, {QStringLiteral("100"), QStringLiteral("2026-10-09")}, {}, {}, true, false});
    wb.sheetAt(0).setScenarios(set);
    QString error;
    QVERIFY(showScenario(wb, 0, 1, &error));
    QCOMPARE(number(wb, 0, "A1"), 100.0);
    QVERIFY(number(wb, 0, "A2") > 46000); // a date, entered as typing it would
    QCOMPARE(number(wb, 0, "A3"), 100.0 * number(wb, 0, "A2"));
    QCOMPARE(wb.sheetAt(0).scenarios().current, 1);
    QCOMPARE(wb.sheetAt(0).scenarios().shown, 1);
    QVERIFY(showScenario(wb, 0, 0, &error));
    QCOMPARE(number(wb, 0, "A3"), 2.0);
    QVERIFY(!showScenario(wb, 0, 2, &error));
    QVERIFY(!error.isEmpty());
}

void CalcWhatIfTest::summaryGivesEveryCellBack()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Model"));
    put(wb, "A4", QStringLiteral("5"));
    put(wb, "A1", QStringLiteral("=A4*2"));
    put(wb, "A2", QStringLiteral("3"));
    put(wb, "A3", QStringLiteral("=A1+A2"));
    ScenarioSet set;
    set.scenarios.append({QStringLiteral("Best"), {at("A1"), at("A2")}, {QStringLiteral("20"), QStringLiteral("4")},
                          QStringLiteral("Created by Pat"), {}, true, false});
    set.scenarios.append({QStringLiteral("Worst"), {at("A2")}, {QStringLiteral("1")}, {}, {}, true, false});
    wb.sheetAt(0).setScenarios(set);
    const ScenarioSummary summary = scenarioSummary(wb, 0, {{at("A3"), at("A3")}});
    QCOMPARE(summary.names, (QStringList{QStringLiteral("Current Values"), QStringLiteral("Best"), QStringLiteral("Worst")}));
    QCOMPARE(summary.changing, (QList<CellAddr>{at("A1"), at("A2")}));
    QCOMPARE(summary.results, (QList<CellAddr>{at("A3")}));
    QCOMPARE(summary.values.size(), 3);
    QCOMPARE(summary.values.at(0), (QList<Value>{10.0, 3.0, 13.0}));
    QCOMPARE(summary.values.at(1), (QList<Value>{20.0, 4.0, 24.0}));
    QCOMPARE(summary.values.at(2), (QList<Value>{10.0, 1.0, 11.0})); // Worst leaves A1 as it is
    QCOMPARE(summary.comments.value(1), QStringLiteral("Created by Pat"));
    QCOMPARE(summary.setBy.value(2), QList<CellAddr>{at("A2")});
    // The sheet is exactly as it was: A1 keeps its formula.
    const Cell *a1 = wb.sheetAt(0).cellAt(at("A1"));
    QVERIFY(a1 != nullptr && a1->hasFormula());
    QCOMPARE(a1->formula, QStringLiteral("A4*2"));
    QCOMPARE(number(wb, 0, "A1"), 10.0);
    QCOMPARE(number(wb, 0, "A3"), 13.0);
    put(wb, "A4", QStringLiteral("6")); // and still follows its precedent
    QCOMPARE(number(wb, 0, "A3"), 15.0);
}

void CalcWhatIfTest::summaryReportLayout()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Model"));
    put(wb, "A1", QStringLiteral("10"));
    put(wb, "A2", QStringLiteral("3"));
    put(wb, "A3", QStringLiteral("=A1*A2"));
    RangeRef rate;
    rate.sheet = QStringLiteral("Model");
    rate.range = {at("A2"), at("A2")};
    rate.absRow0 = rate.absCol0 = rate.absRow1 = rate.absCol1 = true;
    wb.defineName(QStringLiteral("Rate"), rate);
    ScenarioSet set;
    set.scenarios.append({QStringLiteral("Best"), {at("A1"), at("A2")}, {QStringLiteral("20"), QStringLiteral("4")}, {}, {}, true, false});
    set.scenarios.append({QStringLiteral("Worst"), {at("A2")}, {QStringLiteral("1")}, {}, {}, true, false});
    wb.sheetAt(0).setScenarios(set);
    const ScenarioSummary summary = scenarioSummary(wb, 0, {{at("A3"), at("A3")}});
    wb.addSheet(QStringLiteral("Scenario Summary"));
    writeScenarioSummary(wb, 1, 0, summary);
    const auto text = [&wb](const char *a1) { return toText(wb.cellValue(1, at(a1).row, at(a1).col)); };
    QCOMPARE(text("B2"), QStringLiteral("Scenario Summary"));
    QCOMPARE(text("D3"), QStringLiteral("Current Values:"));
    QCOMPARE(text("E3"), QStringLiteral("Best"));
    QCOMPARE(text("F3"), QStringLiteral("Worst"));
    QCOMPARE(text("B5"), QStringLiteral("Changing Cells:"));
    QCOMPARE(text("C6"), QStringLiteral("$A$1"));
    QCOMPARE(text("C7"), QStringLiteral("Rate")); // the name that names the cell
    QCOMPARE(number(wb, 1, "D6"), 10.0);
    QCOMPARE(number(wb, 1, "E6"), 20.0);
    QCOMPARE(number(wb, 1, "F6"), 10.0);
    QCOMPARE(text("B8"), QStringLiteral("Result Cells:"));
    QCOMPARE(text("C9"), QStringLiteral("$A$3"));
    QCOMPARE(number(wb, 1, "D9"), 30.0);
    QCOMPARE(number(wb, 1, "E9"), 80.0);
    QCOMPARE(number(wb, 1, "F9"), 10.0);
    QVERIFY(text("B10").startsWith(QStringLiteral("Notes:")));
    QCOMPARE(text("B12"), QStringLiteral("scenario are highlighted in gray."));
    const auto fill = [&wb](const char *a1) {
        const CellStyle *style = wb.style(wb.sheetAt(1).styleIdAt(at(a1)));
        return style != nullptr && style->bg.has_value();
    };
    QVERIFY(fill("E6"));  // Best sets A1
    QVERIFY(!fill("F6")); // Worst does not
    QVERIFY(fill("F7"));
    QVERIFY(!fill("D6")); // current values are not highlighted
    const CellStyle *title = wb.style(wb.sheetAt(1).styleIdAt(at("B2")));
    QVERIFY(title != nullptr && title->bold);
    // The columns fit their text (Excel autofits the report); the notes spill.
    const Sheet &report = wb.sheetAt(1);
    QVERIFY(report.columnWidth(1) >= 7.0 * QStringLiteral("Changing Cells:").size());
    QVERIFY(report.columnWidth(3) >= 7.0 * QStringLiteral("Current Values:").size());
    QVERIFY(report.columnWidth(1) < 7.0 * QStringLiteral("scenario are highlighted in gray.").size());
}

void CalcWhatIfTest::scenariosFollowStructuralEdits()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Model"));
    put(wb, "A2", QStringLiteral("1"));
    put(wb, "B2", QStringLiteral("2"));
    ScenarioSet set;
    set.scenarios.append({QStringLiteral("One"), {at("A2"), at("B2")}, {QStringLiteral("5"), QStringLiteral("6")}, {}, {}, true, false});
    set.scenarios.append({QStringLiteral("Two"), {at("C5")}, {QStringLiteral("9")}, {}, {}, true, false});
    set.current = 1;
    set.shown = 1;
    set.resultCells = {{at("D4"), at("D4")}};
    wb.sheetAt(0).setScenarios(set);
    applyStructureEdit(wb, {StructureOp::InsertRows, 0, 0, 2});
    ScenarioSet moved = wb.sheetAt(0).scenarios();
    QCOMPARE(moved.scenarios.at(0).cells, (QList<CellAddr>{at("A4"), at("B4")}));
    QCOMPARE(moved.scenarios.at(1).cells, QList<CellAddr>{at("C7")});
    QCOMPARE(moved.resultCells.value(0), (CellRange{at("D6"), at("D6")}));
    applyStructureEdit(wb, {StructureOp::RemoveColumns, 0, 0, 1}); // column A goes
    moved = wb.sheetAt(0).scenarios();
    QCOMPARE(moved.scenarios.at(0).cells, QList<CellAddr>{at("A4")}); // was B4
    QCOMPARE(moved.scenarios.at(0).values, QStringList{QStringLiteral("6")});
    applyStructureEdit(wb, {StructureOp::RemoveRows, 0, 6, 1}); // row 7 goes with Two's only cell
    moved = wb.sheetAt(0).scenarios();
    QCOMPARE(moved.scenarios.size(), 1);
    QCOMPARE(moved.scenarios.at(0).name, QStringLiteral("One"));
    QCOMPARE(moved.current, 0);
    QCOMPARE(moved.shown, -1); // the scenario last shown is gone
}

QTEST_GUILESS_MAIN(CalcWhatIfTest)
#include "tst_calc_whatif.moc"
