// SPDX-License-Identifier: GPL-3.0-or-later
// M78 (D-087): how a sheet is cut into printed pages (page_layout.h) — the
// paper and margins, the default cut by rows and columns, the scale, Fit to
// (and manual breaks ignored under it), manual breaks, print titles
// repeated, hidden rows, headings, the page order and first page number,
// multi-area print areas, the used range printing falls back to, the
// Breaks commands — and the print settings through inserted and deleted
// rows and columns; Excel's header and footer codes (header_footer.h).
//
// Every expectation is worked by hand: a default column is 96 px = 72 pt
// wide and a default row 20 px = 15 pt tall; Letter portrait with Normal
// margins leaves 511.2 × 684 pt to print in — seven columns, 45 rows.

#include "header_footer.h"
#include "page_layout.h"
#include "structure.h"
#include "workbook.h"

#include <QtTest>

using namespace QindaOffice::Calc;

namespace {

// A sheet whose used range is A1 to (rows, cols): the corners suffice.
Workbook grid(int rows, int cols)
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Sheet1"));
    wb.setCellValue(0, {0, 0}, Value(1.0));
    wb.setCellValue(0, {rows - 1, cols - 1}, Value(2.0));
    return wb;
}

CellRange range(int r0, int c0, int r1, int c1)
{
    return {{r0, c0}, {r1, c1}};
}

QList<qint32> rowStarts(const PrintLayout &layout)
{
    return layout.rowStarts;
}

} // namespace

class CalcPageLayoutTest : public QObject {
    Q_OBJECT
private slots:
    void paperAndMarginsShapeTheContent();
    void pagesAreCutByRowsAndColumns();
    void scaleShrinksTheGrid();
    void fitToWorksOutTheScale();
    void manualBreaksStartPagesExceptUnderFitTo();
    void printTitlesRepeatOnLaterPages();
    void hiddenRowsTakeNoRoom();
    void headingsTakeTheirRoom();
    void pageOrderAndFirstNumber();
    void everyPrintAreaPrintsOnItsOwnPages();
    void usedRangeHoldsStylesMergesAndObjects();
    void breakCommandsFollowExcel();
    void printSettingsFollowStructuralEdits();
    void paperSizes();
    void headerFooterCodesParse();
    void headerFooterFieldsFillIn();
    void headerFooterPartsRoundTrip();
    void headerFooterPresetsAreExcels();
};

void CalcPageLayoutTest::paperAndMarginsShapeTheContent()
{
    Workbook wb = grid(1, 1);
    PrintLayout layout = layoutPrintPages(wb, 0);
    QCOMPARE(layout.paperPt.width(), 612.0);
    QCOMPARE(layout.paperPt.height(), 792.0);
    QCOMPARE(layout.contentLeftPt, 0.7 * 72);
    QCOMPARE(layout.contentTopPt, 0.75 * 72);
    QVERIFY(qFuzzyCompare(layout.contentWidthPt, 511.2));
    QVERIFY(qFuzzyCompare(layout.contentHeightPt, 684.0));
    QCOMPARE(layout.pages.size(), 1);
    QCOMPARE(layout.pages.first().body, range(0, 0, 0, 0));
    QCOMPARE(layout.pages.first().number, 1);
    // Landscape A4 with Wide margins (1 inch, header/footer 0.5).
    Sheet::PageSetup setup;
    setup.orientation = 1;
    setup.paperSize = 9;
    setup.marginLeft = setup.marginRight = setup.marginTop = setup.marginBottom = 1.0;
    wb.sheetAt(0).setPageSetup(setup);
    layout = layoutPrintPages(wb, 0);
    QVERIFY(qAbs(layout.paperPt.width() - 297 * 72 / 25.4) < 1e-9);
    QVERIFY(qAbs(layout.paperPt.height() - 210 * 72 / 25.4) < 1e-9);
    QVERIFY(qAbs(layout.contentWidthPt - (297 * 72 / 25.4 - 144)) < 1e-9);
    QCOMPARE(layout.contentLeftPt, 72.0);
}

