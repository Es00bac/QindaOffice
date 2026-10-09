// SPDX-License-Identifier: GPL-3.0-or-later
// M69 (D-078): arrays the way Excel computes them. Array constants
// ({1,2;3,4}) parse, evaluate and print; a dynamic-array formula applies a
// one-value function to each element of a range (LEN(A1:A3) spills three
// lengths); a formula Excel stored without the dynamic-array mark keeps
// pre-2019 implicit intersection (the cell in its own row — '@' in Excel
// 365); a legacy Ctrl+Shift+Enter formula fills its fixed block; a defined
// name's relative references follow the cell using it. Expected values
// are worked from Microsoft's documentation.

#include "structure.h"
#include "workbook.h"

#include <QtTest>

using namespace QindaOffice::Calc;

class CalcArraysTest : public QObject {
    Q_OBJECT
private slots:
    void arrayConstants();
    void implicitIntersectionOperator();
    void dynamicFormulasLift();
    void legacyFormulasIntersect();
    void legacyArrayBlocks();
    void arrayBlocksMoveWithEdits();
    void relativeNamesFollowTheUsingCell();
    void arrayFunctions();
    void shapingFunctions();
    void legacyArrayParameters();
};

namespace {

Value value(const Workbook &book, qint32 row, qint32 col)
{
    return book.cellValue(0, row, col);
}

Workbook book123()
{
    Workbook book;
    book.addSheet(QStringLiteral("Sheet1"));
    book.setCellValue(0, {0, 0}, 1.0);
    book.setCellValue(0, {1, 0}, 2.0);
    book.setCellValue(0, {2, 0}, 3.0);
    return book;
}

void legacy(Workbook &book, qint32 row, qint32 col, const QString &formula)
{
    const ParseResult parsed = parseFormula(formula);
    QVERIFY2(parsed.error.isEmpty(), qPrintable(formula));
    book.installFormula(0, {row, col}, parsed.root, formula, false);
}

} // namespace

void CalcArraysTest::arrayConstants()
{
    for (const char *text : {"{1,2;3,4}", "{\"a\",TRUE;#N/A,-2.5}", "SUM({1,2,3})*2", "{1;2;3}"}) {
        const ParseResult parsed = parseFormula(QString::fromLatin1(text));
        QVERIFY2(parsed.error.isEmpty(), text);
        QCOMPARE(nodeText(parsed.root), QString::fromLatin1(text));
    }
    QVERIFY(!parseFormula(QStringLiteral("{1,2;3}")).error.isEmpty());  // ragged rows
    QVERIFY(!parseFormula(QStringLiteral("{1,A1}")).error.isEmpty());   // constants only
    QVERIFY(!parseFormula(QStringLiteral("{1,2")).error.isEmpty());     // unterminated
    Workbook book = book123();
    QVERIFY(book.setCellInput(0, {0, 2}, QStringLiteral("=SUM({1,2;3,4})")));
    QCOMPARE(value(book, 0, 2), Value(10.0));
    QVERIFY(book.setCellInput(0, {1, 2}, QStringLiteral("=INDEX({10,20,30},2)")));
    QCOMPARE(value(book, 1, 2), Value(20.0));
    // A typed array constant spills, row by row.
    QVERIFY(book.setCellInput(0, {4, 2}, QStringLiteral("={1,2;3,4}")));
    QCOMPARE(value(book, 4, 3), Value(2.0));
    QCOMPARE(value(book, 5, 2), Value(3.0));
    QCOMPARE(value(book, 5, 3), Value(4.0));
    // An array constant times a range: element by element.
    QVERIFY(book.setCellInput(0, {0, 6}, QStringLiteral("=SUMPRODUCT(A1:A3*{1;10;100})")));
    QCOMPARE(value(book, 0, 6), Value(321.0));
}

