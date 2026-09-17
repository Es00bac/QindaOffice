// SPDX-License-Identifier: GPL-3.0-or-later
// Structural edits (calc_engine/structure.h): inserting and removing rows
// and columns, sheet-list operations and range sorting.
//
// The point of these cases is the part that is easy to get wrong and
// invisible until a user loses data: cells move, but so must every formula
// that pointed at them, the merges, the explicit geometry, the named
// ranges, and the dependency index the recalculation runs on.

#include "structure.h"

#include "sheet.h"
#include "workbook.h"

#include <QCoreApplication>
#include <QtTest>

using namespace QindaOffice::Calc;

namespace {

CellAddr at(qint32 row, qint32 col)
{
    return {row, col};
}

QString formulaOf(Workbook &book, qint32 sheet, const CellAddr &addr)
{
    const Cell *cell = book.sheetAt(sheet).cellAt(addr);
    return cell != nullptr ? cell->formula : QString();
}

double numberAt(Workbook &book, qint32 sheet, const CellAddr &addr)
{
    return toNumber(book.cellValue(sheet, addr.row, addr.col)).value_or(-12345);
}

} // namespace

class CalcStructureTest : public QObject {
    Q_OBJECT
private slots:
    void insertRowsMovesCellsAndFormulas();
    void removeRowsKillsReferencesIntoTheGap();
    void insertColumnsGrowsStraddlingRanges();
    void removeColumnsMovesStylesAndGeometry();
    void mergesFollowTheEdit();
    void namedRangesFollowTheEdit();
    void crossSheetReferencesFollowTheEdit();
    void renameSheetRewritesReferences();
    void removeSheetBreaksItsReferencesOnly();
    void removeSheetRefusesTheLastOne();
    void sortRangeMovesRowsAndShiftsFormulas();
    void sortRangeKeepsHeaderAndSinksBlanks();
    void recalcAllRefreshesOffDiagonalCells();
};

void CalcStructureTest::insertRowsMovesCellsAndFormulas()
{
    Workbook book;
    book.addSheet(QStringLiteral("Sheet1"));
    book.setCellInput(0, at(0, 0), QStringLiteral("10"));   // A1
    book.setCellInput(0, at(4, 0), QStringLiteral("20"));   // A5
    book.setCellInput(0, at(5, 0), QStringLiteral("=A5*2")); // A6
    book.setCellInput(0, at(6, 0), QStringLiteral("=SUM(A1:A5)")); // A7

    applyStructureEdit(book, {StructureOp::InsertRows, 0, 2, 1});

    // A5 moved to A6, so its dependent moved to A7 and now names A6.
    QCOMPARE(numberAt(book, 0, at(5, 0)), 20.0);
    QCOMPARE(formulaOf(book, 0, at(6, 0)), QStringLiteral("A6*2"));
    QCOMPARE(numberAt(book, 0, at(6, 0)), 40.0);
    // The SUM straddled the insertion point, so its range grew with it.
    QCOMPARE(formulaOf(book, 0, at(7, 0)), QStringLiteral("SUM(A1:A6)"));
    QCOMPARE(numberAt(book, 0, at(7, 0)), 30.0);
    // A value typed into the new blank row recalculates the SUM, which is
    // only true if the dependency index was rebuilt after the move.
    book.setCellInput(0, at(2, 0), QStringLiteral("5"));
    QCOMPARE(numberAt(book, 0, at(7, 0)), 35.0);
}

void CalcStructureTest::removeRowsKillsReferencesIntoTheGap()
{
    Workbook book;
    book.addSheet(QStringLiteral("Sheet1"));
    book.setCellInput(0, at(0, 0), QStringLiteral("1"));
    book.setCellInput(0, at(1, 0), QStringLiteral("2"));   // A2, about to go
    book.setCellInput(0, at(2, 0), QStringLiteral("3"));
    book.setCellInput(0, at(4, 0), QStringLiteral("=A2+1"));
    book.setCellInput(0, at(5, 0), QStringLiteral("=SUM(A1:A3)"));

    applyStructureEdit(book, {StructureOp::RemoveRows, 0, 1, 1});

    // The reference to the deleted cell must say so, not silently re-point
    // at whatever slid up into A2. Only the dead REFERENCE is replaced, the
    // rest of the expression survives — the spelling every spreadsheet uses.
    QCOMPARE(formulaOf(book, 0, at(3, 0)), QStringLiteral("#REF!+1"));
    QVERIFY(isErr(book.cellValue(0, 3, 0)));
    // The straddling range shrank instead.
    QCOMPARE(formulaOf(book, 0, at(4, 0)), QStringLiteral("SUM(A1:A2)"));
    QCOMPARE(numberAt(book, 0, at(4, 0)), 4.0);
}