void CalcPageLayoutTest::pagesAreCutByRowsAndColumns()
{
    const Workbook wb = grid(100, 10);
    const PrintLayout layout = layoutPrintPages(wb, 0);
    QCOMPARE(layout.area, range(0, 0, 99, 9));
    QCOMPARE(layout.scalePercent, 100);
    QCOMPARE(rowStarts(layout), (QList<qint32>{0, 45, 90}));
    QCOMPARE(layout.colStarts, (QList<qint32>{0, 7}));
    QCOMPARE(layout.rowStartManual, (QList<bool>{false, false, false}));
    QCOMPARE(layout.pages.size(), 6);
    // Down, then over.
    QCOMPARE(layout.pages.at(0).body, range(0, 0, 44, 6));
    QCOMPARE(layout.pages.at(1).body, range(45, 0, 89, 6));
    QCOMPARE(layout.pages.at(2).body, range(90, 0, 99, 6));
    QCOMPARE(layout.pages.at(3).body, range(0, 7, 44, 9));
    QCOMPARE(layout.pages.at(5).body, range(90, 7, 99, 9));
    for (int i = 0; i < 6; ++i) {
        QCOMPARE(layout.pages.at(i).number, i + 1);
        QVERIFY(!layout.pages.at(i).titleRows.has_value());
    }
    QCOMPARE(layout.pages.at(0).printedSizePt, QSizeF(7 * 72, 45 * 15));
    QCOMPARE(layout.pages.at(5).printedSizePt, QSizeF(3 * 72, 10 * 15));
}

void CalcPageLayoutTest::scaleShrinksTheGrid()
{
    Workbook wb = grid(100, 10);
    Sheet::PageSetup setup;
    setup.scale = 50;
    wb.sheetAt(0).setPageSetup(setup);
    const PrintLayout layout = layoutPrintPages(wb, 0);
    QCOMPARE(layout.scalePercent, 50);
    QCOMPARE(layout.scale, 0.5);
    // 1022.4 pt of columns and 1368 pt of rows at 50 %: 91 rows a page.
    QCOMPARE(layout.colStarts, (QList<qint32>{0}));
    QCOMPARE(layout.rowStarts, (QList<qint32>{0, 91}));
    QCOMPARE(layout.pages.first().printedSizePt, QSizeF(10 * 72 * 0.5, 91 * 15 * 0.5));
    setup.scale = 5; // below Excel's 10 % floor
    wb.sheetAt(0).setPageSetup(setup);
    QCOMPARE(layoutPrintPages(wb, 0).scalePercent, 10);
}

void CalcPageLayoutTest::fitToWorksOutTheScale()
{
    // Eleven columns are 792 pt: one page wide needs 511.2 / 792 = 64.5 %.
    Workbook wb = grid(100, 11);
    Sheet::PageSetup setup;
    setup.fitToPage = true;
    setup.fitToWidth = 1;
    setup.fitToHeight = 0; // automatic
    wb.sheetAt(0).setPageSetup(setup);
    PrintLayout layout = layoutPrintPages(wb, 0);
    QCOMPARE(layout.scalePercent, 64);
    QCOMPARE(layout.colStarts, (QList<qint32>{0}));
    QCOMPARE(layout.rowStarts, (QList<qint32>{0, 71})); // 684 / 0.64 = 1068.75 pt: 71 rows
    QCOMPARE(layout.pages.size(), 2);
    QCOMPARE(layout.pages.at(1).across, 0);
    // One page wide by one tall: 1500 pt of rows in 684 needs 45 %.
    setup.fitToHeight = 1;
    wb.sheetAt(0).setPageSetup(setup);
    layout = layoutPrintPages(wb, 0);
    QCOMPARE(layout.scalePercent, 45);
    QCOMPARE(layout.pages.size(), 1);
    // Nothing to fit beyond 100 %, and "automatic" on both axes is 100 %.
    setup.fitToWidth = 0;
    setup.fitToHeight = 0;
    wb.sheetAt(0).setPageSetup(setup);
    QCOMPARE(layoutPrintPages(wb, 0).scalePercent, 100);
    Workbook small = grid(3, 3);
    setup.fitToWidth = setup.fitToHeight = 1;
    small.sheetAt(0).setPageSetup(setup);
    QCOMPARE(layoutPrintPages(small, 0).scalePercent, 100);
}

