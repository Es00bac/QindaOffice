// SPDX-License-Identifier: GPL-3.0-or-later
// M69 (D-078): Excel tables and structured references. Every specifier
// Excel has resolves to the cells Microsoft's documentation says
// ("Using structured references with Excel tables"); references follow
// the table when it grows, when a header is retyped, when rows and columns
// are inserted or deleted, and die (#REF!) with the columns or the table
// they name; calculated columns fill and grow; the totals row toggles.

#include "structure.h"
#include "structured_ref.h"
#include "table_edit.h"
#include "workbook.h"

#include <QtTest>

using namespace QindaOffice::Calc;

class CalcTablesTest : public QObject {
    Q_OBJECT
private slots:
    void parsesAndPrintsBothSpellings();
    void everySpecifierResolves();
    void referencesFollowTheirCells();
    void calculatedColumnsFillAndGrow();
    void typingBelowTheTableGrowsIt();
    void retypedHeadersRenameReferences();
    void insertedAndDeletedColumns();
    void insertedAndDeletedRows();
    void deletedTablesBreakTheirReferences();
    void totalsRowToggles();
    void createTableNamesItsHeaders();
};

namespace {

CellAddr at(qint32 row, qint32 col)
{
    return {row, col};
}

Value value(const Workbook &book, qint32 row, qint32 col, qint32 sheet = 0)
{
    return book.cellValue(sheet, row, col);
}

QString formulaAt(const Workbook &book, qint32 row, qint32 col, qint32 sheet = 0)
{
    const Cell *cell = book.sheetAt(sheet).cellAt({row, col});
    return cell != nullptr ? cell->formula : QString();
}

// Sales on Sheet1!A1:C5: Region | Amount | Cost, three data rows, and a
// totals row summing Amount (Excel's SUBTOTAL 109).
Workbook salesBook(bool totals = true)
{
    Workbook book;
    book.addSheet(QStringLiteral("Sheet1"));
    book.addSheet(QStringLiteral("Report"));
    const QStringList headers{QStringLiteral("Region"), QStringLiteral("Amount"), QStringLiteral("Cost")};
    for (qint32 c = 0; c < 3; ++c) {
        book.setCellValue(0, at(0, c), headers.at(c));
    }
    const QStringList regions{QStringLiteral("North"), QStringLiteral("South"), QStringLiteral("East")};
    for (qint32 r = 0; r < 3; ++r) {
        book.setCellValue(0, at(r + 1, 0), regions.at(r));
        book.setCellValue(0, at(r + 1, 1), double(10 * (r + 1)));
        book.setCellValue(0, at(r + 1, 2), double(r + 1));
    }
    TableSpec table;
    table.name = QStringLiteral("Sales");
    table.range = {at(0, 0), at(totals ? 4 : 3, 2)};
    table.totalsRow = totals;
    table.columnNames = headers;
    book.sheetAt(0).setTables({table});
    if (totals) {
        book.setCellValue(0, at(4, 0), QStringLiteral("Total"));
        book.setCellInput(0, at(4, 1), QStringLiteral("=SUBTOTAL(109,[Amount])"));
    }
    return book;
}

} // namespace

