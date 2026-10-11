// SPDX-License-Identifier: GPL-3.0-or-later
// Visibility is a formula input, independent of the range's cell values.
#include "workbook.h"
#include "row_visibility.h"
#include "sheet.h"
#include "functions/aggregate_refs.h"

namespace QindaOffice::Calc {
bool Workbook::rowExcluded(qint32 index, qint32 row, bool includeHidden) const
{
    if (index < 0 || index >= sheetCount()) return false;
    const Sheet &sheet = sheetAt(index);
    if (includeHidden && sheet.isRowHidden(row)) return true;
    const AutoFilter &filter = sheet.autoFilter();
    if (!filter.active || row <= filter.range.topLeft.row || row > filter.range.bottomRight.row) return false;
    for (const auto &[column, allowed] : filter.allowed.asKeyValueRange()) {
        if (column < filter.range.topLeft.col || column > filter.range.bottomRight.col) continue;
        const Cell *cell = sheet.cellAt({row, column});
        if (!allowed.contains(cell != nullptr ? toText(cell->value) : QString())) return true;
    }
    return false;
}
QSet<CellKey> rowVisibilityTotals(const Workbook &book)
{
    bool filtering = false;
    for (qint32 s = 0; s < book.sheetCount(); ++s) filtering = filtering || book.sheetAt(s).autoFilter().active;
    if (!filtering) return {};
    QSet<CellKey> totals;
    for (qint32 s = 0; s < book.sheetCount(); ++s) {
        for (const auto &[key, cell] : book.sheetAt(s).cells().asKeyValueRange()) {
            if (containsSubtotal(cell.ast)) totals.insert(CellKey(s, qint32(key & 0xffffffffu), qint32(key >> 32)));
        }
    }
    // The first pass has settled filter-column formulas. A second ordered
    // pass propagates changed visibility totals to their own dependants.
    return totals;
}
} // namespace QindaOffice::Calc
