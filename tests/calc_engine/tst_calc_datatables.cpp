// SPDX-License-Identifier: GPL-3.0-or-later
// M78 (D-087) data tables in the engine ({=TABLE(r,c)}): one variable down
// a column and across a row with two formulas each, two variables, results
// following an input value or a precedent and read by other formulas, the
// input cell given back with its own formula, Excel's refusals and formula
// text, structural edits (a deleted input cell shows #REF!, Excel's
// "Cannot change part of a data table" cuts), and the speed of a 20 × 20
// table worked out after every edit.

#include "data_table.h"
#include "structure.h"
#include "workbook.h"

#include <QElapsedTimer>
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

class CalcDataTablesTest : public QObject {
    Q_OBJECT
private slots:
    void oneVariableColumnTable();
    void oneVariableRowTable();
    void twoVariableTable();
    void tableInputCellKeepsItsFormula();
    void makeDataTableAndFormulaText();
    void tablesFollowStructuralEdits();
    void twentyByTwentyTableIsQuick();
};

void CalcDataTablesTest::oneVariableColumnTable()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Model"));
    put(wb, "A1", QStringLiteral("2"));
    put(wb, "D1", QStringLiteral("0"));
    put(wb, "B3", QStringLiteral("=A1*10+D1"));
    put(wb, "C3", QStringLiteral("=A1+100"));
    for (int i = 0; i < 3; ++i) {
        QVERIFY(wb.setCellInput(0, {3 + i, 0}, QString::number(i + 1)));
    }
    QString error;
    const std::optional<DataTableSpec> spec = makeDataTable({at("A3"), at("C6")}, CellAddr{}, at("A1"), &error);
    QVERIFY2(spec.has_value(), qPrintable(error));
    QCOMPARE(spec->ref, (CellRange{at("B4"), at("C6")}));
    QVERIFY(!spec->twoD && !spec->rowInput);
    wb.sheetAt(0).setDataTables({*spec});
    wb.recalcAll();
    QCOMPARE(number(wb, 0, "B4"), 10.0);
    QCOMPARE(number(wb, 0, "C4"), 101.0);
    QCOMPARE(number(wb, 0, "B6"), 30.0);
    QCOMPARE(number(wb, 0, "C6"), 103.0);
    QCOMPARE(number(wb, 0, "A1"), 2.0); // the input cell is given back
    QCOMPARE(number(wb, 0, "B3"), 20.0);
    QVERIFY(!wb.sheetAt(0).cellAt(at("B5"))->hasFormula()); // results are values
    put(wb, "A5", QStringLiteral("7"));                     // an input value changes
    QCOMPARE(number(wb, 0, "B5"), 70.0);
    put(wb, "D1", QStringLiteral("1"));                     // a precedent changes
    QCOMPARE(number(wb, 0, "B4"), 11.0);
    QCOMPARE(number(wb, 0, "B5"), 71.0);
    put(wb, "E1", QStringLiteral("=SUM(B4:B6)"));            // a formula reads the results
    QCOMPARE(number(wb, 0, "E1"), 11.0 + 71.0 + 31.0);
}

void CalcDataTablesTest::oneVariableRowTable()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Model"));
    put(wb, "A1", QStringLiteral("1"));
    for (int i = 0; i < 3; ++i) {
        QVERIFY(wb.setCellInput(0, {7, 1 + i}, QString::number(4 + i)));
    }
    put(wb, "A9", QStringLiteral("=A1*2"));
    put(wb, "A10", QStringLiteral("=A1^2"));
    const std::optional<DataTableSpec> spec = makeDataTable({at("A8"), {9, 3}}, at("A1"), CellAddr{});
    QVERIFY(spec.has_value());
    QVERIFY(spec->rowInput && !spec->twoD);
    QCOMPARE(spec->ref, (CellRange{at("B9"), {9, 3}}));
    wb.sheetAt(0).setDataTables({*spec});
    wb.recalcAll();
    QCOMPARE(number(wb, 0, "B9"), 8.0);
    QCOMPARE(number(wb, 0, "D9"), 12.0);
    QCOMPARE(toNumber(wb.cellValue(0, 9, 2)).value_or(0), 25.0); // C10 = 5^2
    QCOMPARE(toNumber(wb.cellValue(0, 9, 3)).value_or(0), 36.0);
    QCOMPARE(dataTableFormulaText(*spec), QStringLiteral("{=TABLE(A1,)}"));
}

