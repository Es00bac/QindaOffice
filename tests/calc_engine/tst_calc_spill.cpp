// SPDX-License-Identifier: GPL-3.0-or-later
// M66 (D-075): dynamic arrays. FILTER, SORT, SORTBY, UNIQUE, SEQUENCE and
// RANDARRAY return arrays, and a formula whose result is an array spills it
// into the cells to its right and below, the way Excel 365 does: the spill
// follows its inputs, a blocked spill is #SPILL! until the blocker goes,
// editing the anchor or a spilled cell behaves like Excel, and formulas
// reading spilled cells see their values. Expected values are worked from
// Microsoft's documentation of each function (and agree with LibreOffice
// 25.2 where it implements them).

#include "structure.h"
#include "workbook.h"

#include <QtTest>

using namespace QindaOffice::Calc;

class CalcSpillTest : public QObject {
    Q_OBJECT
private slots:
    void sequenceSpills();
    void spillFeedsDependents();
    void blockedSpillShowsSpillError();
    void editingASpilledCellBlocksTheAnchor();
    void changingTheAnchorClearsItsSpill();
    void rangeResultsSpill();
    void filterSortAndUnique();
    void sortByAndRandArray();
    void spillMovesWithStructuralEdits();
};

namespace {

double number(const Workbook &wb, qint32 row, qint32 col, qint32 sheet = 0)
{
    const Value value = wb.cellValue(sheet, row, col);
    return isNumber(value) ? std::get<double>(value) : -99999.0;
}

QString text(const Workbook &wb, qint32 row, qint32 col, qint32 sheet = 0)
{
    return toText(wb.cellValue(sheet, row, col));
}

// A column of numbers from row 0.
void column(Workbook &wb, qint32 col, const QList<double> &values)
{
    for (qsizetype i = 0; i < values.size(); ++i) {
        wb.setCellValue(0, {qint32(i), col}, values.at(i));
    }
}

} // namespace

void CalcSpillTest::sequenceSpills()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("S"));
    QVERIFY(wb.setCellInput(0, {0, 1}, QStringLiteral("=SEQUENCE(3)")));
    QCOMPARE(number(wb, 0, 1), 1.0);
    QCOMPARE(number(wb, 1, 1), 2.0);
    QCOMPARE(number(wb, 2, 1), 3.0);
    QVERIFY(isBlank(wb.cellValue(0, 3, 1)));
    // rows x columns, start, step: row-major, like Excel.
    QVERIFY(wb.setCellInput(0, {0, 4}, QStringLiteral("=SEQUENCE(2,3,10,5)")));
    QCOMPARE(number(wb, 0, 4), 10.0);
    QCOMPARE(number(wb, 0, 6), 20.0);
    QCOMPARE(number(wb, 1, 4), 25.0);
    QCOMPARE(number(wb, 1, 6), 35.0);
}

void CalcSpillTest::spillFeedsDependents()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("S"));
    QVERIFY(wb.setCellInput(0, {0, 3}, QStringLiteral("=B2*10")));     // reads a spilled cell
    QVERIFY(wb.setCellInput(0, {0, 4}, QStringLiteral("=SUM(B1:B3)"))); // reads the whole spill
    QVERIFY(wb.setCellInput(0, {0, 1}, QStringLiteral("=SEQUENCE(3)")));
    QCOMPARE(number(wb, 0, 3), 20.0);
    QCOMPARE(number(wb, 0, 4), 6.0);
    // The spill follows its input and the readers follow the spill.
    QVERIFY(wb.setCellInput(0, {0, 0}, QStringLiteral("5")));
    QVERIFY(wb.setCellInput(0, {0, 1}, QStringLiteral("=SEQUENCE(3,1,A1)")));
    QCOMPARE(number(wb, 0, 3), 60.0);
    QCOMPARE(number(wb, 0, 4), 18.0);
    QVERIFY(wb.setCellInput(0, {0, 0}, QStringLiteral("10")));
    QCOMPARE(number(wb, 1, 1), 11.0);
    QCOMPARE(number(wb, 0, 3), 110.0);
}