void CalcStructureTest::insertColumnsGrowsStraddlingRanges()
{
    Workbook book;
    book.addSheet(QStringLiteral("Sheet1"));
    book.setCellInput(0, at(0, 0), QStringLiteral("1")); // A1
    book.setCellInput(0, at(0, 2), QStringLiteral("3")); // C1
    book.setCellInput(0, at(0, 3), QStringLiteral("=SUM(A1:C1)"));

    applyStructureEdit(book, {StructureOp::InsertColumns, 0, 1, 2});

    QCOMPARE(numberAt(book, 0, at(0, 4)), 3.0); // C1 -> E1
    QCOMPARE(formulaOf(book, 0, at(0, 5)), QStringLiteral("SUM(A1:E1)"));
    QCOMPARE(numberAt(book, 0, at(0, 5)), 4.0);
}

void CalcStructureTest::removeColumnsMovesStylesAndGeometry()
{
    Workbook book;
    Sheet &sheet = book.addSheet(QStringLiteral("Sheet1"));
    const qint32 styleId = book.addStyle(CellStyle{});
    sheet.setStyleIdAt(at(0, 3), styleId); // a style on an EMPTY cell
    sheet.setColumnWidth(3, 200);
    sheet.setColumnWidth(0, 50);

    applyStructureEdit(book, {StructureOp::RemoveColumns, 0, 1, 1});

    QCOMPARE(book.sheetAt(0).styleIdAt(at(0, 2)), styleId);
    QCOMPARE(book.sheetAt(0).styleIdAt(at(0, 3)), 0);
    QCOMPARE(book.sheetAt(0).columnWidth(2), 200.0);
    QCOMPARE(book.sheetAt(0).columnWidth(0), 50.0);
}

void CalcStructureTest::mergesFollowTheEdit()
{
    Workbook book;
    Sheet &sheet = book.addSheet(QStringLiteral("Sheet1"));
    sheet.addMerge({at(4, 0), at(4, 2)}); // A5:C5
    sheet.addMerge({at(1, 0), at(1, 1)}); // A2:B2, about to be deleted

    applyStructureEdit(book, {StructureOp::RemoveRows, 0, 1, 1});

    const QList<CellRange> merges = book.sheetAt(0).merges();
    QCOMPARE(merges.size(), 1);
    QCOMPARE(merges.first().topLeft.row, 3);
    QCOMPARE(merges.first().bottomRight.col, 2);
}

void CalcStructureTest::namedRangesFollowTheEdit()
{
    Workbook book;
    book.addSheet(QStringLiteral("Sheet1"));
    book.setCellInput(0, at(4, 0), QStringLiteral("7"));
    RangeRef ref;
    ref.sheet = QStringLiteral("Sheet1");
    ref.range = {at(4, 0), at(4, 0)};
    book.defineName(QStringLiteral("TOTAL"), ref);
    book.setCellInput(0, at(0, 1), QStringLiteral("=TOTAL"));
    QCOMPARE(numberAt(book, 0, at(0, 1)), 7.0);

    applyStructureEdit(book, {StructureOp::InsertRows, 0, 0, 2});

    const auto moved = book.namedRange(QStringLiteral("TOTAL"));
    QVERIFY(moved.has_value());
    QCOMPARE(moved->range.topLeft.row, 6);
    // The name still resolves to the value it was defined over.
    QCOMPARE(numberAt(book, 0, at(2, 1)), 7.0);
}

void CalcStructureTest::crossSheetReferencesFollowTheEdit()
{
    Workbook book;
    book.addSheet(QStringLiteral("Data"));
    book.addSheet(QStringLiteral("Report"));
    book.setCellInput(0, at(4, 0), QStringLiteral("42"));
    book.setCellInput(1, at(0, 0), QStringLiteral("=Data!A5"));
    QCOMPARE(numberAt(book, 1, at(0, 0)), 42.0);

    applyStructureEdit(book, {StructureOp::InsertRows, 0, 0, 1});

    // The formula lives on Report but names Data, so it must follow Data's
    // edit — the case a "same sheet only" rewrite silently gets wrong.
    QCOMPARE(formulaOf(book, 1, at(0, 0)), QStringLiteral("Data!A6"));
    QCOMPARE(numberAt(book, 1, at(0, 0)), 42.0);
}

void CalcStructureTest::renameSheetRewritesReferences()
{
    Workbook book;
    book.addSheet(QStringLiteral("Data"));
    book.addSheet(QStringLiteral("Report"));
    book.setCellInput(0, at(0, 0), QStringLiteral("5"));
    book.setCellInput(1, at(0, 0), QStringLiteral("=Data!A1*2"));

    QVERIFY(renameSheet(book, 0, QStringLiteral("Figures")));

    QCOMPARE(book.sheetName(0), QStringLiteral("Figures"));
    QCOMPARE(book.sheetIndex(QStringLiteral("Data")), -1);
    QCOMPARE(formulaOf(book, 1, at(0, 0)), QStringLiteral("Figures!A1*2"));
    QCOMPARE(numberAt(book, 1, at(0, 0)), 10.0);
}