void CalcPageLayoutTest::manualBreaksStartPagesExceptUnderFitTo()
{
    Workbook wb = grid(100, 10);
    Sheet::PageSetup setup;
    setup.rowBreaks = {10};
    setup.colBreaks = {3};
    wb.sheetAt(0).setPageSetup(setup);
    PrintLayout layout = layoutPrintPages(wb, 0);
    QCOMPARE(layout.rowStarts, (QList<qint32>{0, 10, 55}));
    QCOMPARE(layout.rowStartManual, (QList<bool>{false, true, false}));
    QCOMPARE(layout.colStarts, (QList<qint32>{0, 3}));
    QCOMPARE(layout.colStartManual, (QList<bool>{false, true}));
    QCOMPARE(layout.pages.size(), 6);
    QCOMPARE(layout.pages.at(0).body, range(0, 0, 9, 2));
    // Excel ignores manual breaks under Fit to.
    setup.fitToPage = true;
    setup.fitToWidth = setup.fitToHeight = 0;
    wb.sheetAt(0).setPageSetup(setup);
    layout = layoutPrintPages(wb, 0);
    QCOMPARE(layout.rowStarts, (QList<qint32>{0, 45, 90}));
    QCOMPARE(layout.colStarts, (QList<qint32>{0, 7}));
}

void CalcPageLayoutTest::printTitlesRepeatOnLaterPages()
{
    Workbook wb = grid(100, 10);
    wb.sheetAt(0).setPrintTitles(CellRange{{0, 0}, {1, Sheet::kMaxCols - 1}},
                                 CellRange{{0, 0}, {Sheet::kMaxRows - 1, 0}});
    const PrintLayout layout = layoutPrintPages(wb, 0);
    // The second row band makes room for the two title rows (30 pt): 43
    // rows; the second column band for column A (72 pt): the rest fits.
    QCOMPARE(layout.rowStarts, (QList<qint32>{0, 45, 88}));
    QCOMPARE(layout.colStarts, (QList<qint32>{0, 7}));
    const PrintPage &first = layout.pages.at(0);
    QVERIFY(!first.titleRows.has_value());
    QVERIFY(!first.titleCols.has_value());
    const PrintPage &second = layout.pages.at(1); // rows 45–87, columns A–G
    QCOMPARE(second.body, range(45, 0, 87, 6));
    QVERIFY(second.titleRows.has_value());
    QCOMPARE(*second.titleRows, range(0, 0, 1, 6));
    QVERIFY(!second.titleCols.has_value());
    QCOMPARE(second.printedSizePt, QSizeF(7 * 72, 30 + 43 * 15));
    const PrintPage &fifth = layout.pages.at(4); // rows 45–87, columns H–J
    QCOMPARE(fifth.body, range(45, 7, 87, 9));
    QCOMPARE(*fifth.titleRows, range(0, 7, 1, 9));
    QCOMPARE(*fifth.titleCols, range(45, 0, 87, 0));
    QCOMPARE(fifth.printedSizePt, QSizeF(72 + 3 * 72, 30 + 43 * 15));
    // A print area below the titles repeats them on every page.
    wb.sheetAt(0).setPrintArea(range(10, 1, 20, 3));
    const PrintLayout below = layoutPrintPages(wb, 0);
    QCOMPARE(below.pages.size(), 1);
    QCOMPARE(*below.pages.first().titleRows, range(0, 1, 1, 3));
    QCOMPARE(*below.pages.first().titleCols, range(10, 0, 20, 0));
}

void CalcPageLayoutTest::hiddenRowsTakeNoRoom()
{
    Workbook wb = grid(100, 10);
    SheetOutline outline;
    for (int row = 10; row < 20; ++row) {
        outline.setLine(OutlineAxis::Rows, row, LineOutline{0, true, false});
    }
    outline.setLine(OutlineAxis::Columns, 1, LineOutline{0, true, false});
    wb.sheetAt(0).setOutline(outline);
    const PrintLayout layout = layoutPrintPages(wb, 0);
    QCOMPARE(layout.rowStarts, (QList<qint32>{0, 55}));
    QCOMPARE(layout.colStarts, (QList<qint32>{0, 8})); // B takes no room: A, C–H fit
    QCOMPARE(layout.pages.first().printedSizePt, QSizeF(7 * 72, 45 * 15));
}

