// SPDX-License-Identifier: GPL-3.0-or-later
// Trying values in cells and giving the cells back (what_if_internal.h,
// M78, D-087).

#include "what_if_internal.h"

#include "eval.h"
#include "sheet.h"
#include "workbook.h"

#include <QLocale>

namespace QindaOffice::Calc::WhatIf {

SavedCell saveCell(const Workbook &book, qint32 sheet, const CellAddr &addr)
{
    SavedCell saved;
    saved.addr = addr;
    if (const Cell *cell = book.sheetAt(sheet).cellAt(addr)) {
        saved.cell = *cell;
    }
    return saved;
}

CellKey restoreCell(Workbook &book, qint32 sheet, const SavedCell &saved)
{
    Sheet &target = book.sheetAt(sheet);
    const CellKey key(sheet, saved.addr.row, saved.addr.col);
    if (!saved.cell.has_value()) {
        // AGENT-GUARD: dropCellContent, not clearCell — clearCell also drops
        // the cell's style, which a trial never touched.
        static_cast<void>(target.dropCellContent(saved.addr));
        book.dependencies().updateCell(key, {});
        return key;
    }
    target.ensureCell(saved.addr) = *saved.cell;
    if (saved.cell->ast) {
        EvalContext ctx;
        ctx.host = &book;
        ctx.sheet = sheet;
        ctx.here = saved.addr;
        // updateCell before addVolatile: updateCell clears the volatile mark.
        book.dependencies().updateCell(key, collectTargets(saved.cell->ast, ctx));
        if (saved.cell->isVolatile) {
            book.dependencies().addVolatile(key);
        }
    } else {
        book.dependencies().updateCell(key, {});
    }
    return key;
}

CellKey putTrialValue(Workbook &book, qint32 sheet, const CellAddr &addr, const Value &value)
{
    Cell &cell = book.sheetAt(sheet).ensureCell(addr);
    cell.ast.reset();
    cell.formula.clear();
    cell.dynamicArray = false;
    cell.value = value;
    return CellKey(sheet, addr.row, addr.col);
}

QString numberText(double value)
{
    return QString::number(value, 'g', QLocale::FloatingPointShortest);
}

} // namespace QindaOffice::Calc::WhatIf