void CalcTablesTest::parsesAndPrintsBothSpellings()
{
    // Excel's file spelling reads; the formula bar spelling prints.
    const auto display = [](const QString &text) {
        const std::optional<StructRef> ref = parseStructRef(text);
        return ref ? structRefText(*ref, StructRefSpelling::Display) : QStringLiteral("<invalid>");
    };
    QCOMPARE(display(QStringLiteral("Sales[[#This Row],[Amount]]")), QStringLiteral("Sales[@Amount]"));
    QCOMPARE(display(QStringLiteral("[@Amount]")), QStringLiteral("[@Amount]"));
    QCOMPARE(display(QStringLiteral("Sales[[#This Row],[Unit Price]]")), QStringLiteral("Sales[@[Unit Price]]"));
    QCOMPARE(display(QStringLiteral("Sales[[Jan]:[Mar]]")), QStringLiteral("Sales[[Jan]:[Mar]]"));
    QCOMPARE(display(QStringLiteral("Sales[#All]")), QStringLiteral("Sales[#All]"));
    QCOMPARE(display(QStringLiteral("Sales[[#Headers],[#Data],[Amount]]")), QStringLiteral("Sales[[#Headers],[#Data],[Amount]]"));
    QCOMPARE(display(QStringLiteral("Sales[[#Totals],[Amount]]")), QStringLiteral("Sales[[#Totals],[Amount]]"));
    QCOMPARE(display(QStringLiteral("Sales['#Items]")), QStringLiteral("Sales['#Items]"));
    QCOMPARE(display(QStringLiteral("Sales[[Total $]]")), QStringLiteral("Sales[[Total $]]"));
    QCOMPARE(display(QStringLiteral("Sales[]")), QStringLiteral("Sales[]"));
    QCOMPARE(display(QStringLiteral("Sales[#This Row]")), QStringLiteral("Sales[@]"));
    QCOMPARE(display(QStringLiteral("Sales[[#Headers],[#Totals]]")), QStringLiteral("<invalid>"));
    QCOMPARE(display(QStringLiteral("Sales[#Nonsense]")), QStringLiteral("<invalid>"));

    // The file always names the table and spells this row out.
    const auto file = [](const QString &text, const QString &table) {
        return structRefText(*parseStructRef(text), StructRefSpelling::File, table);
    };
    QCOMPARE(file(QStringLiteral("[@Amount]"), QStringLiteral("Sales")), QStringLiteral("Sales[[#This Row],[Amount]]"));
    QCOMPARE(file(QStringLiteral("[@[Jan]:[Mar]]"), QStringLiteral("Sales")),
             QStringLiteral("Sales[[#This Row],[Jan]:[Mar]]"));
    QCOMPARE(file(QStringLiteral("[Amount]"), QStringLiteral("Sales")), QStringLiteral("Sales[Amount]"));
    QCOMPARE(file(QStringLiteral("Other[@]"), QStringLiteral("Sales")), QStringLiteral("Other[#This Row]"));
    QCOMPARE(file(QStringLiteral("[Amount]"), QString()), QStringLiteral("#REF!"));

    // In a formula: one token, printed back the way it was meant.
    const ParseResult parsed = parseFormula(QStringLiteral("SUM(Sales[Amount])+[@Cost]*2"));
    QVERIFY2(parsed.error.isEmpty(), qPrintable(parsed.error));
    QCOMPARE(nodeText(parsed.root), QStringLiteral("SUM(Sales[Amount])+[@Cost]*2"));
}

void CalcTablesTest::everySpecifierResolves()
{
    Workbook book = salesBook();
    const auto in = [&book](qint32 row, qint32 col, const QString &formula, qint32 sheet = 1) {
        book.setCellInput(sheet, at(row, col), formula);
        return book.cellValue(sheet, row, col);
    };
    QCOMPARE(in(0, 0, QStringLiteral("=SUM(Sales[Amount])")), Value(60.0));
    QCOMPARE(in(1, 0, QStringLiteral("=ROWS(Sales[#All])")), Value(5.0));
    QCOMPARE(in(2, 0, QStringLiteral("=ROWS(Sales[#Data])")), Value(3.0));
    QCOMPARE(in(3, 0, QStringLiteral("=Sales[[#Headers],[Cost]]")), Value(QStringLiteral("Cost")));
    QCOMPARE(in(4, 0, QStringLiteral("=Sales[[#Totals],[Amount]]")), Value(60.0));
    QCOMPARE(in(5, 0, QStringLiteral("=ROWS(Sales[[#Headers],[#Data],[Amount]])")), Value(4.0));
    QCOMPARE(in(6, 0, QStringLiteral("=ROWS(Sales[[#Data],[#Totals],[Amount]])")), Value(4.0));
    QCOMPARE(in(7, 0, QStringLiteral("=SUM(Sales[[Amount]:[Cost]])")), Value(66.0));
    QCOMPARE(in(8, 0, QStringLiteral("=COLUMNS(Sales[[Region]:[Cost]])")), Value(3.0));
    QCOMPARE(in(9, 0, QStringLiteral("=SUM(Sales)")), Value(66.0)); // the name alone: the data
    QCOMPARE(in(10, 0, QStringLiteral("=ROWS(Sales[])")), Value(3.0));
    // Names match without regard to case; three cells spill.
    QCOMPARE(in(11, 0, QStringLiteral("=sales[AMOUNT]")), Value(10.0));
    QCOMPARE(value(book, 13, 0, 1), Value(30.0));
    // This row: the formula's own row, on any sheet, within the data rows.
    QCOMPARE(in(2, 3, QStringLiteral("=Sales[@Amount]*2")), Value(40.0));
    QCOMPARE(in(0, 3, QStringLiteral("=Sales[@Amount]")), Value(Err::Value)); // the header row
    QCOMPARE(in(4, 3, QStringLiteral("=Sales[@Amount]")), Value(Err::Value)); // the totals row
    // Missing parts are #REF!.
    QCOMPARE(in(0, 5, QStringLiteral("=SUM(Sales[Profit])")), Value(Err::Ref));
    QCOMPARE(in(1, 5, QStringLiteral("=SUM(Nope[Amount])")), Value(Err::Ref));
    QCOMPARE(in(2, 5, QStringLiteral("=SUM([Amount])")), Value(Err::Ref)); // outside any table
    Workbook bare = salesBook(false);
    bare.setCellInput(1, at(0, 0), QStringLiteral("=SUM(Sales[[#Totals],[Amount]])"));
    QCOMPARE(bare.cellValue(1, 0, 0), Value(Err::Ref));
    // Inside the table an unqualified reference means its own table.
    QVERIFY(book.setCellInput(0, at(4, 2), QStringLiteral("=SUBTOTAL(109,[Cost])")));
    QCOMPARE(value(book, 4, 2), Value(6.0));
    QCOMPARE(value(book, 4, 1), Value(60.0));
}