void CalcDataTablesTest::twoVariableTable()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Model"));
    put(wb, "A1", QStringLiteral("2"));
    put(wb, "B1", QStringLiteral("3"));
    put(wb, "E3", QStringLiteral("=A1*B1"));
    for (int i = 0; i < 3; ++i) {
        QVERIFY(wb.setCellInput(0, {2, 5 + i}, QString::number(i + 1)));
        QVERIFY(wb.setCellInput(0, {3 + i, 4}, QString::number((i + 1) * 10)));
    }
    const std::optional<DataTableSpec> spec = makeDataTable({at("E3"), at("H6")}, at("A1"), at("B1"));
    QVERIFY(spec.has_value() && spec->twoD);
    QCOMPARE(spec->r1, at("A1"));
    QCOMPARE(spec->r2, at("B1"));
    wb.sheetAt(0).setDataTables({*spec});
    computeDataTable(wb, 0, *spec);
    QCOMPARE(number(wb, 0, "F4"), 10.0);
    QCOMPARE(number(wb, 0, "H4"), 30.0);
    QCOMPARE(number(wb, 0, "G5"), 40.0);
    QCOMPARE(number(wb, 0, "H6"), 90.0);
    QCOMPARE(number(wb, 0, "E3"), 6.0);
    put(wb, "H3", QStringLiteral("4"));
    QCOMPARE(number(wb, 0, "H4"), 40.0);
    QCOMPARE(number(wb, 0, "H6"), 120.0);
    QCOMPARE(dataTableFormulaText(*spec), QStringLiteral("{=TABLE(A1,B1)}"));
    QVERIFY(dataTableAt(wb.sheetAt(0), at("G5")) == &wb.sheetAt(0).dataTables().first());
    QVERIFY(dataTableAt(wb.sheetAt(0), at("E4")) == nullptr);
}

void CalcDataTablesTest::tableInputCellKeepsItsFormula()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Model"));
    put(wb, "D5", QStringLiteral("2"));
    put(wb, "A1", QStringLiteral("=D5"));
    put(wb, "B3", QStringLiteral("=A1*10"));
    put(wb, "A4", QStringLiteral("1"));
    put(wb, "A5", QStringLiteral("9"));
    const std::optional<DataTableSpec> spec = makeDataTable({at("A3"), at("B5")}, CellAddr{}, at("A1"));
    QVERIFY(spec.has_value());
    wb.sheetAt(0).setDataTables({*spec});
    wb.recalcAll();
    QCOMPARE(number(wb, 0, "B4"), 10.0);
    QCOMPARE(number(wb, 0, "B5"), 90.0);
    const Cell *input = wb.sheetAt(0).cellAt(at("A1"));
    QVERIFY(input != nullptr && input->hasFormula());
    QCOMPARE(number(wb, 0, "A1"), 2.0);
    put(wb, "D5", QStringLiteral("3")); // the input cell still follows its own formula
    QCOMPARE(number(wb, 0, "A1"), 3.0);
    QCOMPARE(number(wb, 0, "B3"), 30.0);
    QCOMPARE(number(wb, 0, "B4"), 10.0);
}

void CalcDataTablesTest::makeDataTableAndFormulaText()
{
    QString error;
    QVERIFY(!makeDataTable({at("A1"), at("A1")}, at("C1"), CellAddr{}, &error).has_value());
    QCOMPARE(error, QStringLiteral("Selection not valid."));
    QVERIFY(!makeDataTable({at("A3"), at("C6")}, CellAddr{}, CellAddr{}, &error).has_value());
    QCOMPARE(error, QStringLiteral("Input cell reference is not valid."));
    QVERIFY(!makeDataTable({at("A3"), at("C6")}, at("B4"), CellAddr{}, &error).has_value());
    QCOMPARE(error, QStringLiteral("Input cell reference is not valid."));
    DataTableSpec spec = *makeDataTable({at("A3"), at("C6")}, CellAddr{}, at("A1"));
    QCOMPARE(dataTableFormulaText(spec), QStringLiteral("{=TABLE(,A1)}"));
    QCOMPARE(spec.inputValues(false), (CellRange{at("A4"), at("A6")}));
    QCOMPARE(spec.inputValues(true), (CellRange{at("B3"), at("C3")}));
    spec.r1Deleted = true;
    QCOMPARE(dataTableFormulaText(spec), QStringLiteral("{=TABLE(,#REF!)}"));
}

