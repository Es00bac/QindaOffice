// SPDX-License-Identifier: GPL-3.0-or-later
// M78 (D-087): the print settings and the rules kept as Excel wrote them
// through inserted and removed rows and columns (structure_internal.h).
// Excel keeps a print area and print titles as defined names, so they
// follow the cells they name; a manual page break sits above a row (left
// of a column) and goes with it; a kept rule's sqref moves like every
// rule's range.

#include "sheet.h"
#include "structure_internal.h"

#include <algorithm>

namespace QindaOffice::Calc {

using namespace StructureMath;

namespace {

// A range moved along the edited axis; nullopt when the whole span went.
std::optional<CellRange> movedAlong(CellRange range, const StructureEdit &edit)
{
    const Band band = bandOf(edit);
    const bool rows = editsRows(edit);
    qint32 lo = rows ? range.topLeft.row : range.topLeft.col;
    qint32 hi = rows ? range.bottomRight.row : range.bottomRight.col;
    if (!movedSpan(lo, hi, band)) {
        return std::nullopt;
    }
    // Inserted lines push a range that reaches the sheet's edge off it:
    // what passes the last row (column) falls off, as in Excel.
    const qint32 edge = (rows ? Sheet::kMaxRows : Sheet::kMaxCols) - 1;
    if (lo > edge) {
        return std::nullopt;
    }
    (rows ? range.topLeft.row : range.topLeft.col) = lo;
    (rows ? range.bottomRight.row : range.bottomRight.col) = qMin(hi, edge);
    return range;
}

std::optional<CellRange> movedOptional(const std::optional<CellRange> &range, const StructureEdit &edit)
{
    return range.has_value() ? movedAlong(*range, edit) : std::nullopt;
}

// Breaks on the edited axis: a break moves with the line it sits above; a
// break whose line is removed goes; the first line never holds one.
QList<qint32> movedBreaks(const QList<qint32> &breaks, const StructureEdit &edit)
{
    const Band band = bandOf(edit);
    QList<qint32> out;
    for (const qint32 line : breaks) {
        const qint32 moved = movedIndex(line, band);
        if (moved > 0 && !out.contains(moved)) {
            out.append(moved);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

void StructureMath::moveSheetPrintAndKept(Sheet &sheet, const StructureEdit &edit)
{
    const bool rows = editsRows(edit);
    // Every area of a multi-area print area moves; one that is gone goes,
    // and the first that stays becomes Sheet::printArea. AGENT-GUARD: the
    // setup is read BEFORE setPrintArea, which clears its further areas.
    Sheet::PageSetup setup = sheet.pageSetup();
    QList<CellRange> areas;
    if (sheet.printArea().has_value()) {
        areas.append(*sheet.printArea());
    }
    areas += setup.morePrintAreas;
    QList<CellRange> movedAreas;
    for (const CellRange &area : std::as_const(areas)) {
        if (const auto moved = movedAlong(area, edit)) {
            movedAreas.append(*moved);
        }
    }
    sheet.setPrintArea(movedAreas.isEmpty() ? std::nullopt : std::optional<CellRange>(movedAreas.first()));
    setup.morePrintAreas = movedAreas.mid(1);
    // Title rows only move with rows, title columns with columns (a title
    // row spans every column and is untouched by a column edit).
    std::optional<CellRange> titleRows = sheet.printTitleRows();
    std::optional<CellRange> titleCols = sheet.printTitleCols();
    if (rows) {
        titleRows = movedOptional(titleRows, edit);
    } else {
        titleCols = movedOptional(titleCols, edit);
    }
    sheet.setPrintTitles(titleRows, titleCols);
    if (rows) {
        setup.rowBreaks = movedBreaks(setup.rowBreaks, edit);
    } else {
        setup.colBreaks = movedBreaks(setup.colBreaks, edit);
    }
    sheet.setPageSetup(setup);

    QList<KeptRule> kept;
    for (KeptRule rule : sheet.keptRules()) {
        if (rule.ranges.isEmpty()) {
            kept.append(rule); // a sqref the reader could not read: nothing to move, nothing to lose
            continue;
        }
        QList<CellRange> ranges;
        for (const CellRange &range : std::as_const(rule.ranges)) {
            if (const auto moved = movedAlong(range, edit)) {
                ranges.append(*moved);
            }
        }
        if (ranges.isEmpty()) {
            continue; // every cell it applied to is gone, as in Excel
        }
        rule.ranges = ranges;
        kept.append(rule);
    }
    sheet.setKeptRules(kept);
}

} // namespace QindaOffice::Calc