void CalcArraysTest::implicitIntersectionOperator()
{
    for (const char *text : {"@A1:A3", "-@A1:A3", "@(A1+B1)", "@INDEX(A1:B2,1,1)", "SUM(@A1:A3)"}) {
        const ParseResult parsed = parseFormula(QString::fromLatin1(text));
        QVERIFY2(parsed.error.isEmpty(), text);
        QCOMPARE(nodeText(parsed.root), QString::fromLatin1(text));
    }
    Workbook book = book123();
    QVERIFY(book.setCellInput(0, {1, 1}, QStringLiteral("=@A1:A3*10"))); // B2: the row's own cell
    QCOMPARE(value(book, 1, 1), Value(20.0));
    QVERIFY(book.setCellInput(0, {5, 1}, QStringLiteral("=@A1:A3")));    // B6: no cell in its row
    QCOMPARE(value(book, 5, 1), Value(Err::Value));
    QVERIFY(book.setCellInput(0, {1, 2}, QStringLiteral("=@{7,8,9}")));  // an array: its first element
    QCOMPARE(value(book, 1, 2), Value(7.0));
}

void CalcArraysTest::dynamicFormulasLift()
{
    Workbook book;
    book.addSheet(QStringLiteral("Sheet1"));
    book.setCellValue(0, {0, 0}, QStringLiteral("a"));
    book.setCellValue(0, {1, 0}, QStringLiteral("bbb"));
    book.setCellValue(0, {2, 0}, QStringLiteral("cc"));
    // LEN over a range spills one length per cell.
    QVERIFY(book.setCellInput(0, {0, 1}, QStringLiteral("=LEN(A1:A3)")));
    QCOMPARE(value(book, 0, 1), Value(1.0));
    QCOMPARE(value(book, 1, 1), Value(3.0));
    QCOMPARE(value(book, 2, 1), Value(2.0));
    // IF lifts its condition and the branches with it.
    QVERIFY(book.setCellInput(0, {0, 2}, QStringLiteral("=IF(LEN(A1:A3)>1,A1:A3,\"-\")")));
    QCOMPARE(value(book, 0, 2), Value(QStringLiteral("-")));
    QCOMPARE(value(book, 1, 2), Value(QStringLiteral("bbb")));
    QCOMPARE(value(book, 2, 2), Value(QStringLiteral("cc")));
    // A row times a column: every pair (Excel's broadcast).
    QVERIFY(book.setCellInput(0, {4, 0}, QStringLiteral("=MID(\"abcdef\",{1;3},{1,2})")));
    QCOMPARE(value(book, 4, 0), Value(QStringLiteral("a")));
    QCOMPARE(value(book, 4, 1), Value(QStringLiteral("ab")));
    QCOMPARE(value(book, 5, 0), Value(QStringLiteral("c")));
    QCOMPARE(value(book, 5, 1), Value(QStringLiteral("cd")));
    // Lifted inside an aggregate: no spill, one answer.
    QVERIFY(book.setCellInput(0, {7, 0}, QStringLiteral("=SUM(LEN(A1:A3))")));
    QCOMPARE(value(book, 7, 0), Value(6.0));
    // A lookup over several keys returns one result per key.
    QVERIFY(book.setCellInput(0, {8, 0}, QStringLiteral("=MATCH({\"cc\";\"a\"},A1:A3,0)")));
    QCOMPARE(value(book, 8, 0), Value(3.0));
    QCOMPARE(value(book, 9, 0), Value(1.0));
}

