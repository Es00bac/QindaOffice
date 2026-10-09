// SPDX-License-Identifier: GPL-3.0-or-later
// Printed pages (page_layout.h, M78, D-087): the used range printing falls
// back to, and the cut into pages — bands of rows and columns filled
// greedily inside the printable size at the print scale, manual breaks
// forcing a new band (except under Fit to, where Excel ignores them), print
// titles and headings taking their room on the pages that show them, and
// the bands numbered down then over (or over then down). Paper sizes and
// the Breaks commands are page_setup.cpp's.

#include "page_layout.h"

#include "workbook.h"

#include <functional>

namespace QindaOffice::Calc {

namespace {

constexpr double kPtPerPx = 0.75;      // 96 dpi pixels to points
constexpr double kPtPerInch = 72.0;
constexpr double kMmPerInch = 25.4;
constexpr double kEpsilon = 1e-6;
constexpr double kMinContentPt = 72.0; // a page always keeps an inch to print in

// One band of lines (rows or columns) printed on the same pages.
struct Band {
    qint32 first = 0;
    qint32 last = 0;
    bool manual = false; // it starts at a manual break
};

// The lines of [first, last] cut into bands no longer than available(start)
// (sheet points), a manual break in `breaks` starting a band of its own
// (none when `breaks` is null). A line longer than a page gets a band of
// its own; bands of hidden lines only are left out.
QList<Band> cutBands(qint32 first, qint32 last, const std::function<double(qint32)> &size,
                     const std::function<double(qint32)> &available, const QList<qint32> *breaks)
{
    QList<Band> out;
    qint32 start = first;
    while (start <= last) {
        const double room = available(start);
        double used = 0;
        bool visible = false;
        qint32 line = start;
        for (; line <= last; ++line) {
            if (line != start && breaks != nullptr && breaks->contains(line)) {
                break;
            }
            const double extent = size(line);
            if (visible && extent > 0 && used + extent > room + kEpsilon) {
                break;
            }
            used += extent;
            visible = visible || extent > 0;
        }
        if (visible) {
            out.append({start, line - 1, start != first && breaks != nullptr && breaks->contains(start)});
        }
        start = line;
    }
    return out;
}

// Everything one layout needs about the sheet, read once.
struct Geometry {
    const Sheet *sheet = nullptr;
    Sheet::PageSetup setup;
    std::optional<CellRange> titleRows;
    std::optional<CellRange> titleCols;
    double contentWidth = 0;  // points on paper
    double contentHeight = 0;
    double headingWidth = 0;  // sheet points (scaled on paper)
    double headingHeight = 0;