void CalcDataTablesTest::tablesFollowStructuralEdits()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Model"));
    put(wb, "A1", QStringLiteral("2"));
    put(wb, "B3", QStringLiteral("=A1*10"));
    put(wb, "A4", QStringLiteral("1"));
    put(wb, "A5", QStringLiteral("2"));
    wb.sheetAt(0).setDataTables({*makeDataTable({at("A3"), at("B5")}, CellAddr{}, at("A1"))});
    wb.recalcAll();
    const Sheet &sheet = wb.sheetAt(0);
    // Excel refuses to cut through a table; whole lines of it may go.
    QVERIFY(dataTableSplitBy(sheet, true, 3, 1, true));   // a row inserted inside the results
    QVERIFY(dataTableSplitBy(sheet, true, 3, 1, false));  // one results row removed
    QVERIFY(!dataTableSplitBy(sheet, true, 2, 1, true));  // a row inserted above the formula row
    QVERIFY(!dataTableSplitBy(sheet, true, 2, 3, false)); // the whole table removed
    QVERIFY(!dataTableSplitBy(sheet, false, 3, 1, true)); // columns right of it
    applyStructureEdit(wb, {StructureOp::InsertRows, 0, 1, 2}); // two rows above the table, below A1
    QCOMPARE(wb.sheetAt(0).dataTables().first().ref, (CellRange{at("B6"), at("B7")}));
    QCOMPARE(wb.sheetAt(0).dataTables().first().r1, at("A1"));
    QCOMPARE(number(wb, 0, "B7"), 20.0);
    applyStructureEdit(wb, {StructureOp::InsertColumns, 0, 0, 1}); // a column left of everything
    const DataTableSpec moved = wb.sheetAt(0).dataTables().first();
    QCOMPARE(moved.ref, (CellRange{at("C6"), at("C7")}));
    QCOMPARE(moved.r1, at("B1"));
    QCOMPARE(number(wb, 0, "C6"), 10.0);
    applyStructureEdit(wb, {StructureOp::RemoveRows, 0, 0, 1}); // the input cell's row goes
    const DataTableSpec lost = wb.sheetAt(0).dataTables().first();
    QVERIFY(lost.r1Deleted);
    QCOMPARE(lost.ref, (CellRange{at("C5"), at("C6")}));
    QCOMPARE(wb.cellValue(0, 4, 2), Value(Err::Ref));
    QCOMPARE(dataTableFormulaText(lost), QStringLiteral("{=TABLE(,#REF!)}"));
    applyStructureEdit(wb, {StructureOp::RemoveRows, 0, 3, 3}); // the whole table goes
    QVERIFY(wb.sheetAt(0).dataTables().isEmpty());
}

void CalcDataTablesTest::twentyByTwentyTableIsQuick()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Loans"));
    put(wb, "A1", QStringLiteral("0.05"));  // rate
    put(wb, "A2", QStringLiteral("240"));   // months
    put(wb, "A3", QStringLiteral("200000"));
    put(wb, "C5", QStringLiteral("=PMT(A1/12,A2,-A3)"));
    for (int i = 0; i < 20; ++i) {
        QVERIFY(wb.setCellInput(0, {4, 3 + i}, QString::number(0.01 + i * 0.0025))); // rates across
        QVERIFY(wb.setCellInput(0, {5 + i, 2}, QString::number(60 + i * 12)));      // terms down
    }
    const std::optional<DataTableSpec> spec = makeDataTable({{4, 2}, {24, 22}}, at("A1"), at("A2"));
    QVERIFY(spec.has_value());
    wb.sheetAt(0).setDataTables({*spec});
    QElapsedTimer timer;
    timer.start();
    put(wb, "A3", QStringLiteral("250000")); // one edit: the whole table again
    const qint64 elapsed = timer.elapsed();
    QVERIFY2(elapsed < 1000, qPrintable(QStringLiteral("%1 ms").arg(elapsed)));
    // D6: rate 1 %, 60 months, 250000.
    const double r = 0.01 / 12;
    const double expected = 250000 * r / (1 - std::pow(1 + r, -60));
    QVERIFY(std::abs(toNumber(wb.cellValue(0, 5, 3)).value_or(0) - expected) < 1e-6);
    QCOMPARE(number(wb, 0, "A1"), 0.05);
    QCOMPARE(number(wb, 0, "A2"), 240.0);
}

QTEST_GUILESS_MAIN(CalcDataTablesTest)
#include "tst_calc_datatables.moc"