void CalcTablesTest::referencesFollowTheirCells()
{
    Workbook book = salesBook();
    book.setCellInput(1, at(0, 0), QStringLiteral("=SUM(Sales[Amount])"));
    book.setCellInput(1, at(1, 0), QStringLiteral("=Sales[@Cost]"));
    book.setCellValue(0, at(2, 1), 100.0);
    book.setCellValue(0, at(1, 2), 7.0);
    QCOMPARE(value(book, 0, 0, 1), Value(140.0));
    QCOMPARE(value(book, 4, 1), Value(140.0)); // the totals row
    QCOMPARE(value(book, 1, 0, 1), Value(7.0));
}

void CalcTablesTest::calculatedColumnsFillAndGrow()
{
    Workbook book = salesBook(false);
    // Grow the table over an empty fourth column and type one formula.
    QList<TableSpec> tables = book.sheetAt(0).tables();
    tables[0].range.bottomRight.col = 3;
    tables[0].columnNames.append(QStringLiteral("Margin"));
    book.sheetAt(0).setTables(tables);
    book.setCellValue(0, at(0, 3), QStringLiteral("Margin"));
    QVERIFY(setTableAwareInput(book, 0, at(2, 3), QStringLiteral("=[@Amount]-[@Cost]")));
    for (qint32 r = 1; r <= 3; ++r) {
        QCOMPARE(formulaAt(book, r, 3), QStringLiteral("[@Amount]-[@Cost]"));
        QCOMPARE(value(book, r, 3), Value(double(10 * r - r)));
    }
    QCOMPARE(book.sheetAt(0).tables().first().calculatedColumns.value(3), QStringLiteral("[@Amount]-[@Cost]"));
    // A new formula in a calculated column replaces the whole column.
    QVERIFY(setTableAwareInput(book, 0, at(1, 3), QStringLiteral("=[@Amount]*2")));
    QCOMPARE(value(book, 3, 3), Value(60.0));
    // A value is an exception: the rest of the column stays calculated.
    QVERIFY(setTableAwareInput(book, 0, at(2, 3), QStringLiteral("5")));
    QCOMPARE(value(book, 2, 3), Value(5.0));
    QCOMPARE(formulaAt(book, 3, 3), QStringLiteral("[@Amount]*2"));
    // A formula among values does not fill: Region is not empty.
    QVERIFY(setTableAwareInput(book, 0, at(1, 0), QStringLiteral("=\"N\"&\"orth\"")));
    QCOMPARE(formulaAt(book, 2, 0), QString());
}

void CalcTablesTest::typingBelowTheTableGrowsIt()
{
    Workbook book = salesBook(false);
    QList<TableSpec> tables = book.sheetAt(0).tables();
    tables[0].calculatedColumns = {QString(), QString(), QStringLiteral("[@Amount]/10")};
    book.sheetAt(0).setTables(tables);
    for (qint32 r = 1; r <= 3; ++r) {
        book.setCellInput(0, at(r, 2), QStringLiteral("=[@Amount]/10"));
    }
    book.setCellInput(1, at(0, 0), QStringLiteral("=SUM(Sales[Amount])"));
    QVERIFY(setTableAwareInput(book, 0, at(4, 1), QStringLiteral("40")));
    QCOMPARE(book.sheetAt(0).tables().first().range, (CellRange{at(0, 0), at(4, 2)}));
    QCOMPARE(value(book, 0, 0, 1), Value(100.0));            // the reference grew with it
    QCOMPARE(formulaAt(book, 4, 2), QStringLiteral("[@Amount]/10")); // the calculated column too
    QCOMPARE(value(book, 4, 2), Value(4.0));
    // The column just right of the table joins it, named Column1.
    QVERIFY(setTableAwareInput(book, 0, at(2, 3), QStringLiteral("x")));
    QCOMPARE(book.sheetAt(0).tables().first().range.bottomRight.col, 3);
    QCOMPARE(value(book, 0, 3), Value(QStringLiteral("Column1")));
    QCOMPARE(book.sheetAt(0).tables().first().columnNames.value(3), QStringLiteral("Column1"));
    // A totals row stops the growth below the table.
    Workbook totals = salesBook(true);
    QVERIFY(setTableAwareInput(totals, 0, at(5, 1), QStringLiteral("1")));
    QCOMPARE(totals.sheetAt(0).tables().first().range.bottomRight.row, 4);
}

