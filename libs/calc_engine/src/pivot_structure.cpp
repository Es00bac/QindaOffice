// SPDX-License-Identifier: GPL-3.0-or-later
// PivotTables through structural edits and sheet renames (pivot_edit.h;
// M74, D-083): a pivot moves with its cells; a cache's source range moves
// like a formula reference (it grows with rows inserted inside it, shrinks
// with rows removed from it); a renamed sheet's name follows into the
// sources. Called by structure.cpp and structure_sheets.cpp.

#include "pivot_edit.h"

#include "sheet.h"
#include "structure_internal.h"
#include "workbook.h"

namespace QindaOffice::Calc {

using namespace StructureMath;

bool pivotSplitBy(const Workbook &book, qint32 sheet, bool rows, qint32 at, qint32 count, bool insert)
{
    if (sheet < 0 || sheet >= book.sheetCount()) {
        return false;
    }
    for (const PivotTable &pivot : book.sheetAt(sheet).pivots()) {
        const CellRange &extent = pivot.extent;
        if (!extent.isValid()) {
            continue;
        }
        const qint32 lo = rows ? extent.topLeft.row : extent.topLeft.col;
        const qint32 hi = rows ? extent.bottomRight.row : extent.bottomRight.col;
        if (insert ? (at > lo && at <= hi) : (at <= hi && at + count - 1 >= lo)) {
            return true; // Excel: "… move the PivotTable and try again"
        }
    }
    return false;
}

void movePivotsForEdit(Workbook &book, const StructureEdit &edit)
{
    const Band band = bandOf(edit);
    const bool rows = editsRows(edit);
    const QString editedName = book.sheetName(edit.sheet);
    Sheet &sheet = book.sheetAt(edit.sheet);
    QList<PivotTable> kept;
    QList<qint32> moved;
    for (PivotTable pivot : sheet.pivots()) {
        CellRange &extent = pivot.extent;
        if (extent.isValid()) {
            qint32 lo = rows ? extent.topLeft.row : extent.topLeft.col;
            qint32 hi = rows ? extent.bottomRight.row : extent.bottomRight.col;
            const bool cut = band.insert ? (band.at > lo && band.at <= hi)
                                         : (band.at <= hi && band.at + band.count - 1 >= lo);
            if (cut) {
                continue; // the edit went through it: the definition goes, the cells stay
            }
            if (!movedSpan(lo, hi, band)) {
                continue;
            }
            const qint32 shift = lo - (rows ? extent.topLeft.row : extent.topLeft.col);
            (rows ? extent.topLeft.row : extent.topLeft.col) = lo;
            (rows ? extent.bottomRight.row : extent.bottomRight.col) = hi;
            (rows ? pivot.location.row : pivot.location.col) += shift;
            if (shift != 0) {
                moved.append(qint32(kept.size()));
            }
        } else if (const qint32 at = movedIndex(rows ? pivot.location.row : pivot.location.col, band); at >= 0) {
            (rows ? pivot.location.row : pivot.location.col) = at;
        }
        kept.append(pivot);
    }
    sheet.setPivots(kept);
    // Their layouts follow the move (the cells move with the sheet).
    for (const qint32 index : std::as_const(moved)) {
        rebuildPivotRender(book, {edit.sheet, index});
    }
    for (PivotCache &cache : book.pivotCaches) {
        if (!cache.source.name.isEmpty() || !cache.source.range.isValid()
            || unquotedSheetName(cache.source.sheet).compare(editedName, Qt::CaseInsensitive) != 0) {
            continue; // a table moves by itself
        }
        CellRange &range = cache.source.range;
        qint32 lo = rows ? range.topLeft.row : range.topLeft.col;
        qint32 hi = rows ? range.bottomRight.row : range.bottomRight.col;
        if (!movedSpan(lo, hi, band)) {
            range = CellRange{}; // its data is gone: a refresh says so
            continue;
        }
        (rows ? range.topLeft.row : range.topLeft.col) = lo;
        (rows ? range.bottomRight.row : range.bottomRight.col) = hi;
    }
}

void requalifyPivotSources(Workbook &book, const QString &from, const QString &to)
{
    for (PivotCache &cache : book.pivotCaches) {
        if (cache.source.name.isEmpty() && unquotedSheetName(cache.source.sheet).compare(from, Qt::CaseInsensitive) == 0
            && !to.isEmpty()) {
            cache.source.sheet = to;
        }
    }
}

} // namespace QindaOffice::Calc
