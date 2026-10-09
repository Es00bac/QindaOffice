// SPDX-License-Identifier: GPL-3.0-or-later
// Data ▸ Data Tools (M78, D-087): Text to Columns (text_to_columns.h),
// Remove Duplicates and Consolidate (data_tools.h), each against what
// Excel does with the same data. Flash Fill has its own test
// (tst_calc_flashfill).

#include "data_tools.h"
#include "functions/functions.h"
#include "sheet.h"
#include "text_to_columns.h"
#include "workbook.h"

#include <QDate>
#include <QtTest>

using namespace QindaOffice::Calc;

namespace {

using Format = TextToColumnsOptions::Format;

TextToColumnsOptions delimited(bool comma, bool space = false)
{
    TextToColumnsOptions options;
    options.tab = !comma && !space;
    options.comma = comma;
    options.space = space;
    return options;
}

QString serialOf(int year, int month, int day)
{
    return QString::number(serialFromDate(QDate(year, month, day)), 'g', 15);
}

// The input each converted cell takes, by "row,col".
QHash<QString, TextColumnCell> byCell(const QList<TextColumnCell> &cells)
{
    QHash<QString, TextColumnCell> out;
    for (const TextColumnCell &cell : cells) {
        out.insert(QStringLiteral("%1,%2").arg(cell.addr.row).arg(cell.addr.col), cell);
    }
    return out;
}

void put(Workbook &book, qint32 sheet, int row, int col, const QString &input)
{
    QVERIFY(book.setCellInput(sheet, {row, col}, input));
}

} // namespace

class CalcDataToolsTest : public QObject {
    Q_OBJECT
private slots:
    void delimitedSplitKeepsEmptyFields();
    void qualifierKeepsDelimitersAndDoubledQuotes();
    void consecutiveDelimitersCountAsOne();
    void fixedWidthBreaksAreGuessedAndApplied();
    void columnFormatsConvertAndSkipCloseUp();
    void generalReadsNumbersWithTheAdvancedSeparators();
    void destinationAndEmptyLines();
    void removeDuplicatesByChosenColumns();
    void removeDuplicatesRefusesWhatItCannotMove();
    void wholeColumnsMeanTheirData();
    void headerRowIsRecognised();
    void currentRegionIsBoundedByBlankLines();
    void consolidateByPositionAcrossSheets();
    void consolidateByLabels();
    void consolidateRefusesNoReferences();
};

void CalcDataToolsTest::delimitedSplitKeepsEmptyFields()
{
    const QList<QStringList> split =
        splitTextToColumns({QStringLiteral("a,b,c"), QStringLiteral("d,,f"), QStringLiteral("g,h,")}, delimited(true));
    QCOMPARE(split.size(), 3);
    QCOMPARE(split.at(0), (QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));
    QCOMPARE(split.at(1), (QStringList{QStringLiteral("d"), QString(), QStringLiteral("f")}));
    // A trailing delimiter is a last, empty field, as in Excel.
    QCOMPARE(split.at(2), (QStringList{QStringLiteral("g"), QStringLiteral("h"), QString()}));
    // Tab is the default delimiter.
    QCOMPARE(splitTextToColumns({QStringLiteral("x\ty")}, TextToColumnsOptions{}).first(),
             (QStringList{QStringLiteral("x"), QStringLiteral("y")}));
    // "Other" takes its first character.
    TextToColumnsOptions pipe = delimited(false);
    pipe.tab = false;
    pipe.other = QStringLiteral("|");
    QCOMPARE(splitTextToColumns({QStringLiteral("1|2")}, pipe).first(), (QStringList{QStringLiteral("1"), QStringLiteral("2")}));
}