    [[nodiscard]] double rowSize(qint32 row) const
    {
        return sheet->isRowHidden(row) ? 0.0 : sheet->rowHeight(row) * kPtPerPx;
    }
    [[nodiscard]] double colSize(qint32 col) const
    {
        return sheet->isColumnHidden(col) ? 0.0 : sheet->columnWidth(col) * kPtPerPx;
    }
    // The title lines a band starting at `start` repeats: the titles above
    // (left of) it, none when the band starts at or above the titles.
    [[nodiscard]] static std::optional<QPair<qint32, qint32>> repeated(const std::optional<CellRange> &titles,
                                                                       bool rows, qint32 start)
    {
        if (!titles.has_value()) {
            return std::nullopt;
        }
        const qint32 top = rows ? titles->topLeft.row : titles->topLeft.col;
        const qint32 bottom = qMin(rows ? titles->bottomRight.row : titles->bottomRight.col, start - 1);
        if (bottom < top) {
            return std::nullopt;
        }
        return qMakePair(top, bottom);
    }
    [[nodiscard]] double titleExtent(bool rows, qint32 start) const
    {
        const auto lines = repeated(rows ? titleRows : titleCols, rows, start);
        double extent = 0;
        for (qint32 line = lines ? lines->first : 1; lines && line <= lines->second; ++line) {
            extent += rows ? rowSize(line) : colSize(line);
        }
        return extent;
    }
};

struct AreaBands {
    QList<Band> rows;
    QList<Band> cols;
};

AreaBands bandsFor(const Geometry &geometry, const CellRange &area, double scale, bool useBreaks)
{
    const Sheet::PageSetup &setup = geometry.setup;
    AreaBands bands;
    bands.rows = cutBands(
        area.topLeft.row, area.bottomRight.row, [&geometry](qint32 row) { return geometry.rowSize(row); },
        [&geometry, scale](qint32 start) {
            return geometry.contentHeight / scale - geometry.headingHeight - geometry.titleExtent(true, start);
        },
        useBreaks ? &setup.rowBreaks : nullptr);
    bands.cols = cutBands(
        area.topLeft.col, area.bottomRight.col, [&geometry](qint32 col) { return geometry.colSize(col); },
        [&geometry, scale](qint32 start) {
            return geometry.contentWidth / scale - geometry.headingWidth - geometry.titleExtent(false, start);
        },
        useBreaks ? &setup.colBreaks : nullptr);
    return bands;
}

// The largest whole percent (≤ 100, ≥ 10) at which every area fits Fit to's
// pages wide × tall (0: no limit). The page count only falls as the scale
// does, so a bisection finds it.
int fitPercent(const Geometry &geometry, const QList<CellRange> &areas)
{
    const int wide = geometry.setup.fitToWidth;
    const int tall = geometry.setup.fitToHeight;
    if (wide <= 0 && tall <= 0) {
        return 100;
    }
    const auto fits = [&](int percent) {
        for (const CellRange &area : areas) {
            const AreaBands bands = bandsFor(geometry, area, percent / 100.0, false);
            if ((wide > 0 && bands.cols.size() > wide) || (tall > 0 && bands.rows.size() > tall)) {
                return false;
            }
        }
        return true;
    };
    if (fits(100)) {
        return 100;
    }
    int low = 10; // assumed to fit; Excel prints at 10 % when nothing does
    int high = 100; // known not to fit
    while (high - low > 1) {
        const int mid = (low + high) / 2;
        (fits(mid) ? low : high) = mid;
    }
    return low;
}

double extentOf(const Geometry &geometry, bool rows, qint32 first, qint32 last)
{
    double extent = 0;
    for (qint32 line = first; line <= last; ++line) {
        extent += rows ? geometry.rowSize(line) : geometry.colSize(line);
    }
    return extent;
}

// The print areas: the sheet's (all of a multi-area one), clamped to the
// grid, else the used range.
QList<CellRange> printAreasOf(const Workbook &book, qint32 sheetIndex)
{
    const Sheet &sheet = book.sheetAt(sheetIndex);
    QList<CellRange> areas;
    QList<CellRange> wanted = sheet.pageSetup().morePrintAreas;
    if (sheet.printArea().has_value()) {
        wanted.prepend(*sheet.printArea());
    }
    for (CellRange area : std::as_const(wanted)) {
        area.bottomRight.row = qMin(area.bottomRight.row, Sheet::kMaxRows - 1);
        area.bottomRight.col = qMin(area.bottomRight.col, Sheet::kMaxCols - 1);
        if (area.isValid()) {
            areas.append(area);
        }
    }
    if (areas.isEmpty()) {
        const CellRange used = printUsedRange(book, sheetIndex);
        if (used.isValid()) {
            areas.append(used);
        }
    }
    return areas;
}

} // namespace

CellRange printUsedRange(const Workbook &book, qint32 sheetIndex)
{
    if (sheetIndex < 0 || sheetIndex >= book.sheetCount()) {
        return {};
    }
    const Sheet &sheet = book.sheetAt(sheetIndex);
    qint32 top = Sheet::kMaxRows;
    qint32 left = Sheet::kMaxCols;
    qint32 bottom = -1;
    qint32 right = -1;
    const auto take = [&](const CellAddr &addr) {
        top = qMin(top, addr.row);
        left = qMin(left, addr.col);
        bottom = qMax(bottom, addr.row);
        right = qMax(right, addr.col);
    };
    const auto keyAddr = [](quint64 key) { return CellAddr{qint32(key & 0xffffffffu), qint32(key >> 32)}; };
    for (auto it = sheet.cells().constBegin(); it != sheet.cells().constEnd(); ++it) {
        if (it.value().hasFormula() || !isBlank(it.value().value)) {
            take(keyAddr(it.key()));
        }
    }
    for (auto it = sheet.styleIds().constBegin(); it != sheet.styleIds().constEnd(); ++it) {
        if (it.value() != 0) {
            take(keyAddr(it.key()));
        }
    }
    for (const CellRange &merge : sheet.merges()) {
        take(merge.topLeft);
        take(merge.bottomRight);
    }
    // A floating chart or picture prints with the cells under it.
    const auto takeObject = [&](int row, int col, int widthPx, int heightPx) {
        take({row, col});
        qint32 lastCol = col;
        for (double width = sheet.columnWidth(lastCol); width < widthPx && lastCol + 1 < Sheet::kMaxCols;) {
            width += sheet.columnWidth(++lastCol);
        }
        qint32 lastRow = row;
        for (double height = sheet.rowHeight(lastRow); height < heightPx && lastRow + 1 < Sheet::kMaxRows;) {
            height += sheet.rowHeight(++lastRow);
        }
        take({lastRow, lastCol});
    };
    for (const ChartSpec &chart : book.charts) {
        if (chart.sheet == sheetIndex && chart.anchorRow >= 0 && chart.anchorCol >= 0) {
            takeObject(chart.anchorRow, chart.anchorCol, chart.widthPx, chart.heightPx);
        }
    }
    for (const ImageSpec &image : book.images) {
        if (image.sheet == sheetIndex && image.anchorRow >= 0 && image.anchorCol >= 0) {
            takeObject(image.anchorRow, image.anchorCol, image.widthPx, image.heightPx);
        }
    }
    if (bottom < 0) {
        return {};
    }
    return {{top, left}, {bottom, right}};
}

PrintLayout layoutPrintPages(const Workbook &book, qint32 sheetIndex)
{
    PrintLayout layout;
    if (sheetIndex < 0 || sheetIndex >= book.sheetCount()) {
        return layout;
    }
    const Sheet &sheet = book.sheetAt(sheetIndex);
    Geometry geometry;
    geometry.sheet = &sheet;
    geometry.setup = sheet.pageSetup();
    const Sheet::PageSetup &setup = geometry.setup;
    geometry.titleRows = sheet.printTitleRows();
    geometry.titleCols = sheet.printTitleCols();

    // The paper and the printable rectangle inside the margins.
    const QSizeF paper = paperSizeMm(setup.paperSize) * (kPtPerInch / kMmPerInch);
    layout.paperPt = setup.orientation == 1 ? QSizeF(paper.height(), paper.width()) : paper;
    layout.contentLeftPt = qMax(0.0, setup.marginLeft) * kPtPerInch;
    layout.contentTopPt = qMax(0.0, setup.marginTop) * kPtPerInch;
    layout.contentWidthPt = qMax(kMinContentPt, layout.paperPt.width() - layout.contentLeftPt
                                                    - qMax(0.0, setup.marginRight) * kPtPerInch);
    layout.contentHeightPt = qMax(kMinContentPt, layout.paperPt.height() - layout.contentTopPt
                                                     - qMax(0.0, setup.marginBottom) * kPtPerInch);
    geometry.contentWidth = layout.contentWidthPt;
    geometry.contentHeight = layout.contentHeightPt;

    if (sheet.isChartSheet()) {
        return layout; // a chart sheet prints its chart, not cells
    }
    layout.areas = printAreasOf(book, sheetIndex);
    if (layout.areas.isEmpty()) {
        return layout;
    }
    layout.area = layout.areas.first();
    if (setup.headings) {
        // Excel's headings: the column letters one default row tall, the
        // row numbers as wide as the largest one printed needs.
        qint32 lastRow = 0;
        for (const CellRange &area : std::as_const(layout.areas)) {
            lastRow = qMax(lastRow, area.bottomRight.row);
        }
        geometry.headingHeight = 20.0 * kPtPerPx;
        geometry.headingWidth = (7.0 * QString::number(lastRow + 1).size() + 10.0) * kPtPerPx;
    }

    layout.scalePercent = setup.fitToPage ? fitPercent(geometry, layout.areas) : qBound(10, setup.scale, 400);
    layout.scale = layout.scalePercent / 100.0;
    layout.headingHeightPt = geometry.headingHeight * layout.scale;
    layout.headingWidthPt = geometry.headingWidth * layout.scale;

    // Excel ignores manual breaks under Fit to.
    const bool useBreaks = !setup.fitToPage;
    const int firstNumber = setup.useFirstPageNumber ? setup.firstPageNumber : 1;
    for (qsizetype index = 0; index < layout.areas.size(); ++index) {
        const CellRange &area = layout.areas.at(index);
        const AreaBands bands = bandsFor(geometry, area, layout.scale, useBreaks);
        if (index == 0) {
            for (const Band &band : bands.rows) {
                layout.rowStarts.append(band.first);
                layout.rowStartManual.append(band.manual);
            }
            for (const Band &band : bands.cols) {
                layout.colStarts.append(band.first);
                layout.colStartManual.append(band.manual);
            }
        }
        const auto page = [&](int across, int down) {
            const Band &cols = bands.cols.at(across);
            const Band &rows = bands.rows.at(down);
            PrintPage out;
            out.body = {{rows.first, cols.first}, {rows.last, cols.last}};
            if (const auto titles = Geometry::repeated(geometry.titleRows, true, rows.first)) {
                out.titleRows = CellRange{{titles->first, cols.first}, {titles->second, cols.last}};
            }
            if (const auto titles = Geometry::repeated(geometry.titleCols, false, cols.first)) {
                out.titleCols = CellRange{{rows.first, titles->first}, {rows.last, titles->second}};
            }
            out.across = across;
            out.down = down;
            out.area = int(index);
            out.number = firstNumber + int(layout.pages.size());
            const double width = geometry.headingWidth + geometry.titleExtent(false, cols.first)
                + extentOf(geometry, false, cols.first, cols.last);
            const double height = geometry.headingHeight + geometry.titleExtent(true, rows.first)
                + extentOf(geometry, true, rows.first, rows.last);
            out.printedSizePt = QSizeF(width * layout.scale, height * layout.scale);
            layout.pages.append(out);
        };
        if (setup.overThenDown) {
            for (int down = 0; down < bands.rows.size(); ++down) {
                for (int across = 0; across < bands.cols.size(); ++across) {
                    page(across, down);
                }
            }
        } else {
            for (int across = 0; across < bands.cols.size(); ++across) {
                for (int down = 0; down < bands.rows.size(); ++down) {
                    page(across, down);
                }
            }
        }
    }
    return layout;
}

} // namespace QindaOffice::Calc