void CalcPageLayoutTest::headingsTakeTheirRoom()
{
    Workbook wb = grid(100, 10);
    Sheet::PageSetup setup;
    setup.headings = true;
    wb.sheetAt(0).setPageSetup(setup);
    const PrintLayout layout = layoutPrintPages(wb, 0);
    QCOMPARE(layout.headingHeightPt, 15.0);
    QCOMPARE(layout.headingWidthPt, (7.0 * 3 + 10) * 0.75); // three digits: row 100
    QCOMPARE(layout.colStarts, (QList<qint32>{0, 6}));
    QCOMPARE(layout.rowStarts, (QList<qint32>{0, 44, 88}));
    QCOMPARE(layout.pages.first().printedSizePt, QSizeF(23.25 + 6 * 72, 15 + 44 * 15));
}

void CalcPageLayoutTest::pageOrderAndFirstNumber()
{
    Workbook wb = grid(100, 10);
    Sheet::PageSetup setup;
    setup.overThenDown = true;
    setup.useFirstPageNumber = true;
    setup.firstPageNumber = 5;
    wb.sheetAt(0).setPageSetup(setup);
    const PrintLayout layout = layoutPrintPages(wb, 0);
    QCOMPARE(layout.pages.size(), 6);
    const QList<QPair<int, int>> order{{0, 0}, {1, 0}, {0, 1}, {1, 1}, {0, 2}, {1, 2}};
    for (int i = 0; i < 6; ++i) {
        QCOMPARE(layout.pages.at(i).across, order.at(i).first);
        QCOMPARE(layout.pages.at(i).down, order.at(i).second);
        QCOMPARE(layout.pages.at(i).number, 5 + i);
    }
    // A first page number Excel is told to ignore ("Auto").
    setup.useFirstPageNumber = false;
    wb.sheetAt(0).setPageSetup(setup);
    QCOMPARE(layoutPrintPages(wb, 0).pages.first().number, 1);
}

void CalcPageLayoutTest::everyPrintAreaPrintsOnItsOwnPages()
{
    Workbook wb = grid(100, 10);
    wb.sheetAt(0).setPrintArea(range(1, 1, 4, 2));
    Sheet::PageSetup setup;
    setup.morePrintAreas = {range(9, 4, 11, 5)};
    wb.sheetAt(0).setPageSetup(setup);
    const PrintLayout layout = layoutPrintPages(wb, 0);
    QCOMPARE(layout.areas, (QList<CellRange>{range(1, 1, 4, 2), range(9, 4, 11, 5)}));
    QCOMPARE(layout.area, range(1, 1, 4, 2));
    QCOMPARE(layout.pages.size(), 2);
    QCOMPARE(layout.pages.at(0).body, range(1, 1, 4, 2));
    QCOMPARE(layout.pages.at(0).area, 0);
    QCOMPARE(layout.pages.at(1).body, range(9, 4, 11, 5));
    QCOMPARE(layout.pages.at(1).area, 1);
    QCOMPARE(layout.pages.at(1).number, 2);
    // Setting a print area replaces every area, as Excel's Set Print Area.
    wb.sheetAt(0).setPrintArea(range(0, 0, 2, 2));
    QVERIFY(wb.sheetAt(0).pageSetup().morePrintAreas.isEmpty());
}

void CalcPageLayoutTest::usedRangeHoldsStylesMergesAndObjects()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Empty"));
    QVERIFY(!printUsedRange(wb, 0).isValid());
    QVERIFY(layoutPrintPages(wb, 0).pages.isEmpty());
    wb.setCellValue(0, {2, 1}, Value(QStringLiteral("x")));
    QCOMPARE(printUsedRange(wb, 0), range(2, 1, 2, 1));
    CellStyle filled;
    filled.bg = Rgb{255, 255, 0};
    wb.sheetAt(0).setStyleIdAt({49, 25}, wb.addStyle(filled)); // a painted empty Z50 prints
    QCOMPARE(printUsedRange(wb, 0), range(2, 1, 49, 25));
    wb.sheetAt(0).addMerge(range(0, 0, 1, 1));
    QCOMPARE(printUsedRange(wb, 0), range(0, 0, 49, 25));
    ChartSpec chart;
    chart.sheet = 0;
    chart.anchorRow = 5;
    chart.anchorCol = 30;
    chart.widthPx = 480; // five default columns
    chart.heightPx = 320; // sixteen default rows
    wb.charts.append(chart);
    QCOMPARE(printUsedRange(wb, 0), range(0, 0, 49, 34));
}