void CalcDataToolsTest::qualifierKeepsDelimitersAndDoubledQuotes()
{
    const QString line = QStringLiteral("\"Smith, John\",42,\"say \"\"hi\"\"\"");
    QCOMPARE(splitTextToColumns({line}, delimited(true)).first(),
             (QStringList{QStringLiteral("Smith, John"), QStringLiteral("42"), QStringLiteral("say \"hi\"")}));
    // {none}: the quotes are text and the comma inside splits.
    TextToColumnsOptions none = delimited(true);
    none.qualifier = QChar();
    QCOMPARE(splitTextToColumns({QStringLiteral("\"a,b\"")}, none).first(),
             (QStringList{QStringLiteral("\"a"), QStringLiteral("b\"")}));
    // The single quote as qualifier.
    TextToColumnsOptions single = delimited(true);
    single.qualifier = QLatin1Char('\'');
    QCOMPARE(splitTextToColumns({QStringLiteral("'x,y',z")}, single).first(),
             (QStringList{QStringLiteral("x,y"), QStringLiteral("z")}));
}

void CalcDataToolsTest::consecutiveDelimitersCountAsOne()
{
    TextToColumnsOptions space = delimited(false, true);
    QCOMPARE(splitTextToColumns({QStringLiteral("John  Smith")}, space).first(),
             (QStringList{QStringLiteral("John"), QString(), QStringLiteral("Smith")}));
    space.consecutiveAsOne = true;
    QCOMPARE(splitTextToColumns({QStringLiteral("John  Smith")}, space).first(),
             (QStringList{QStringLiteral("John"), QStringLiteral("Smith")}));
    // Different delimiters in a run are one break too.
    TextToColumnsOptions both = delimited(true, true);
    both.consecutiveAsOne = true;
    QCOMPARE(splitTextToColumns({QStringLiteral("Lee, Ann")}, both).first(),
             (QStringList{QStringLiteral("Lee"), QStringLiteral("Ann")}));
}

void CalcDataToolsTest::fixedWidthBreaksAreGuessedAndApplied()
{
    const QStringList lines{QStringLiteral("John    Smith   42"), QStringLiteral("Alice   Brown   7")};
    QCOMPARE(guessFixedWidthBreaks(lines), (QList<int>{8, 16}));
    TextToColumnsOptions fixed;
    fixed.mode = TextToColumnsOptions::Mode::FixedWidth;
    fixed.breaks = {16, 8, 8}; // unsorted and repeated: cleaned
    const QList<QStringList> split = splitTextToColumns(lines, fixed);
    QCOMPARE(split.at(0), (QStringList{QStringLiteral("John"), QStringLiteral("Smith"), QStringLiteral("42")}));
    QCOMPARE(split.at(1), (QStringList{QStringLiteral("Alice"), QStringLiteral("Brown"), QStringLiteral("7")}));
    // Right-aligned numbers: the break goes where they start.
    QCOMPARE(guessFixedWidthBreaks({QStringLiteral("Item       5"), QStringLiteral("Thing    123")}), (QList<int>{9}));
}

