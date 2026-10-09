// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Sheet printing (M78, D-087): how Excel cuts a sheet into printed pages —
// the print area (or the used range), the print titles repeated on every
// page, the scale or Fit to N × M pages, manual page breaks (ignored under
// Fit to, as in Excel), hidden rows and columns, the margins, the row and
// column headings, centring and the page order. Print, Export PDF and Page
// Break Preview all ask this one function, so the paper, the PDF and the
// preview cannot disagree. Pure arithmetic over the sheet's geometry; Qt
// Core only.
//
// Units: the engine's row heights and column widths are pixels at 96 dpi;
// paper and margins are points (1/72 inch) here. One pixel is 0.75 pt. A
// page's grid is printed at `scale`: a 96 px (72 pt) column takes 72 ×
// scale points of paper.
//
// AGENT-CONTRACT (renderer, apps/qindacalc): draw page i of `pages` inside
// the content rectangle (contentLeftPt/TopPt/WidthPt/HeightPt). Its grid —
// the headings band when PageSetup::headings is on (headingHeightPt tall
// above, headingWidthPt wide on the left), then the title rows above the
// body and the title columns left of it (with both, the corner cells of
// the title rows × title columns too) — is printedSizePt big on paper;
// with centerHorizontally / centerVertically the grid is offset by half of
// what is left of the content rectangle on that axis. Headers sit
// marginHeader from the paper's top edge, footers marginFooter from its
// bottom (header_footer.h), outside the content rectangle, as in Excel:
// with alignWithMargins their left and right sections line up with the
// left and right margins (else with the paper's edges), with scaleWithDoc
// their text takes the print scale. Page i uses the first-page header and
// footer when differentFirst and it is the first page printed, the even
// ones when differentOddEven and its number is even, else the odd ones.
// PageSetup::errors says how error cells print (as shown, blank, "--",
// "#N/A"); gridLines, blackAndWhite and draft likewise are the renderer's.

#include "sheet.h"
#include "value.h"

#include <QList>
#include <QSizeF>

#include <optional>

namespace QindaOffice::Calc {

class Workbook;

// One printed page.
struct PrintPage {
    CellRange body;                     // the print-area cells on this page
    // The Print Titles a page repeats: the title rows above the body, over
    // the body's columns, and the title columns left of it, over the body's
    // rows. None on a page whose body already starts at or above them.
    std::optional<CellRange> titleRows;
    std::optional<CellRange> titleCols;
    int number = 1;                     // the page number printed (&P): firstPageNumber-based
    int across = 0;                     // its column band (0-based) and row band, within its print area
    int down = 0;
    int area = 0;                       // which print area it belongs to (PrintLayout::areas)
    QSizeF printedSizePt;               // the grid on paper (headings, titles and body), scale applied
};

struct PrintLayout {
    CellRange area;          // what prints: the (first) print area, or the used range
    // Every print area, in the order Excel prints them (a multi-area print
    // area: Sheet::printArea then PageSetup::morePrintAreas); each starts
    // on a page of its own. One entry, `area`, otherwise.
    QList<CellRange> areas;
    double scale = 1.0;      // the scale the pages print at (Fit to works it out)
    int scalePercent = 100;  // the same, as the whole percent Excel shows
    QSizeF paperPt;          // the paper, orientation applied
    // The printable rectangle inside the margins, in points from the paper's top-left.
    double contentLeftPt = 0;
    double contentTopPt = 0;
    double contentWidthPt = 0;
    double contentHeightPt = 0;
    // The row and column headings' bands on paper (0 without headings):
    // the column letters' height above the grid, the row numbers' width
    // left of it.
    double headingHeightPt = 0;
    double headingWidthPt = 0;
    // Where each row band and column band of the FIRST print area starts
    // (the rows / columns that begin a page), and which of those starts are
    // manual breaks — Page Break Preview draws automatic ones dashed and
    // manual ones solid. Every page's own body is in `pages`.
    QList<qint32> rowStarts;
    QList<qint32> colStarts;
    QList<bool> rowStartManual;
    QList<bool> colStartManual;
    QList<PrintPage> pages;  // in print order (down, then over — or over, then down)
};

// Excel's paperSize codes — Windows' DMPAPER values, which Excel writes —
// in millimetres, portrait: 1 Letter, 3 Tabloid, 5 Legal, 7 Executive, 8 A3,
// 9 A4, 11 A5, 12 B4 (JIS), 13 B5 (JIS), 20 Envelope #10, 27 Envelope DL,
// 28 Envelope C5, 34 Envelope B5, 37 Envelope Monarch, 43 Japanese
// Postcard, 70 A6, …. Unknown codes and 0 ("not stated") are Letter
// (Excel's default in a US locale).
[[nodiscard]] QSizeF paperSizeMm(int code);
// The codes the Page Layout ▸ Size menu offers, in Excel's order, with
// their names ("Letter", "Legal", "A4", …).
struct PaperChoice {
    int code;
    const char *name;
};
[[nodiscard]] QList<PaperChoice> commonPaperSizes();
// The name Excel gives a code ("A4", "Envelope #10"); empty when unknown.
[[nodiscard]] QString paperSizeName(int code);

// The used range printing falls back to without a print area: every cell
// with content or a style, every merge, every floating chart and picture,
// as Excel's. Invalid when the sheet is empty.
[[nodiscard]] CellRange printUsedRange(const Workbook &book, qint32 sheet);

// Lays out the sheet's pages from its PageSetup, print area and titles.
// An empty sheet (and a chart sheet) gives no pages.
[[nodiscard]] PrintLayout layoutPrintPages(const Workbook &book, qint32 sheet);

// Page Layout ▸ Breaks, as Excel does them. Pure functions over the setup.
// Insert Page Break: a row break above the cursor's row and a column break
// left of its column — only the row break when the cursor is in column A,
// only the column break when it is in row 1; false when nothing was added
// (A1, or the breaks are there already).
bool insertPageBreak(Sheet::PageSetup &setup, const CellAddr &cursor);
// Remove Page Break: the manual breaks along the cursor cell's top and left
// edges (above its row, left of its column); false when there was none.
bool removePageBreak(Sheet::PageSetup &setup, const CellAddr &cursor);
// Reset All Page Breaks: every manual break goes.
void resetPageBreaks(Sheet::PageSetup &setup);
// A manual break borders the cursor's cell (Remove Page Break applies).
[[nodiscard]] bool hasPageBreakAt(const Sheet::PageSetup &setup, const CellAddr &cursor);

} // namespace QindaOffice::Calc