void CalcTablesTest::retypedHeadersRenameReferences()
{
    Workbook book = salesBook();
    book.setCellInput(1, at(0, 0), QStringLiteral("=SUM(Sales[Amount])"));
    book.setCellInput(1, at(1, 0), QStringLiteral("=SUM(Sales[[Amount]:[Cost]])"));
    QVERIFY(setTableAwareInput(book, 0, at(0, 1), QStringLiteral("Revenue")));
    QCOMPARE(formulaAt(book, 0, 0, 1), QStringLiteral("SUM(Sales[Revenue])"));
    QCOMPARE(formulaAt(book, 1, 0, 1), QStringLiteral("SUM(Sales[[Revenue]:[Cost]])"));
    QCOMPARE(formulaAt(book, 4, 1), QStringLiteral("SUBTOTAL(109,[Revenue])"));
    QCOMPARE(value(book, 0, 0, 1), Value(60.0));
    QCOMPARE(book.sheetAt(0).tables().first().columnNames.value(1), QStringLiteral("Revenue"));
    // A duplicate takes a number; a cleared header becomes Column<N>.
    QVERIFY(setTableAwareInput(book, 0, at(0, 2), QStringLiteral("Region")));
    QCOMPARE(value(book, 0, 2), Value(QStringLiteral("Region2")));
    book.setCellValue(0, at(0, 2), Value());
    QVERIFY(syncTableHeaders(book, 0));
    QCOMPARE(value(book, 0, 2), Value(QStringLiteral("Column1")));
}

void CalcTablesTest::insertedAndDeletedColumns()
{
    Workbook book = salesBook();
    book.setCellInput(1, at(0, 0), QStringLiteral("=SUM(Sales[Cost])"));
    book.setCellInput(1, at(1, 0), QStringLiteral("=SUM(Sales[[Amount]:[Cost]])"));
    book.setCellInput(1, at(2, 0), QStringLiteral("=COLUMNS(Sales[[Region]:[Amount]])"));
    // A column inserted inside the table is a table column.
    applyStructureEdit(book, {StructureOp::InsertColumns, 0, 1, 1});
    const TableSpec grown = book.sheetAt(0).tables().first();
    QCOMPARE(grown.range, (CellRange{at(0, 0), at(4, 3)}));
    QCOMPARE(grown.columnNames, (QStringList{QStringLiteral("Region"), QStringLiteral("Column1"),
                                             QStringLiteral("Amount"), QStringLiteral("Cost")}));
    QCOMPARE(value(book, 0, 1), Value(QStringLiteral("Column1")));
    QCOMPARE(value(book, 2, 0, 1), Value(3.0)); // the range spans the new column
    // Deleting Cost: a reference to it dies, a range keeps what remains.
    applyStructureEdit(book, {StructureOp::RemoveColumns, 0, 3, 1});
    QCOMPARE(formulaAt(book, 0, 0, 1), QStringLiteral("SUM(#REF!)"));
    QCOMPARE(value(book, 0, 0, 1), Value(Err::Ref));
    QCOMPARE(formulaAt(book, 1, 0, 1), QStringLiteral("SUM(Sales[Amount])"));
    QCOMPARE(value(book, 1, 0, 1), Value(60.0));
}