void CalcArraysTest::legacyFormulasIntersect()
{
    Workbook book = book123();
    // Stored by Excel without the dynamic-array mark: implicit intersection.
    legacy(book, 1, 1, QStringLiteral("A1:A3*2"));                 // B2
    QCOMPARE(value(book, 1, 1), Value(4.0));
    legacy(book, 5, 1, QStringLiteral("A1:A3*2"));                 // B6: outside the rows
    QCOMPARE(value(book, 5, 1), Value(Err::Value));
    legacy(book, 2, 2, QStringLiteral("LEN(A1:A3)"));              // C3: LEN(A3)
    QCOMPARE(value(book, 2, 2), Value(1.0));
    legacy(book, 0, 3, QStringLiteral("IF(A1:A3>1,\"y\",\"n\")")); // D1: A1 > 1?
    QCOMPARE(value(book, 0, 3), Value(QStringLiteral("n")));
    legacy(book, 2, 3, QStringLiteral("IF(A1:A3>1,\"y\",\"n\")")); // D3: A3 > 1?
    QCOMPARE(value(book, 2, 3), Value(QStringLiteral("y")));
    // Aggregates still read the whole range; a range result intersects.
    legacy(book, 1, 4, QStringLiteral("SUM(A1:A3)"));
    QCOMPARE(value(book, 1, 4), Value(6.0));
    legacy(book, 2, 5, QStringLiteral("OFFSET(A1,0,0,3,1)"));      // F3: A3
    QCOMPARE(value(book, 2, 5), Value(3.0));
    legacy(book, 0, 6, QStringLiteral("{5,6,7}"));                 // an array: its first element
    QCOMPARE(value(book, 0, 6), Value(5.0));
    // The intersected cell is a dependency like any other.
    book.setCellValue(0, {1, 0}, 20.0);
    QCOMPARE(value(book, 1, 1), Value(40.0));
}

void CalcArraysTest::legacyArrayBlocks()
{
    Workbook book = book123();
    const auto install = [&book](const CellRange &block, const QString &formula) {
        const ParseResult parsed = parseFormula(formula);
        QVERIFY2(parsed.error.isEmpty(), qPrintable(formula));
        book.installArrayFormula(0, block, parsed.root, formula);
    };
    install({{0, 1}, {2, 1}}, QStringLiteral("A1:A3*2")); // {=A1:A3*2} in B1:B3
    QCOMPARE(value(book, 0, 1), Value(2.0));
    QCOMPARE(value(book, 1, 1), Value(4.0));
    QCOMPARE(value(book, 2, 1), Value(6.0));
    QCOMPARE(book.arrayBlockAt(0, {1, 1}), std::optional<CellRange>(CellRange{{0, 1}, {2, 1}}));
    QVERIFY(!book.arrayBlockAt(0, {3, 1}).has_value());
    book.setCellValue(0, {2, 0}, 10.0);
    QCOMPARE(value(book, 2, 1), Value(20.0));
    // A block larger than the result: #N/A beyond it; a scalar fills.
    install({{0, 2}, {3, 2}}, QStringLiteral("A1:A3+0"));
    QCOMPARE(value(book, 3, 2), Value(Err::NA));
    install({{0, 3}, {1, 4}}, QStringLiteral("5"));
    QCOMPARE(value(book, 1, 4), Value(5.0));
    // One cell, array semantics: the classic {=SUM(IF(…))}.
    install({{6, 0}, {6, 0}}, QStringLiteral("SUM(IF(A1:A3>1,A1:A3))"));
    QCOMPARE(value(book, 6, 0), Value(12.0));
    // Writing the anchor lets go of the block.
    book.setCellValue(0, {0, 1}, 1.0);
    QVERIFY(!book.arrayBlockAt(0, {1, 1}).has_value());
    QVERIFY(isBlank(value(book, 1, 1)));
    // Dependents of the block's cells follow it.
    install({{0, 1}, {2, 1}}, QStringLiteral("A1:A3*2"));
    QVERIFY(book.setCellInput(0, {9, 1}, QStringLiteral("=B2+1")));
    QCOMPARE(value(book, 9, 1), Value(5.0));
    book.setCellValue(0, {1, 0}, 4.0);
    QCOMPARE(value(book, 9, 1), Value(9.0));
}