void CalcDataToolsTest::columnFormatsConvertAndSkipCloseUp()
{
    TextToColumnsOptions options = delimited(true);
    options.formats = {Format::Text, Format::DateMDY, Format::Skip, Format::DateDMY, Format::DateMDY};
    const QHash<QString, TextColumnCell> cells = byCell(textToColumnsCells(
        {QStringLiteral("001,1/2/26,drop,13/01/2026,13/01/2026")}, 0, {0, 0}, options));
    QCOMPARE(cells.size(), 4); // the skipped column is gone and the rest close up
    QCOMPARE(cells.value(QStringLiteral("0,0")).input, QStringLiteral("'001"));
    QCOMPARE(cells.value(QStringLiteral("0,1")).input, serialOf(2026, 1, 2));
    QVERIFY(cells.value(QStringLiteral("0,1")).dateFormat);
    QCOMPARE(cells.value(QStringLiteral("0,1")).numberFormat, QStringLiteral("m/d/yyyy"));
    QCOMPARE(cells.value(QStringLiteral("0,2")).input, serialOf(2026, 1, 13));
    // Not a date in MDY order (month 13): it stays text.
    QCOMPARE(cells.value(QStringLiteral("0,3")).input, QStringLiteral("'13/01/2026"));
    QVERIFY(!cells.value(QStringLiteral("0,3")).dateFormat);
    // Two-digit years, month names and a time.
    TextToColumnsOptions dates = delimited(true);
    dates.formats = {Format::DateMDY, Format::DateMDY, Format::DateYMD, Format::DateDMY};
    const QHash<QString, TextColumnCell> more = byCell(textToColumnsCells(
        {QStringLiteral("1/2/29,1/2/30,2026-Mar-04,5 Jan 2026 1:30 PM")}, 0, {0, 0}, dates));
    QCOMPARE(more.value(QStringLiteral("0,0")).input, serialOf(2029, 1, 2));
    QCOMPARE(more.value(QStringLiteral("0,1")).input, serialOf(1930, 1, 2));
    QCOMPARE(more.value(QStringLiteral("0,2")).input, serialOf(2026, 3, 4));
    const double withTime = serialFromDate(QDate(2026, 1, 5)) + 13.5 / 24.0;
    QCOMPARE(more.value(QStringLiteral("0,3")).input.toDouble(), withTime);
    QCOMPARE(more.value(QStringLiteral("0,3")).numberFormat, QStringLiteral("m/d/yyyy h:mm"));
    // The width the overwrite question measures leaves skipped columns out.
    TextToColumnsOptions skip = delimited(true);
    skip.formats = {Format::General, Format::Skip};
    QCOMPARE(textToColumnsWidth({QStringLiteral("a,b,c"), QStringLiteral("d")}, skip), 2);
}

void CalcDataToolsTest::generalReadsNumbersWithTheAdvancedSeparators()
{
    const QHash<QString, TextColumnCell> cells = byCell(textToColumnsCells(
        {QStringLiteral("1,234.50\t(12)\t7-\t12%\t$3\tabc\t'quoted\t=1+2\tTRUE\t2026-03-04\t3/4/2026")}, 0, {0, 0},
        TextToColumnsOptions{}));
    QCOMPARE(cells.value(QStringLiteral("0,0")).input, QStringLiteral("1234.50"));
    QCOMPARE(cells.value(QStringLiteral("0,1")).input, QStringLiteral("-12"));
    QCOMPARE(cells.value(QStringLiteral("0,2")).input, QStringLiteral("-7"));
    QCOMPARE(cells.value(QStringLiteral("0,3")).input, QStringLiteral("12%"));
    QCOMPARE(cells.value(QStringLiteral("0,3")).numberFormat, QStringLiteral("0%"));
    QCOMPARE(cells.value(QStringLiteral("0,4")).input, QStringLiteral("3"));
    QCOMPARE(cells.value(QStringLiteral("0,5")).input, QStringLiteral("abc"));
    // The field's own apostrophe is kept as a character.
    QCOMPARE(cells.value(QStringLiteral("0,6")).input, QStringLiteral("''quoted"));
    // General makes "=…" a formula and TRUE a boolean, as Excel does.
    QCOMPARE(cells.value(QStringLiteral("0,7")).input, QStringLiteral("=1+2"));
    QCOMPARE(cells.value(QStringLiteral("0,8")).input, QStringLiteral("TRUE"));
    QCOMPARE(cells.value(QStringLiteral("0,9")).input, serialOf(2026, 3, 4));
    QVERIFY(cells.value(QStringLiteral("0,9")).dateFormat);
    QCOMPARE(cells.value(QStringLiteral("0,10")).input, serialOf(2026, 3, 4)); // month/day/year
    // Decimal comma and thousands point (Step 3 ▸ Advanced).
    TextToColumnsOptions european;
    european.decimalSeparator = QLatin1Char(',');
    european.thousandsSeparator = QLatin1Char('.');
    QCOMPARE(textToColumnsCells({QStringLiteral("1.234,5")}, 0, {0, 0}, european).first().input, QStringLiteral("1234.5"));
    // Trailing minus off: "7-" is text.
    TextToColumnsOptions strict;
    strict.trailingMinus = false;
    QCOMPARE(textToColumnsCells({QStringLiteral("7-")}, 0, {0, 0}, strict).first().input, QStringLiteral("7-"));
}

