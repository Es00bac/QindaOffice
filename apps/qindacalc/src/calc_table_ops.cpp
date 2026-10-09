// SPDX-License-Identifier: GPL-3.0-or-later
// SpreadsheetDocument's M69 edits (D-078): Excel tables (Insert ▸ Table,
// the Total Row), legacy array formulas (Ctrl+Shift+Enter, edited only as
// a whole), and the guard every cell edit asks — a chart sheet has no
// cells, part of an array cannot change, a locked cell refuses. One undo
// step each, like the rest of the document.

#include "spreadsheet_document.h"

#include "calc_undo.h"

#include "sheet.h"
#include "table_edit.h"

namespace QindaOffice::Apps::QindaCalc {

using CalcEng::CellAddr;
using CalcEng::CellRange;

namespace {

bool overlaps(const CellRange &a, const CellRange &b)
{
    return a.topLeft.row <= b.bottomRight.row && a.bottomRight.row >= b.topLeft.row
        && a.topLeft.col <= b.bottomRight.col && a.bottomRight.col >= b.topLeft.col;
}

bool contains(const CellRange &outer, const CellRange &inner)
{
    return outer.contains(inner.topLeft) && outer.contains(inner.bottomRight);
}

// Every legacy array block of a sheet (their anchors hold the range).
QList<CellRange> arrayBlocks(const CalcEng::Sheet &sheet)
{
    QList<CellRange> blocks;
    for (const CalcEng::Cell &cell : sheet.cells()) {
        if (cell.isArrayAnchor() && cell.arrayRange.cellCount() > 1) {
            blocks.append(cell.arrayRange);
        }
    }
    return blocks;
}

} // namespace

bool SpreadsheetDocument::refuseCellEdit(const CellRange &range)
{
    const CalcEng::Sheet &sheet = m_workbook.sheetAt(m_activeSheet);
    if (sheet.isChartSheet()) {
        emit editRefused(tr("“%1” is a chart sheet: it holds a chart and no cells.").arg(sheet.name()));
        return true;
    }
    // Excel's rule: an array formula changes, or goes, as a whole.
    bool part = false;
    if (range.cellCount() == 1) {
        const auto block = m_workbook.arrayBlockAt(m_activeSheet, range.topLeft);
        part = block.has_value() && block->cellCount() > 1;
    } else {
        for (const CellRange &block : arrayBlocks(sheet)) {
            part = part || (overlaps(block, range) && !contains(range, block));
        }
    }
    if (part) {
        emit editRefused(tr("You can't change part of an array. Select the whole array formula to change or "
                            "delete it, or edit it and press Ctrl+Shift+Enter."));
        return true;
    }
    return refuseLockedCells(range);
}

bool SpreadsheetDocument::refuseArraySplit(bool rows, qint32 at, qint32 count, bool insert)
{
    for (const CellRange &block : arrayBlocks(m_workbook.sheetAt(m_activeSheet))) {
        const qint32 lo = rows ? block.topLeft.row : block.topLeft.col;
        const qint32 hi = rows ? block.bottomRight.row : block.bottomRight.col;
        const bool split = insert ? (at > lo && at <= hi)
                                  : (at <= hi && at + count - 1 >= lo && !(at <= lo && at + count - 1 >= hi));
        if (split) {
            emit editRefused(tr("You can't change part of an array: the rows or columns would cut through one."));
            return true;
        }
    }
    return false;
}

void SpreadsheetDocument::applyArrayFormula(const CellRange &block, const QString &input)
{
    QString text = input.trimmed();
    if (text.startsWith(QLatin1Char('{')) && text.endsWith(QLatin1Char('}'))) {
        text = text.mid(1, text.size() - 2).trimmed();
    }
    if (text.startsWith(QLatin1Char('='))) {
        text = text.mid(1);
    }
    const CalcEng::Sheet &sheet = m_workbook.sheetAt(m_activeSheet);
    if (!block.isValid() || text.isEmpty() || refuseLockedCells(block)) {
        return;
    }
    if (sheet.isChartSheet()) {
        emit editRefused(tr("“%1” is a chart sheet: it holds a chart and no cells.").arg(sheet.name()));
        return;
    }
    const CalcEng::ParseResult parsed = CalcEng::parseFormula(text);
    if (!parsed.error.isEmpty() || !parsed.root) {
        emit editRefused(tr("The array formula has a mistake (%1).").arg(parsed.error));
        return;
    }
    for (const CellRange &other : arrayBlocks(sheet)) {
        if (overlaps(other, block) && !contains(block, other)) {
            emit editRefused(tr("You can't change part of an array."));
            return;
        }
    }
    for (const CellRange &merge : sheet.merges()) {
        if (overlaps(merge, block)) {
            emit editRefused(tr("Array formulas cannot go in merged cells."));
            return;
        }
    }
    for (const CalcEng::TableSpec &table : sheet.tables()) {
        if (block.cellCount() > 1 && overlaps(table.range, block)) {
            emit editRefused(tr("Multi-cell array formulas aren't allowed in tables."));
            return;
        }
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    m_workbook.installArrayFormula(m_activeSheet, block, parsed.root, text);
    pushUndo(before, tr("Array formula"));
    setDirty(true);
    emit contentChanged();
}

bool SpreadsheetDocument::insertTable(const CellRange &range)
{
    if (refuseCellEdit(range)) {
        return false;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    QString error;
    if (!CalcEng::createTable(m_workbook, m_activeSheet, range, &error)) {
        emit editRefused(error);
        return false;
    }
    pushUndo(before, tr("Insert table"));
    setDirty(true);
    emit contentChanged();
    return true;
}

bool SpreadsheetDocument::setTotalsRow(const CellAddr &addr, bool on)
{
    const QList<CalcEng::TableSpec> &tables = m_workbook.sheetAt(m_activeSheet).tables();
    qsizetype index = -1;
    for (qsizetype i = 0; i < tables.size(); ++i) {
        if (tables.at(i).range.contains(addr)) {
            index = i;
        }
    }
    if (index < 0) {
        emit editRefused(tr("Put the cursor in a table first."));
        return false;
    }
    if (refuseLockedCells(tables.at(index).range)) {
        return false;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    QString error;
    if (!CalcEng::setTableTotalsRow(m_workbook, m_activeSheet, index, on, &error)) {
        emit editRefused(error);
        return false;
    }
    pushUndo(before, on ? tr("Total row") : tr("Remove total row"));
    setDirty(true);
    emit contentChanged();
    return true;
}

} // namespace QindaOffice::Apps::QindaCalc