void CalcStructureTest::removeSheetBreaksItsReferencesOnly()
{
    Workbook book;
    book.addSheet(QStringLiteral("Data"));
    book.addSheet(QStringLiteral("Other"));
    book.addSheet(QStringLiteral("Report"));
    book.setCellInput(1, at(0, 0), QStringLiteral("9"));
    book.setCellInput(2, at(0, 0), QStringLiteral("=Data!A1"));
    book.setCellInput(2, at(1, 0), QStringLiteral("=Other!A1"));

    QVERIFY(removeSheet(book, 0));

    QCOMPARE(book.sheetCount(), 2);
    QCOMPARE(formulaOf(book, 1, at(0, 0)), QStringLiteral("#REF!"));
    // The surviving sheet is now index 0; the reference to it still works,
    // which only holds if the dependency index was rebuilt by index.
    QCOMPARE(formulaOf(book, 1, at(1, 0)), QStringLiteral("Other!A1"));
    QCOMPARE(numberAt(book, 1, at(1, 0)), 9.0);
    book.setCellInput(0, at(0, 0), QStringLiteral("11"));
    QCOMPARE(numberAt(book, 1, at(1, 0)), 11.0);
}

void CalcStructureTest::removeSheetRefusesTheLastOne()
{
    Workbook book;
    book.addSheet(QStringLiteral("Sheet1"));
    QVERIFY(!removeSheet(book, 0));
    QCOMPARE(book.sheetCount(), 1);
}

void CalcStructureTest::sortRangeMovesRowsAndShiftsFormulas()
{
    Workbook book;
    book.addSheet(QStringLiteral("Sheet1"));
    // A: key, B: a formula that must keep meaning "my own A times 10".
    const QStringList keys = {QStringLiteral("3"), QStringLiteral("1"), QStringLiteral("2")};
    for (int r = 0; r < keys.size(); ++r) {
        book.setCellInput(0, at(r, 0), keys.at(r));
        book.setCellInput(0, at(r, 1), QStringLiteral("=A%1*10").arg(r + 1));
    }

    sortRange(book, 0, {at(0, 0), at(2, 1)}, 0, true, false);

    QCOMPARE(numberAt(book, 0, at(0, 0)), 1.0);
    QCOMPARE(numberAt(book, 0, at(1, 0)), 2.0);
    QCOMPARE(numberAt(book, 0, at(2, 0)), 3.0);
    QCOMPARE(formulaOf(book, 0, at(0, 1)), QStringLiteral("A1*10"));
    QCOMPARE(numberAt(book, 0, at(0, 1)), 10.0);
    QCOMPARE(numberAt(book, 0, at(2, 1)), 30.0);
}

void CalcStructureTest::sortRangeKeepsHeaderAndSinksBlanks()
{
    Workbook book;
    book.addSheet(QStringLiteral("Sheet1"));
    book.setCellInput(0, at(0, 0), QStringLiteral("Name"));
    book.setCellInput(0, at(1, 0), QStringLiteral("2"));
    // row 2 deliberately blank
    book.setCellInput(0, at(3, 0), QStringLiteral("1"));

    sortRange(book, 0, {at(0, 0), at(3, 0)}, 0, false, true);

    QCOMPARE(toText(book.cellValue(0, 0, 0)), QStringLiteral("Name"));
    QCOMPARE(numberAt(book, 0, at(1, 0)), 2.0);
    QCOMPARE(numberAt(book, 0, at(2, 0)), 1.0);
    QVERIFY(isBlank(book.cellValue(0, 3, 0)));
}

void CalcStructureTest::recalcAllRefreshesOffDiagonalCells()
{
    Workbook book;
    book.addSheet(QStringLiteral("Sheet1"));
    // B1 is off the diagonal (row 0, col 1): the transposed key this used
    // to build pointed at A2, which holds nothing, so recalcAll skipped it.
    book.setCellInput(0, at(0, 0), QStringLiteral("4"));
    book.installFormula(0, at(0, 1), parseFormula(QStringLiteral("A1*3")).root,
                        QStringLiteral("A1*3"));
    book.sheetAt(0).ensureCell(at(0, 1)).value = Value(std::monostate{});

    book.recalcAll();

    QCOMPARE(numberAt(book, 0, at(0, 1)), 12.0);
}

QTEST_MAIN(CalcStructureTest)
#include "tst_calc_structure.moc"