void CalcSpillTest::blockedSpillShowsSpillError()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("S"));
    QVERIFY(wb.setCellInput(0, {1, 1}, QStringLiteral("in the way")));
    QVERIFY(wb.setCellInput(0, {0, 1}, QStringLiteral("=SEQUENCE(3)")));
    QCOMPARE(text(wb, 0, 1), QStringLiteral("#SPILL!"));
    QCOMPARE(text(wb, 1, 1), QStringLiteral("in the way")); // never overwritten
    QVERIFY(isBlank(wb.cellValue(0, 2, 1)));
    // Clearing the blocker (the app's Delete: drop the cell, recalculate
    // from it) lets the array spill.
    QVERIFY(wb.sheetAt(0).clearCell({1, 1}));
    wb.dependencies().updateCell(CellKey(0, 1, 1), {});
    wb.recalc({CellKey(0, 1, 1)});
    QCOMPARE(number(wb, 0, 1), 1.0);
    QCOMPARE(number(wb, 2, 1), 3.0);
}

void CalcSpillTest::editingASpilledCellBlocksTheAnchor()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("S"));
    QVERIFY(wb.setCellInput(0, {0, 0}, QStringLiteral("=SEQUENCE(3)")));
    QVERIFY(wb.setCellInput(0, {2, 0}, QStringLiteral("typed")));
    QCOMPARE(text(wb, 0, 0), QStringLiteral("#SPILL!"));
    QVERIFY(isBlank(wb.cellValue(0, 1, 0)));
    QCOMPARE(text(wb, 2, 0), QStringLiteral("typed"));
}

void CalcSpillTest::changingTheAnchorClearsItsSpill()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("S"));
    QVERIFY(wb.setCellInput(0, {0, 0}, QStringLiteral("=SEQUENCE(4)")));
    QCOMPARE(number(wb, 3, 0), 4.0);
    QVERIFY(wb.setCellInput(0, {0, 0}, QStringLiteral("=SEQUENCE(2)")));
    QCOMPARE(number(wb, 1, 0), 2.0);
    QVERIFY(isBlank(wb.cellValue(0, 2, 0)));
    QVERIFY(isBlank(wb.cellValue(0, 3, 0)));
    QVERIFY(wb.setCellInput(0, {0, 0}, QStringLiteral("7")));
    QVERIFY(isBlank(wb.cellValue(0, 1, 0)));
}

void CalcSpillTest::rangeResultsSpill()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("S"));
    column(wb, 0, {4, 5, 6});
    QVERIFY(wb.setCellInput(0, {0, 2}, QStringLiteral("=A1:A3*2")));
    QCOMPARE(number(wb, 2, 2), 12.0);
    QVERIFY(wb.setCellInput(0, {0, 3}, QStringLiteral("=A1:A3")));
    QCOMPARE(number(wb, 1, 3), 5.0);
}