void CalcDataToolsTest::destinationAndEmptyLines()
{
    const QList<TextColumnCell> cells =
        textToColumnsCells({QStringLiteral("a,,c"), QString(), QStringLiteral("d")}, 4, {10, 3}, delimited(true));
    const QHash<QString, TextColumnCell> at = byCell(cells);
    QCOMPARE(cells.size(), 4); // three fields, nothing for the empty line, one field
    QCOMPARE(at.value(QStringLiteral("10,3")).input, QStringLiteral("a"));
    QVERIFY(at.contains(QStringLiteral("10,4")));
    QCOMPARE(at.value(QStringLiteral("10,4")).input, QString()); // an empty field clears its cell
    QCOMPARE(at.value(QStringLiteral("10,5")).input, QStringLiteral("c"));
    QCOMPARE(at.value(QStringLiteral("12,3")).input, QStringLiteral("d"));
    QVERIFY(!at.contains(QStringLiteral("12,4"))); // a short line leaves the rest alone
    // Without a destination row the lines stay on their own rows.
    QCOMPARE(textToColumnsCells({QStringLiteral("x")}, 4, {-1, 2}, delimited(true)).first().addr, (CellAddr{4, 2}));
}

void CalcDataToolsTest::removeDuplicatesByChosenColumns()
{
    Workbook book;
    book.addSheet(QStringLiteral("Data"));
    const QStringList rows{QStringLiteral("Name,City,Amount"), QStringLiteral("Ann,Paris,10"),
                           QStringLiteral("bob,Rome,20"),      QStringLiteral("ANN,paris,30"),
                           QStringLiteral("Bob,Rome,20"),      QStringLiteral("Cid,Oslo,5")};
    for (int r = 0; r < rows.size(); ++r) {
        const QStringList cells = rows.at(r).split(QLatin1Char(','));
        for (int c = 0; c < cells.size(); ++c) {
            put(book, 0, r, c, cells.at(c));
        }
        if (r > 0) {
            put(book, 0, r, 3, QStringLiteral("=C%1*2").arg(r + 1)); // a formula on its own row
        }
    }
    put(book, 0, 0, 5, QStringLiteral("outside"));
    put(book, 0, 7, 0, QStringLiteral("below"));
    CellStyle bold;
    bold.bold = true;
    const qint32 boldId = book.addStyle(bold);
    book.sheetAt(0).setStyleIdAt({5, 0}, boldId);
    book.sheetAt(0).setCommentAt({5, 0}, CellComment{QStringLiteral("me"), QStringLiteral("last")});
    QVERIFY(looksLikeHeaderRow(book, 0, {{0, 0}, {5, 3}}));

    const RemoveDuplicatesResult result = removeDuplicates(book, 0, {{0, 0}, {5, 3}}, {0, 1}, true);
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QCOMPARE(result.removed, 2);   // ANN/paris repeats Ann/Paris, Bob/Rome repeats bob/Rome
    QCOMPARE(result.remaining, 3);
    const Sheet &sheet = book.sheetAt(0);
    QCOMPARE(book.cellValue(0, 0, 0), Value(QStringLiteral("Name"))); // the header stays
    QCOMPARE(book.cellValue(0, 1, 0), Value(QStringLiteral("Ann")));  // the first occurrence stays
    QCOMPARE(book.cellValue(0, 2, 0), Value(QStringLiteral("bob")));
    QCOMPARE(book.cellValue(0, 3, 0), Value(QStringLiteral("Cid")));
    QCOMPARE(book.cellValue(0, 3, 2), Value(5.0));
    // Cid's formula moved with its row and still reads its own row's cells.
    QCOMPARE(sheet.cellAt({3, 3})->formula, QStringLiteral("C4*2"));
    QCOMPARE(book.cellValue(0, 3, 3), Value(10.0));
    QCOMPARE(book.cellValue(0, 2, 3), Value(40.0));
    // Its format and note came along; the freed rows are empty.
    QCOMPARE(sheet.styleIdAt({3, 0}), boldId);
    QVERIFY(sheet.commentAt({3, 0}) != nullptr);
    QCOMPARE(sheet.styleIdAt({5, 0}), 0);
    QVERIFY(sheet.commentAt({5, 0}) == nullptr);
    for (int r = 4; r <= 5; ++r) {
        for (int c = 0; c <= 3; ++c) {
            QVERIFY(sheet.cellAt({r, c}) == nullptr);
        }
    }
    // Nothing outside the range moved.
    QCOMPARE(book.cellValue(0, 0, 5), Value(QStringLiteral("outside")));
    QCOMPARE(book.cellValue(0, 7, 0), Value(QStringLiteral("below")));

    // By every column: 20 and 20 match, "bob" and "Bob" match.
    Workbook again;
    again.addSheet(QStringLiteral("S"));
    for (int r = 0; r < 3; ++r) {
        put(again, 0, r, 0, r == 1 ? QStringLiteral("X") : QStringLiteral("x"));
        put(again, 0, r, 1, r == 2 ? QStringLiteral("2") : QStringLiteral("1"));
    }
    const RemoveDuplicatesResult all = removeDuplicates(again, 0, {{0, 0}, {2, 1}}, {0, 1}, false);
    QCOMPARE(all.removed, 1);
    QCOMPARE(again.cellValue(0, 1, 1), Value(2.0));
    QVERIFY(again.sheetAt(0).cellAt({2, 0}) == nullptr);
}