void CalcPageLayoutTest::breakCommandsFollowExcel()
{
    Sheet::PageSetup setup;
    QVERIFY(insertPageBreak(setup, {4, 2})); // C5: above row 5, left of column C
    QCOMPARE(setup.rowBreaks, (QList<qint32>{4}));
    QCOMPARE(setup.colBreaks, (QList<qint32>{2}));
    QVERIFY(insertPageBreak(setup, {9, 0})); // A10: only the row break
    QCOMPARE(setup.rowBreaks, (QList<qint32>{4, 9}));
    QCOMPARE(setup.colBreaks, (QList<qint32>{2}));
    QVERIFY(insertPageBreak(setup, {0, 3})); // D1: only the column break
    QCOMPARE(setup.colBreaks, (QList<qint32>{2, 3}));
    QVERIFY(!insertPageBreak(setup, {0, 0})); // A1: nothing
    QVERIFY(!insertPageBreak(setup, {4, 2})); // already there
    QVERIFY(hasPageBreakAt(setup, {4, 2}));
    QVERIFY(hasPageBreakAt(setup, {4, 7})); // the row break above it
    QVERIFY(!hasPageBreakAt(setup, {6, 6}));
    QVERIFY(removePageBreak(setup, {4, 2}));
    QCOMPARE(setup.rowBreaks, (QList<qint32>{9}));
    QCOMPARE(setup.colBreaks, (QList<qint32>{3}));
    QVERIFY(!removePageBreak(setup, {6, 6}));
    resetPageBreaks(setup);
    QVERIFY(setup.rowBreaks.isEmpty() && setup.colBreaks.isEmpty());
}

void CalcPageLayoutTest::printSettingsFollowStructuralEdits()
{
    Workbook wb = grid(30, 10);
    Sheet &sheet = wb.sheetAt(0);
    sheet.setPrintArea(range(0, 0, 19, 3));
    Sheet::PageSetup setup;
    setup.morePrintAreas = {range(0, 5, 4, 6)};
    setup.rowBreaks = {10};
    setup.colBreaks = {3};
    sheet.setPageSetup(setup);
    sheet.setPrintTitles(CellRange{{0, 0}, {1, Sheet::kMaxCols - 1}}, CellRange{{0, 0}, {Sheet::kMaxRows - 1, 0}});
    applyStructureEdit(wb, {StructureOp::InsertRows, 0, 0, 2});
    const Sheet &moved = wb.sheetAt(0);
    QCOMPARE(*moved.printArea(), range(2, 0, 21, 3));
    QCOMPARE(moved.pageSetup().morePrintAreas, (QList<CellRange>{range(2, 5, 6, 6)}));
    QCOMPARE(*moved.printTitleRows(), (CellRange{{2, 0}, {3, Sheet::kMaxCols - 1}}));
    QCOMPARE(*moved.printTitleCols(), (CellRange{{0, 0}, {Sheet::kMaxRows - 1, 0}}));
    QCOMPARE(moved.pageSetup().rowBreaks, (QList<qint32>{12}));
    QCOMPARE(moved.pageSetup().colBreaks, (QList<qint32>{3}));
    // Columns A and B go: the title column with them, the areas close up.
    applyStructureEdit(wb, {StructureOp::RemoveColumns, 0, 0, 2});
    const Sheet &shrunk = wb.sheetAt(0);
    QCOMPARE(*shrunk.printArea(), range(2, 0, 21, 1));
    QCOMPARE(shrunk.pageSetup().morePrintAreas, (QList<CellRange>{range(2, 3, 6, 4)}));
    QVERIFY(!shrunk.printTitleCols().has_value());
    QVERIFY(shrunk.printTitleRows().has_value());
    QCOMPARE(shrunk.pageSetup().colBreaks, (QList<qint32>{1}));
    QCOMPARE(shrunk.pageSetup().rowBreaks, (QList<qint32>{12}));
    // The row the break sits above is deleted: the break goes with it; a
    // print area whose cells all go is no longer set.
    applyStructureEdit(wb, {StructureOp::RemoveRows, 0, 12, 1});
    QVERIFY(wb.sheetAt(0).pageSetup().rowBreaks.isEmpty());
    applyStructureEdit(wb, {StructureOp::RemoveColumns, 0, 0, 2});
    QCOMPARE(*wb.sheetAt(0).printArea(), range(2, 1, 6, 2)); // the second area is now the first
    QVERIFY(wb.sheetAt(0).pageSetup().morePrintAreas.isEmpty());
}