void CalcArraysTest::arrayBlocksMoveWithEdits()
{
    Workbook book = book123();
    const ParseResult parsed = parseFormula(QStringLiteral("A1:A3*2"));
    book.installArrayFormula(0, {{0, 1}, {2, 1}}, parsed.root, QStringLiteral("A1:A3*2"));
    applyStructureEdit(book, {StructureOp::InsertRows, 0, 0, 2});
    QCOMPARE(book.arrayBlockAt(0, {3, 1}), std::optional<CellRange>(CellRange{{2, 1}, {4, 1}}));
    QCOMPARE(book.sheetAt(0).cellAt({2, 1})->formula, QStringLiteral("A3:A5*2"));
    QCOMPARE(value(book, 4, 1), Value(6.0));
    QVERIFY(isBlank(value(book, 0, 1)));
}

void CalcArraysTest::relativeNamesFollowTheUsingCell()
{
    Workbook book = book123();
    book.setCellValue(0, {1, 1}, 50.0);
    // Excel stores a relative name as if used in A1: "the cell to the
    // left" is XFD1 (wrapping), "the cell above" A1048576.
    book.setDefinedNames({DefinedName{QStringLiteral("LeftCell"), -1, QStringLiteral("Sheet1!XFD1"), {}, false, {}},
                          DefinedName{QStringLiteral("Above"), -1, QStringLiteral("Sheet1!A1048576"), {}, false, {}}});
    QVERIFY(book.setCellInput(0, {1, 2}, QStringLiteral("=LeftCell*2"))); // C2: B2 * 2
    QCOMPARE(value(book, 1, 2), Value(100.0));
    QVERIFY(book.setCellInput(0, {3, 0}, QStringLiteral("=Above+1")));    // A4: A3 + 1
    QCOMPARE(value(book, 3, 0), Value(4.0));
    book.setCellValue(0, {2, 0}, 30.0);
    QCOMPARE(value(book, 3, 0), Value(31.0)); // and depends on it
    // Absolute names stay where they are.
    RangeRef fixed;
    fixed.sheet = QStringLiteral("Sheet1");
    fixed.range = {{0, 0}, {0, 0}};
    fixed.absRow0 = fixed.absCol0 = fixed.absRow1 = fixed.absCol1 = true;
    book.defineName(QStringLiteral("First"), fixed);
    QVERIFY(book.setCellInput(0, {5, 5}, QStringLiteral("=First")));
    QCOMPARE(value(book, 5, 5), Value(1.0));
}

void CalcArraysTest::arrayFunctions()
{
    Workbook book = book123();
    QVERIFY(book.setCellInput(0, {0, 2}, QStringLiteral("=TRANSPOSE(A1:A3)")));
    QCOMPARE(value(book, 0, 3), Value(2.0));
    QCOMPARE(value(book, 0, 4), Value(3.0));
    QVERIFY(book.setCellInput(0, {3, 2}, QStringLiteral("=MMULT({1,2;3,4},{5;6})")));
    QCOMPARE(value(book, 3, 2), Value(17.0));
    QCOMPARE(value(book, 4, 2), Value(39.0));
    QVERIFY(book.setCellInput(0, {6, 2}, QStringLiteral("=MMULT({1,2},{1,2})")));
    QCOMPARE(value(book, 6, 2), Value(Err::Value));
}