void CalcTablesTest::insertedAndDeletedRows()
{
    Workbook book = salesBook();
    QList<TableSpec> tables = book.sheetAt(0).tables();
    tables[0].calculatedColumns = {QString(), QString(), QStringLiteral("[@Amount]/10")};
    book.sheetAt(0).setTables(tables);
    for (qint32 r = 1; r <= 3; ++r) {
        book.setCellInput(0, at(r, 2), QStringLiteral("=[@Amount]/10"));
    }
    book.setCellInput(1, at(0, 0), QStringLiteral("=ROWS(Sales[Amount])"));
    // Rows inserted inside the table: it grows, new rows are calculated.
    applyStructureEdit(book, {StructureOp::InsertRows, 0, 2, 2});
    QCOMPARE(book.sheetAt(0).tables().first().range, (CellRange{at(0, 0), at(6, 2)}));
    QCOMPARE(value(book, 0, 0, 1), Value(5.0));
    QCOMPARE(formulaAt(book, 2, 2), QStringLiteral("[@Amount]/10"));
    QCOMPARE(formulaAt(book, 3, 2), QStringLiteral("[@Amount]/10"));
    QCOMPARE(formulaAt(book, 6, 1), QStringLiteral("SUBTOTAL(109,[Amount])")); // the totals row moved down
    QCOMPARE(value(book, 6, 1), Value(60.0));
    // Deleting data rows shrinks it; deleting the header row leaves none.
    applyStructureEdit(book, {StructureOp::RemoveRows, 0, 1, 2});
    QCOMPARE(value(book, 0, 0, 1), Value(3.0));
    applyStructureEdit(book, {StructureOp::RemoveRows, 0, 0, 1});
    QVERIFY(!book.sheetAt(0).tables().first().headerRow);
    QCOMPARE(book.sheetAt(0).tables().first().range, (CellRange{at(0, 0), at(3, 2)}));
    QCOMPARE(value(book, 0, 0, 1), Value(3.0));
}

void CalcTablesTest::deletedTablesBreakTheirReferences()
{
    Workbook book = salesBook();
    book.setCellInput(1, at(0, 0), QStringLiteral("=SUM(Sales[Amount])"));
    applyStructureEdit(book, {StructureOp::RemoveRows, 0, 0, 5});
    QVERIFY(book.sheetAt(0).tables().isEmpty());
    QCOMPARE(formulaAt(book, 0, 0, 1), QStringLiteral("SUM(#REF!)"));
    Workbook other = salesBook();
    other.setCellInput(1, at(0, 0), QStringLiteral("=SUM(Sales[Amount])"));
    QVERIFY(removeSheet(other, 0));
    QCOMPARE(other.cellValue(0, 0, 0), Value(Err::Ref));
}

void CalcTablesTest::totalsRowToggles()
{
    Workbook book = salesBook(false);
    QString error;
    QVERIFY2(setTableTotalsRow(book, 0, 0, true, &error), qPrintable(error));
    const TableSpec on = book.sheetAt(0).tables().first();
    QVERIFY(on.totalsRow);
    QCOMPARE(on.range.bottomRight.row, 4);
    QCOMPARE(value(book, 4, 0), Value(QStringLiteral("Total")));
    QCOMPARE(formulaAt(book, 4, 2), QStringLiteral("SUBTOTAL(109,[Cost])"));
    QCOMPARE(value(book, 4, 2), Value(6.0));
    QVERIFY(setTableTotalsRow(book, 0, 0, false, &error));
    QVERIFY(!book.sheetAt(0).tables().first().totalsRow);
    QVERIFY(isBlank(value(book, 4, 2)));
    // A row in use below the table refuses the totals row.
    book.setCellValue(0, at(4, 1), 1.0);
    QVERIFY(!setTableTotalsRow(book, 0, 0, true, &error));
    QVERIFY(!error.isEmpty());
}

void CalcTablesTest::createTableNamesItsHeaders()
{
    Workbook book;
    book.addSheet(QStringLiteral("Sheet1"));
    book.setCellValue(0, at(0, 0), QStringLiteral("Item"));
    book.setCellValue(0, at(0, 2), 2024.0);
    book.setCellValue(0, at(1, 0), QStringLiteral("Pens"));
    QString error;
    QVERIFY2(createTable(book, 0, {at(0, 0), at(2, 2)}, &error), qPrintable(error));
    const TableSpec table = book.sheetAt(0).tables().first();
    QCOMPARE(table.name, QStringLiteral("Table1"));
    QCOMPARE(table.columnNames, (QStringList{QStringLiteral("Item"), QStringLiteral("Column1"), QStringLiteral("2024")}));
    QCOMPARE(value(book, 0, 1), Value(QStringLiteral("Column1")));
    QCOMPARE(value(book, 0, 2), Value(QStringLiteral("2024"))); // a header is text
    QCOMPARE(nextTableName(book), QStringLiteral("Table2"));
    QVERIFY(!createTable(book, 0, {at(1, 1), at(3, 3)}, &error)); // overlaps Table1
}

QTEST_GUILESS_MAIN(CalcTablesTest)
#include "tst_calc_tables.moc"