void CalcDataToolsTest::removeDuplicatesRefusesWhatItCannotMove()
{
    Workbook book;
    book.addSheet(QStringLiteral("S"));
    for (int r = 0; r < 4; ++r) {
        put(book, 0, r, 0, QStringLiteral("same"));
    }
    QVERIFY(!removeDuplicates(book, 0, {{0, 0}, {3, 0}}, {}, false).error.isEmpty()); // no column chosen
    book.sheetAt(0).addMerge({{1, 1}, {2, 1}});
    const RemoveDuplicatesResult merged = removeDuplicates(book, 0, {{0, 0}, {3, 1}}, {0}, false);
    QVERIFY(!merged.error.isEmpty());
    QCOMPARE(merged.removed, 0);
    QCOMPARE(book.cellValue(0, 3, 0), Value(QStringLiteral("same"))); // untouched
}

void CalcDataToolsTest::wholeColumnsMeanTheirData()
{
    Workbook book;
    book.addSheet(QStringLiteral("S"));
    put(book, 0, 0, 0, QStringLiteral("Code"));
    put(book, 0, 1, 0, QStringLiteral("7"));
    put(book, 0, 2, 0, QStringLiteral("7"));
    put(book, 0, 3, 0, QStringLiteral("8"));
    const CellRange columnA{{0, 0}, {Sheet::kMaxRows - 1, 0}};
    QVERIFY(looksLikeHeaderRow(book, 0, columnA));
    const RemoveDuplicatesResult result = removeDuplicates(book, 0, columnA, {0}, true);
    QCOMPARE(result.removed, 1);
    QCOMPARE(result.remaining, 2);
    QCOMPARE(book.cellValue(0, 2, 0), Value(8.0));
    QVERIFY(book.sheetAt(0).cellAt({3, 0}) == nullptr);
    ConsolidateOptions options;
    options.sources = {{0, columnA}};
    options.topRow = true;
    const ConsolidateResult sum = consolidate(book, options);
    QCOMPARE(sum.grid.size(), 3); // the label row and the two values, not a million rows
    QCOMPARE(sum.grid.at(0).at(0), Value(QStringLiteral("Code")));
}