void CalcPageLayoutTest::paperSizes()
{
    QCOMPARE(paperSizeMm(9), QSizeF(210, 297));
    QCOMPARE(paperSizeMm(1), QSizeF(215.9, 279.4));
    QCOMPARE(paperSizeMm(12), QSizeF(257, 364)); // B4 (JIS)
    QCOMPARE(paperSizeMm(0), paperSizeMm(1));    // not stated: Letter
    QCOMPARE(paperSizeMm(999), paperSizeMm(1));
    QCOMPARE(paperSizeName(20), QStringLiteral("Envelope #10"));
    QCOMPARE(paperSizeName(43), QStringLiteral("Japanese Postcard"));
    const QList<PaperChoice> common = commonPaperSizes();
    QCOMPARE(common.first().code, 1);
    QVERIFY(std::any_of(common.cbegin(), common.cend(), [](const PaperChoice &c) { return c.code == 9; }));
    for (const PaperChoice &choice : common) {
        QCOMPARE(paperSizeName(choice.code), QString::fromLatin1(choice.name));
    }
}

void CalcPageLayoutTest::headerFooterCodesParse()
{
    using Field = HeaderFooterRun::Field;
    const HeaderFooterText text = parseHeaderFooter(
        QStringLiteral("&L&\"Arial,Bold\"&12Draft&CPage &P of &N&R&KFF0000&D&B &T"));
    QCOMPARE(text.left.runs.size(), 1);
    QCOMPARE(text.left.runs.first().text, QStringLiteral("Draft"));
    QCOMPARE(text.left.runs.first().font, QStringLiteral("Arial"));
    QVERIFY(text.left.runs.first().bold);
    QCOMPARE(text.left.runs.first().size, 12.0);
    QCOMPARE(text.center.runs.size(), 4);
    QCOMPARE(text.center.runs.at(0).text, QStringLiteral("Page "));
    QVERIFY(!text.center.runs.at(0).bold); // a section starts afresh
    QCOMPARE(text.center.runs.at(1).field, Field::Page);
    QCOMPARE(text.center.runs.at(3).field, Field::Pages);
    QCOMPARE(text.right.runs.size(), 3);
    QCOMPARE(text.right.runs.at(0).field, Field::Date);
    QCOMPARE(text.right.runs.at(0).color, std::optional<Rgb>(Rgb{255, 0, 0}));
    QVERIFY(text.right.runs.at(1).bold); // &B toggles on
    QCOMPARE(text.right.runs.at(2).field, Field::Time);
    // Text before any section is centred; && is an ampersand; &P+n offsets.
    const HeaderFooterText plain = parseHeaderFooter(QStringLiteral("R&&D &P+2&P-1"));
    QCOMPARE(plain.center.runs.size(), 3);
    QCOMPARE(plain.center.runs.at(0).text, QStringLiteral("R&D "));
    QCOMPARE(plain.center.runs.at(1).pageOffset, 2);
    QCOMPARE(plain.center.runs.at(2).pageOffset, -1);
    // A theme colour has no RGB without the theme; formatting toggles off.
    const HeaderFooterText theme = parseHeaderFooter(QStringLiteral("&K01+000Theme&U&UPlain&\"-,Italic\"Slanted"));
    QVERIFY(!theme.center.runs.at(0).color.has_value());
    QVERIFY(!theme.center.runs.at(1).underline);
    QVERIFY(theme.center.runs.at(2).italic);
    QVERIFY(theme.center.runs.at(2).font.isEmpty()); // "-": the default font
}

