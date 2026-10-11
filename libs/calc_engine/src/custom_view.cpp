// SPDX-License-Identifier: GPL-3.0-or-later
// Excel's Custom Views (custom_view.h, M82, D-091), and their sheet parts
// through inserted and removed rows and columns (structure_internal.h):
// hidden lines, the filter range, the print area and titles, page breaks
// and the cells a view shows follow the cells, as Excel's names do.

#include "custom_view.h"

#include "structure_internal.h"
#include "workbook.h"

#include <QUuid>

#include <algorithm>

namespace QindaOffice::Calc {

namespace {

QList<qint32> hiddenLines(const Sheet &sheet, OutlineAxis axis)
{
    QList<qint32> out;
    const QMap<qint32, LineOutline> &lines = sheet.outline().lines(axis);
    for (auto it = lines.cbegin(); it != lines.cend(); ++it) {
        if (it.value().hidden) {
            out.append(it.key());
        }
    }
    return out;
}

// The sheet's hidden lines of `axis` made exactly `wanted` (grouping and
// collapsed marks stay as they are).
void setHiddenLines(SheetOutline &outline, OutlineAxis axis, const QList<qint32> &wanted)
{
    const QList<qint32> keys = outline.lines(axis).keys();
    for (const qint32 line : keys) {
        LineOutline state = outline.line(axis, line);
        if (state.hidden && !wanted.contains(line)) {
            state.hidden = false;
            outline.setLine(axis, line, state);
        }
    }
    for (const qint32 line : wanted) {
        LineOutline state = outline.line(axis, line);
        if (!state.hidden) {
            state.hidden = true;
            outline.setLine(axis, line, state);
        }
    }
}

std::optional<CellRange> movedRange(std::optional<CellRange> range, const StructureMath::Band &band, bool rows)
{
    if (!range.has_value()) {
        return range;
    }
    qint32 &lo = rows ? range->topLeft.row : range->topLeft.col;
    qint32 &hi = rows ? range->bottomRight.row : range->bottomRight.col;
    if (!StructureMath::movedSpan(lo, hi, band)) {
        return std::nullopt;
    }
    return range;
}

QList<qint32> movedLines(const QList<qint32> &lines, const StructureMath::Band &band, bool keepFirst)
{
    QList<qint32> out;
    for (const qint32 line : lines) {
        const qint32 moved = StructureMath::movedIndex(line, band);
        if (moved >= (keepFirst ? 0 : 1) && !out.contains(moved)) {
            out.append(moved);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

CellAddr movedCell(CellAddr addr, const StructureMath::Band &band, bool rows)
{
    qint32 &line = rows ? addr.row : addr.col;
    const qint32 moved = StructureMath::movedIndex(line, band);
    line = moved >= 0 ? moved : qMax(0, band.at - 1);
    return addr;
}

} // namespace

QString newCustomViewGuid()
{
    return QUuid::createUuid().toString(QUuid::WithBraces).toUpper();
}

bool customViewsAllowed(const Workbook &wb)
{
    for (qint32 s = 0; s < wb.sheetCount(); ++s) {
        if (!wb.sheetAt(s).tables().isEmpty()) {
            return false;
        }
    }
    return true;
}

const Sheet::CustomView *customViewOf(const Sheet &sheet, const QString &guid)
{
    for (const Sheet::CustomView &view : sheet.customViews()) {
        if (view.guid.compare(guid, Qt::CaseInsensitive) == 0) {
            return &view;
        }
    }
    return nullptr;
}

bool deleteCustomView(Workbook &wb, const QString &guid)
{
    const qsizetype before = wb.customViews.size();
    wb.customViews.removeIf([&guid](const CustomViewSpec &view) { return view.guid.compare(guid, Qt::CaseInsensitive) == 0; });
    for (qint32 s = 0; s < wb.sheetCount(); ++s) {
        QList<Sheet::CustomView> parts = wb.sheetAt(s).customViews();
        if (parts.removeIf([&guid](const Sheet::CustomView &view) { return view.guid.compare(guid, Qt::CaseInsensitive) == 0; }) > 0) {
            wb.sheetAt(s).setCustomViews(parts);
        }
    }
    return wb.customViews.size() != before;
}

QString addCustomView(Workbook &wb, const QString &name, bool printSettings, bool hiddenRowsCols, qint32 activeSheet,
                      const QHash<qint32, Sheet::CustomView> &window)
{
    // Excel's Add replaces a view of the same name.
    for (const CustomViewSpec &view : QList<CustomViewSpec>(wb.customViews)) {
        if (view.name.compare(name, Qt::CaseInsensitive) == 0) {
            deleteCustomView(wb, view.guid);
        }
    }
    CustomViewSpec spec;
    spec.name = name;
    spec.guid = newCustomViewGuid();
    spec.includePrintSettings = printSettings;
    spec.includeHiddenRowCol = hiddenRowsCols;
    spec.activeSheet = activeSheet >= 0 && activeSheet < wb.sheetCount() ? wb.sheetName(activeSheet) : QString();
    // Excel's window geometry for the view (its own defaults).
    spec.attributes = {{QStringLiteral("windowWidth"), QStringLiteral("1920")},
                       {QStringLiteral("windowHeight"), QStringLiteral("1000")}};
    for (qint32 s = 0; s < wb.sheetCount(); ++s) {
        Sheet &sheet = wb.sheetAt(s);
        if (sheet.isChartSheet()) {
            continue;
        }
        Sheet::CustomView part = window.value(s);
        part.guid = spec.guid;
        part.freezeRows = sheet.freezeRows();
        part.freezeCols = sheet.freezeCols();
        // The far pane's first cell is a frozen pane's (past the frozen
        // lines) or a split's; a view with neither has none.
        if (part.freezeRows > 0 || part.freezeCols > 0) {
            part.splitX = 0;
            part.splitY = 0;
            part.splitTopLeft = {qMax(part.splitTopLeft.row, part.freezeRows), qMax(part.splitTopLeft.col, part.freezeCols)};
        } else if (part.splitX <= 0 && part.splitY <= 0) {
            part.splitTopLeft = {0, 0};
        }
        part.sheetHidden = sheet.isHidden();
        if (hiddenRowsCols) {
            part.hiddenRows = hiddenLines(sheet, OutlineAxis::Rows);
            part.hiddenColumns = hiddenLines(sheet, OutlineAxis::Columns);
            part.filter = sheet.autoFilter().active && sheet.autoFilter().range.isValid();
            part.autoFilter = sheet.autoFilter();
        }
        if (printSettings) {
            part.printSettings = true;
            part.pageSetup = sheet.pageSetup();
            part.printArea = sheet.printArea();
            part.printTitleRows = sheet.printTitleRows();
            part.printTitleCols = sheet.printTitleCols();
        }
        QList<Sheet::CustomView> parts = sheet.customViews();
        parts.append(part);
        sheet.setCustomViews(parts);
    }
    wb.customViews.append(spec);
    return spec.guid;
}

bool showCustomView(Workbook &wb, const QString &guid, qint32 *activeSheet)
{
    const auto spec = std::find_if(wb.customViews.cbegin(), wb.customViews.cend(), [&guid](const CustomViewSpec &view) {
        return view.guid.compare(guid, Qt::CaseInsensitive) == 0;
    });
    if (spec == wb.customViews.cend()) {
        return false;
    }
    for (qint32 s = 0; s < wb.sheetCount(); ++s) {
        Sheet &sheet = wb.sheetAt(s);
        const Sheet::CustomView *part = customViewOf(sheet, guid);
        if (part == nullptr) {
            continue;
        }
        sheet.setFreeze(part->freezeRows, part->freezeCols);
        sheet.setHidden(part->sheetHidden);
        if (spec->includeHiddenRowCol) {
            SheetOutline outline = sheet.outline();
            setHiddenLines(outline, OutlineAxis::Rows, part->hiddenRows);
            setHiddenLines(outline, OutlineAxis::Columns, part->hiddenColumns);
            sheet.setOutline(outline);
            sheet.setAutoFilter(part->filter ? part->autoFilter : AutoFilter{});
        }
        if (spec->includePrintSettings && part->printSettings) {
            // AGENT-GUARD: the page setup goes in AFTER the print area —
            // setPrintArea clears the setup's further print areas.
            sheet.setPrintArea(part->printArea);
            sheet.setPrintTitles(part->printTitleRows, part->printTitleCols);
            sheet.setPageSetup(part->pageSetup);
        }
    }
    wb.recalcAll(); // A restored view changes hidden/filter-aware totals.
    if (activeSheet != nullptr) {
        *activeSheet = wb.sheetIndex(spec->activeSheet);
    }
    return true;
}

void StructureMath::moveCustomViews(Sheet &sheet, const StructureEdit &edit)
{
    if (sheet.customViews().isEmpty()) {
        return;
    }
    const Band band = bandOf(edit);
    const bool rows = editsRows(edit);
    QList<Sheet::CustomView> parts = sheet.customViews();
    for (Sheet::CustomView &part : parts) {
        if (rows) {
            part.hiddenRows = movedLines(part.hiddenRows, band, true);
            part.printTitleRows = movedRange(part.printTitleRows, band, true);
            part.pageSetup.rowBreaks = movedLines(part.pageSetup.rowBreaks, band, false);
        } else {
            part.hiddenColumns = movedLines(part.hiddenColumns, band, true);
            part.printTitleCols = movedRange(part.printTitleCols, band, false);
            part.pageSetup.colBreaks = movedLines(part.pageSetup.colBreaks, band, false);
        }
        part.printArea = movedRange(part.printArea, band, rows);
        if (part.autoFilter.range.isValid()) {
            const std::optional<CellRange> range = movedRange(part.autoFilter.range, band, rows);
            part.autoFilter.range = range.value_or(CellRange());
            part.filter = part.filter && range.has_value();
        }
        part.topLeft = movedCell(part.topLeft, band, rows);
        part.splitTopLeft = movedCell(part.splitTopLeft, band, rows);
        part.activeCell = movedCell(part.activeCell, band, rows);
    }
    sheet.setCustomViews(parts);
}

} // namespace QindaOffice::Calc