void CalcDataToolsTest::headerRowIsRecognised()
{
    Workbook book;
    book.addSheet(QStringLiteral("S"));
    put(book, 0, 0, 0, QStringLiteral("1"));
    put(book, 0, 1, 0, QStringLiteral("2"));
    QVERIFY(!looksLikeHeaderRow(book, 0, {{0, 0}, {1, 0}}));
    put(book, 0, 0, 0, QStringLiteral("Qty"));
    QVERIFY(looksLikeHeaderRow(book, 0, {{0, 0}, {1, 0}}));
    QVERIFY(!looksLikeHeaderRow(book, 0, {{0, 0}, {0, 0}})); // one row has no header
}

void CalcDataToolsTest::currentRegionIsBoundedByBlankLines()
{
    Workbook book;
    book.addSheet(QStringLiteral("S"));
    // A list in A2:B4 (B3 and B4 blank), E5 and F6 touching only diagonally.
    put(book, 0, 1, 0, QStringLiteral("Name"));
    put(book, 0, 2, 0, QStringLiteral("Ann"));
    put(book, 0, 3, 0, QStringLiteral("Bo"));
    put(book, 0, 1, 1, QStringLiteral("1"));
    put(book, 0, 4, 4, QStringLiteral("x"));
    put(book, 0, 5, 5, QStringLiteral("y"));
    const Sheet &sheet = book.sheetAt(0);
    // From the blank cell beside the list (where Flash Fill types): the list.
    QCOMPARE(currentRegion(sheet, {2, 1}), (CellRange{{1, 0}, {3, 1}}));
    QCOMPARE(currentRegion(sheet, {0, 0}), (CellRange{{0, 0}, {3, 1}})); // the row above touches it
    QCOMPARE(currentRegion(sheet, {4, 4}), (CellRange{{4, 4}, {5, 5}})); // diagonals count
    QCOMPARE(currentRegion(sheet, {9, 9}), (CellRange{{9, 9}, {9, 9}})); // nothing near: the cell
    QCOMPARE(currentRegion(sheet, {2, 3}), (CellRange{{2, 3}, {2, 3}})); // blank column C keeps D3 out of the list
}

void CalcDataToolsTest::consolidateByPositionAcrossSheets()
{
    Workbook book;
    book.addSheet(QStringLiteral("East"));
    book.addSheet(QStringLiteral("West"));
    put(book, 0, 0, 0, QStringLiteral("1"));
    put(book, 0, 1, 0, QStringLiteral("2"));
    put(book, 1, 0, 0, QStringLiteral("10"));
    put(book, 1, 1, 0, QStringLiteral("20"));
    put(book, 1, 2, 0, QStringLiteral("30"));
    ConsolidateOptions options;
    options.sources = {{0, {{0, 0}, {1, 0}}}, {1, {{0, 0}, {2, 0}}}};
    ConsolidateResult sum = consolidate(book, options);
    QVERIFY2(sum.error.isEmpty(), qPrintable(sum.error));
    // As large as the largest source.
    QCOMPARE(sum.grid.size(), 3);
    QCOMPARE(sum.grid.at(0).at(0), Value(11.0));
    QCOMPARE(sum.grid.at(1).at(0), Value(22.0));
    QCOMPARE(sum.grid.at(2).at(0), Value(30.0));
    options.function = ConsolidateFunction::Average;
    const ConsolidateResult average = consolidate(book, options);
    QCOMPARE(average.grid.at(0).at(0), Value(5.5));
    QCOMPARE(average.grid.at(2).at(0), Value(30.0));
    options.function = ConsolidateFunction::StdDev;
    QCOMPARE(consolidate(book, options).grid.at(2).at(0), Value(Err::Div0)); // one number
}