void CalcPageLayoutTest::headerFooterFieldsFillIn()
{
    HeaderFooterContext context;
    context.page = 3;
    context.pages = 7;
    context.date = QDate(2026, 10, 9);
    context.time = QTime(15, 7);
    context.fileName = QStringLiteral("Budget.xlsx");
    context.filePath = QStringLiteral("/home/owner/Documents/");
    context.sheetName = QStringLiteral("Q1 Sales");
    const HeaderFooterText text =
        parseHeaderFooter(QStringLiteral("&LPage &P of &N, &P+2&C&D &T&R&Z&F (&A)&G"));
    QCOMPARE(headerFooterSectionText(text.left, context), QStringLiteral("Page 3 of 7, 5"));
    QCOMPARE(headerFooterSectionText(text.center, context), QStringLiteral("10/9/2026 3:07 PM"));
    QCOMPARE(headerFooterSectionText(text.right, context),
             QStringLiteral("/home/owner/Documents/Budget.xlsx (Q1 Sales)"));
    context.fileName.clear();
    QCOMPARE(headerFooterSectionText(parseHeaderFooter(QStringLiteral("&F")).center, context), QStringLiteral("Book1"));
}

void CalcPageLayoutTest::headerFooterPartsRoundTrip()
{
    const HeaderFooterParts parts = headerFooterParts(QStringLiteral("&LDraft&CPage &P of &N&R&D"));
    QCOMPARE(parts.left, QStringLiteral("Draft"));
    QCOMPARE(parts.center, QStringLiteral("Page &[Page] of &[Pages]"));
    QCOMPARE(parts.right, QStringLiteral("&[Date]"));
    for (const QString &code : {QStringLiteral("&LDraft&CPage &P of &N&R&D"),
                                QStringLiteral("&L&\"Arial,Bold\"&12Q&&A&C&A&R&Z&F"),
                                QStringLiteral("&CPage &P+1&R&KFF0000&T&G"), QStringLiteral("&R&\"Courier New,Italic\"x")}) {
        QCOMPARE(headerFooterCode(headerFooterParts(code)), code);
    }
    // The editing spelling back to codes: fields by name (any case), a
    // literal &&[Page] left alone, empty sections left out.
    HeaderFooterParts edited;
    edited.left = QStringLiteral("&[tab]");
    edited.center = QStringLiteral("Page &[Page] of &[Pages] &&[Page]");
    QCOMPARE(headerFooterCode(edited), QStringLiteral("&L&A&CPage &P of &N &&[Page]"));
    QCOMPARE(headerFooterCode({}), QString());
    // A code without sections is the centre's.
    QCOMPARE(headerFooterParts(QStringLiteral("R&&D")).center, QStringLiteral("R&&D"));
}

void CalcPageLayoutTest::headerFooterPresetsAreExcels()
{
    HeaderFooterContext context;
    context.sheetName = QStringLiteral("Sheet1");
    context.fileName = QStringLiteral("Book1.xlsx");
    context.date = QDate(2026, 10, 9);
    const QList<HeaderFooterPreset> presets = headerFooterPresets(context);
    QVERIFY(presets.size() >= 10);
    QCOMPARE(presets.first().code, QString());
    const auto find = [&presets](const QString &code) {
        for (const HeaderFooterPreset &preset : presets) {
            if (preset.code == code) {
                return preset.label;
            }
        }
        return QString();
    };
    QCOMPARE(find(QStringLiteral("&CPage &P")), QStringLiteral("Page 1"));
    QCOMPARE(find(QStringLiteral("&CPage &P of &N")), QStringLiteral("Page 1 of ?"));
    QCOMPARE(find(QStringLiteral("&C&A")), QStringLiteral("Sheet1"));
    QCOMPARE(find(QStringLiteral("&LConfidential&C&D&RPage &P")), QStringLiteral("Confidential, 10/9/2026, Page 1"));
    QCOMPARE(find(QStringLiteral("&C&F")), QStringLiteral("Book1.xlsx"));
}

QTEST_GUILESS_MAIN(CalcPageLayoutTest)
#include "tst_calc_pagelayout.moc"