void CalcArraysTest::shapingFunctions()
{
    Workbook book;
    book.addSheet(QStringLiteral("Sheet1"));
    const auto spill = [&book](qint32 row, const char *formula) {
        QVERIFY2(book.setCellInput(0, {row, 0}, QLatin1Char('=') + QString::fromLatin1(formula)), formula);
    };
    const auto row = [&book](qint32 r, qint32 cols) {
        QStringList out;
        for (qint32 c = 0; c < cols; ++c) {
            out.append(toText(book.cellValue(0, r, c)));
        }
        return out.join(QLatin1Char(','));
    };
    spill(0, "FREQUENCY({2;2;5;5;9},{2;5})"); // A1:A3
    QCOMPARE(row(0, 1) + row(1, 1) + row(2, 1), QStringLiteral("221"));
    spill(4, "TOROW({1,2;3,4})");
    QCOMPARE(row(4, 4), QStringLiteral("1,2,3,4"));
    spill(5, "TOROW({1,2;3,4},0,TRUE)");
    QCOMPARE(row(5, 4), QStringLiteral("1,3,2,4"));
    spill(6, "TAKE({1,2,3;4,5,6},1,-2)");
    QCOMPARE(row(6, 2), QStringLiteral("2,3"));
    spill(7, "DROP({1,2,3;4,5,6},1)");
    QCOMPARE(row(7, 3), QStringLiteral("4,5,6"));
    spill(8, "CHOOSECOLS({1,2,3;4,5,6},-1,1)");
    QCOMPARE(row(8, 2), QStringLiteral("3,1"));
    QCOMPARE(row(9, 2), QStringLiteral("6,4"));
    spill(10, "HSTACK({1;2},{3,4})");
    QCOMPARE(row(10, 3), QStringLiteral("1,3,4"));
    QCOMPARE(row(11, 3), QStringLiteral("2,#N/A,#N/A"));
    spill(12, "VSTACK({1,2},{3})");
    QCOMPARE(row(13, 2), QStringLiteral("3,#N/A"));
    spill(14, "EXPAND({1,2},2,3,0)");
    QCOMPARE(row(15, 3), QStringLiteral("0,0,0"));
    spill(16, "WRAPROWS({1,2,3,4,5},2)");
    QCOMPARE(row(18, 2), QStringLiteral("5,#N/A"));
    spill(19, "MINVERSE({4,7;2,6})");
    QVERIFY(qFuzzyCompare(std::get<double>(book.cellValue(0, 19, 0)), 0.6));
    QVERIFY(qFuzzyCompare(std::get<double>(book.cellValue(0, 20, 1)), 0.4));
    spill(21, "MUNIT(2)");
    QCOMPARE(row(22, 2), QStringLiteral("0,1"));
    spill(23, "ROW(A1:A3)");
    QCOMPARE(row(25, 1), QStringLiteral("3"));
    spill(26, "TAKE({1,2},0)");
    QCOMPARE(book.cellValue(0, 26, 0), Value(Err::Calc));
}

void CalcArraysTest::legacyArrayParameters()
{
    // Excel evaluates some parameters as arrays even without Ctrl+Shift+
    // Enter: SUMPRODUCT's, LOOKUP's vectors, INDEX's array, AGGREGATE's.
    Workbook book;
    book.addSheet(QStringLiteral("Sheet1"));
    const QStringList words{QStringLiteral("apple"), QStringLiteral("banana"), QString(), QStringLiteral("kiwi")};
    for (qint32 r = 0; r < words.size(); ++r) {
        if (!words.at(r).isEmpty()) {
            book.setCellValue(0, {r, 0}, words.at(r));
        }
    }
    legacy(book, 9, 2, QStringLiteral("SUMPRODUCT(LEN(A1:A4))"));
    QCOMPARE(value(book, 9, 2), Value(15.0));
    legacy(book, 10, 2, QStringLiteral("LOOKUP(2,1/(A1:A9<>\"\"),A1:A9)")); // the last non-blank
    QCOMPARE(value(book, 10, 2), Value(QStringLiteral("kiwi")));
    legacy(book, 11, 2, QStringLiteral("MAX(INDEX(LEN(A1:A4),0))"));
    QCOMPARE(value(book, 11, 2), Value(6.0));
    legacy(book, 12, 2, QStringLiteral("AGGREGATE(14,6,LEN(A1:A4)/(LEN(A1:A4)>4),2)"));
    QCOMPARE(value(book, 12, 2), Value(5.0));
    // …but a value parameter of an ordinary function still intersects.
    legacy(book, 1, 3, QStringLiteral("LEN(A1:A4)"));
    QCOMPARE(value(book, 1, 3), Value(6.0));
}

QTEST_GUILESS_MAIN(CalcArraysTest)
#include "tst_calc_arrays.moc"