void CalcDataToolsTest::consolidateByLabels()
{
    Workbook book;
    book.addSheet(QStringLiteral("East"));
    book.addSheet(QStringLiteral("West"));
    const QList<QStringList> east{{QStringLiteral("Item"), QStringLiteral("Qty")},
                                  {QStringLiteral("Apple"), QStringLiteral("3")},
                                  {QStringLiteral("Pear"), QStringLiteral("5")}};
    const QList<QStringList> west{{QStringLiteral("Item"), QStringLiteral("qty")},
                                  {QStringLiteral("pear"), QStringLiteral("2")},
                                  {QStringLiteral("Plum"), QStringLiteral("7")}};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 2; ++c) {
            put(book, 0, r, c, east.at(r).at(c));
            put(book, 1, r + 4, c + 2, west.at(r).at(c)); // elsewhere on its sheet
        }
    }
    ConsolidateOptions options;
    options.sources = {{0, {{0, 0}, {2, 1}}}, {1, {{4, 2}, {6, 3}}}};
    options.topRow = true;
    options.leftColumn = true;
    const ConsolidateResult sum = consolidate(book, options);
    QVERIFY2(sum.error.isEmpty(), qPrintable(sum.error));
    QCOMPARE(sum.grid.size(), 4);
    QCOMPARE(sum.grid.at(0).size(), 2);
    QVERIFY(isBlank(sum.grid.at(0).at(0)));                       // the corner stays empty
    QCOMPARE(sum.grid.at(0).at(1), Value(QStringLiteral("Qty"))); // first-seen spelling
    QCOMPARE(sum.grid.at(1).at(0), Value(QStringLiteral("Apple")));
    QCOMPARE(sum.grid.at(1).at(1), Value(3.0));
    QCOMPARE(sum.grid.at(2).at(0), Value(QStringLiteral("Pear")));  // "pear" matched it
    QCOMPARE(sum.grid.at(2).at(1), Value(7.0));
    QCOMPARE(sum.grid.at(3).at(0), Value(QStringLiteral("Plum")));
    QCOMPARE(sum.grid.at(3).at(1), Value(7.0));
    options.function = ConsolidateFunction::Average;
    const ConsolidateResult average = consolidate(book, options);
    QCOMPARE(average.grid.at(2).at(1), Value(3.5));
    QCOMPARE(average.grid.at(1).at(1), Value(3.0));
    // Labels in the left column only: the columns go by position.
    options.function = ConsolidateFunction::Sum;
    options.topRow = false;
    options.sources = {{0, {{1, 0}, {2, 1}}}, {1, {{5, 2}, {6, 3}}}};
    const ConsolidateResult rowsOnly = consolidate(book, options);
    QCOMPARE(rowsOnly.grid.size(), 3);
    QCOMPARE(rowsOnly.grid.at(1).at(0), Value(QStringLiteral("Pear")));
    QCOMPARE(rowsOnly.grid.at(1).at(1), Value(7.0));
    // Count counts what is there, Count Numbers only numbers.
    options.function = ConsolidateFunction::Count;
    QCOMPARE(consolidate(book, options).grid.at(1).at(1), Value(2.0));
}

void CalcDataToolsTest::consolidateRefusesNoReferences()
{
    Workbook book;
    book.addSheet(QStringLiteral("S"));
    QVERIFY(!consolidate(book, ConsolidateOptions{}).error.isEmpty());
    ConsolidateOptions bad;
    bad.sources = {{5, {{0, 0}, {1, 1}}}};
    QVERIFY(!consolidate(book, bad).error.isEmpty());
}

QTEST_MAIN(CalcDataToolsTest)
#include "tst_calc_datatools.moc"