void CalcSpillTest::filterSortAndUnique()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("S"));
    // A1:A6 regions, B1:B6 amounts.
    const QStringList regions{QStringLiteral("East"), QStringLiteral("west"), QStringLiteral("East"),
                              QStringLiteral("North"), QStringLiteral("West"), QStringLiteral("South")};
    const QList<double> amounts{10, 25, 30, 5, 40, 15};
    for (int i = 0; i < 6; ++i) {
        wb.setCellValue(0, {i, 0}, regions.at(i));
        wb.setCellValue(0, {i, 1}, amounts.at(i));
    }
    // FILTER keeps the rows whose include value is TRUE, in order.
    QVERIFY(wb.setCellInput(0, {0, 3}, QStringLiteral("=FILTER(A1:B6,B1:B6>20)")));
    QCOMPARE(text(wb, 0, 3), QStringLiteral("west"));
    QCOMPARE(number(wb, 0, 4), 25.0);
    QCOMPARE(text(wb, 2, 3), QStringLiteral("West"));
    QCOMPARE(number(wb, 2, 4), 40.0);
    QVERIFY(isBlank(wb.cellValue(0, 3, 3)));
    // Nothing matches: if_empty, or #CALC! without it.
    QVERIFY(wb.setCellInput(0, {10, 3}, QStringLiteral("=FILTER(A1:A6,B1:B6>100,\"none\")")));
    QCOMPARE(text(wb, 10, 3), QStringLiteral("none"));
    QVERIFY(wb.setCellInput(0, {11, 3}, QStringLiteral("=FILTER(A1:A6,B1:B6>100)")));
    QCOMPARE(text(wb, 11, 3), QStringLiteral("#CALC!"));
    // UNIQUE compares text without regard to case, keeping the first.
    QVERIFY(wb.setCellInput(0, {0, 6}, QStringLiteral("=UNIQUE(A1:A6)")));
    QCOMPARE(text(wb, 0, 6), QStringLiteral("East"));
    QCOMPARE(text(wb, 1, 6), QStringLiteral("west"));
    QCOMPARE(text(wb, 2, 6), QStringLiteral("North"));
    QCOMPARE(text(wb, 3, 6), QStringLiteral("South"));
    QVERIFY(isBlank(wb.cellValue(0, 4, 6)));
    // exactly_once: the values that occur once only.
    QVERIFY(wb.setCellInput(0, {0, 7}, QStringLiteral("=UNIQUE(A1:A6,FALSE,TRUE)")));
    QCOMPARE(text(wb, 0, 7), QStringLiteral("North"));
    QCOMPARE(text(wb, 1, 7), QStringLiteral("South"));
    // SORT by the second column, descending; text sorts without case.
    QVERIFY(wb.setCellInput(0, {0, 9}, QStringLiteral("=SORT(A1:B6,2,-1)")));
    QCOMPARE(number(wb, 0, 10), 40.0);
    QCOMPARE(text(wb, 0, 9), QStringLiteral("West"));
    QCOMPARE(number(wb, 5, 10), 5.0);
    QVERIFY(wb.setCellInput(0, {0, 12}, QStringLiteral("=SORT(A1:A6)")));
    QCOMPARE(text(wb, 0, 12), QStringLiteral("East"));
    QCOMPARE(text(wb, 2, 12), QStringLiteral("North"));
    QCOMPARE(text(wb, 3, 12), QStringLiteral("South"));
}

void CalcSpillTest::sortByAndRandArray()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("S"));
    for (int i = 0; i < 4; ++i) {
        wb.setCellValue(0, {i, 0}, QStringLiteral("item%1").arg(i + 1));
        wb.setCellValue(0, {i, 1}, double((i * 7) % 5)); // 0 2 4 1
    }
    QVERIFY(wb.setCellInput(0, {0, 3}, QStringLiteral("=SORTBY(A1:A4,B1:B4,-1)")));
    QCOMPARE(text(wb, 0, 3), QStringLiteral("item3"));
    QCOMPARE(text(wb, 1, 3), QStringLiteral("item2"));
    QCOMPARE(text(wb, 2, 3), QStringLiteral("item4"));
    QCOMPARE(text(wb, 3, 3), QStringLiteral("item1"));
    QVERIFY(wb.setCellInput(0, {0, 5}, QStringLiteral("=RANDARRAY(2,3,1,6,TRUE)")));
    for (int r = 0; r < 2; ++r) {
        for (int c = 5; c < 8; ++c) {
            const double v = number(wb, r, c);
            QVERIFY2(v >= 1 && v <= 6 && v == std::floor(v), qPrintable(QString::number(v)));
        }
    }
    QVERIFY(isBlank(wb.cellValue(0, 2, 5)));
}

void CalcSpillTest::spillMovesWithStructuralEdits()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("S"));
    QVERIFY(wb.setCellInput(0, {0, 1}, QStringLiteral("=SEQUENCE(3)")));
    QVERIFY(wb.setCellInput(0, {0, 2}, QStringLiteral("=B3")));
    applyStructureEdit(wb, StructureEdit{StructureOp::InsertRows, 0, 0, 1});
    // The anchor is now B2 and spills B2:B4; nothing is left in B1.
    QVERIFY(isBlank(wb.cellValue(0, 0, 1)));
    QCOMPARE(number(wb, 1, 1), 1.0);
    QCOMPARE(number(wb, 3, 1), 3.0);
    QVERIFY(isBlank(wb.cellValue(0, 4, 1)));
    // The reader followed its reference (B3 → B4) and still sees 3.
    QCOMPARE(number(wb, 1, 2), 3.0);
}

QTEST_GUILESS_MAIN(CalcSpillTest)
#include "tst_calc_spill.moc"
