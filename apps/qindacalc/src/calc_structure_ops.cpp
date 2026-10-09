// SPDX-License-Identifier: GPL-3.0-or-later
// SpreadsheetDocument's structural edits: rows and columns, sorting,
// merging, geometry, fill, and the sheet list. Split out of
// spreadsheet_document.cpp to keep both files inside the line budget.
//
// Every entry point here follows the same shape as the rest of the
// document: snapshot the workbook, mutate through calc_engine, push ONE
// undo command, mark dirty, notify. The engine side (structure.h) is what
// keeps formulas, named ranges, merges, geometry and charts pointing at the
// right data after cells move; this file owns only the undo and signalling.

#include "spreadsheet_document.h"

#include "calc_undo.h"

#include "parser.h"
#include "sheet.h"
#include "structure.h"
#include "table_edit.h"

namespace QindaOffice::Apps::QindaCalc {

using CalcEng::CellAddr;
using CalcEng::CellRange;
using CalcEng::StructureEdit;
using CalcEng::StructureOp;

namespace {

// Structural edits are bounded so a mis-typed count cannot ask the engine
// to move a million rows one at a time.
constexpr qint32 kMaxBand = 1024;

} // namespace

void SpreadsheetDocument::insertRows(qint32 at, qint32 count)
{
    if (refuseOnProtectedSheet(m_workbook.sheetAt(m_activeSheet).protection().insertRows, tr("insert rows"))) {
        return;
    }
    if (at < 0 || count <= 0 || count > kMaxBand) {
        return;
    }
    // Through an array formula, or on a chart sheet, nothing moves (M69).
    if (m_workbook.sheetAt(m_activeSheet).isChartSheet() || refuseArraySplit(true, at, count, true)) {
        if (m_workbook.sheetAt(m_activeSheet).isChartSheet()) {
            emit editRefused(tr("A chart sheet has no rows or columns."));
        }
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    CalcEng::applyStructureEdit(m_workbook, {StructureOp::InsertRows, m_activeSheet, at, count});
    pushUndo(before, count == 1 ? tr("Insert row") : tr("Insert rows"));
    setDirty(true);
    emit contentChanged();
}

void SpreadsheetDocument::removeRows(qint32 at, qint32 count)
{
    if (refuseOnProtectedSheet(m_workbook.sheetAt(m_activeSheet).protection().deleteRows, tr("delete rows"))) {
        return;
    }
    if (at < 0 || count <= 0 || count > kMaxBand) {
        return;
    }
    // Through an array formula, or on a chart sheet, nothing moves (M69).
    if (m_workbook.sheetAt(m_activeSheet).isChartSheet() || refuseArraySplit(true, at, count, false)) {
        if (m_workbook.sheetAt(m_activeSheet).isChartSheet()) {
            emit editRefused(tr("A chart sheet has no rows or columns."));
        }
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    CalcEng::applyStructureEdit(m_workbook, {StructureOp::RemoveRows, m_activeSheet, at, count});
    pushUndo(before, count == 1 ? tr("Delete row") : tr("Delete rows"));
    setDirty(true);
    emit contentChanged();
}

void SpreadsheetDocument::insertColumns(qint32 at, qint32 count)
{
    if (refuseOnProtectedSheet(m_workbook.sheetAt(m_activeSheet).protection().insertColumns, tr("insert columns"))) {
        return;
    }
    if (at < 0 || count <= 0 || count > kMaxBand) {
        return;
    }
    // Through an array formula, or on a chart sheet, nothing moves (M69).
    if (m_workbook.sheetAt(m_activeSheet).isChartSheet() || refuseArraySplit(false, at, count, true)) {
        if (m_workbook.sheetAt(m_activeSheet).isChartSheet()) {
            emit editRefused(tr("A chart sheet has no rows or columns."));
        }
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    CalcEng::applyStructureEdit(m_workbook,
                                {StructureOp::InsertColumns, m_activeSheet, at, count});
    pushUndo(before, count == 1 ? tr("Insert column") : tr("Insert columns"));
    setDirty(true);
    emit contentChanged();
}

void SpreadsheetDocument::removeColumns(qint32 at, qint32 count)
{
    if (refuseOnProtectedSheet(m_workbook.sheetAt(m_activeSheet).protection().deleteColumns, tr("delete columns"))) {
        return;
    }
    if (at < 0 || count <= 0 || count > kMaxBand) {
        return;
    }
    // Through an array formula, or on a chart sheet, nothing moves (M69).
    if (m_workbook.sheetAt(m_activeSheet).isChartSheet() || refuseArraySplit(false, at, count, false)) {
        if (m_workbook.sheetAt(m_activeSheet).isChartSheet()) {
            emit editRefused(tr("A chart sheet has no rows or columns."));
        }
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    CalcEng::applyStructureEdit(m_workbook,
                                {StructureOp::RemoveColumns, m_activeSheet, at, count});
    pushUndo(before, count == 1 ? tr("Delete column") : tr("Delete columns"));
    setDirty(true);
    emit contentChanged();
}

void SpreadsheetDocument::sortRange(const CellRange &range, qint32 keyColumn, bool ascending,
                                    bool headerRow)
{
    if (refuseOnProtectedSheet(m_workbook.sheetAt(m_activeSheet).protection().sort, tr("sort")) || refuseCellEdit(range)) {
        return;
    }
    if (!range.isValid()) {
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    CalcEng::sortRange(m_workbook, m_activeSheet, range, keyColumn, ascending, headerRow);
    pushUndo(before, tr("Sort"));
    setDirty(true);
    emit contentChanged();
}

void SpreadsheetDocument::mergeCells(const CellRange &range)
{
    if (refuseOnProtectedSheet(m_workbook.sheetAt(m_activeSheet).protection().formatCells, tr("merge cells"))) {
        return;
    }
    if (!range.isValid() || range.cellCount() < 2) {
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    CalcEng::Sheet &sheet = m_workbook.sheetAt(m_activeSheet);
    // A cell can only belong to one merge: drop every existing merge the new
    // one overlaps, or the covering lookup would depend on insertion order.
    const QList<CellRange> existing = sheet.merges();
    QList<CellRange> kept;
    for (const CellRange &merge : existing) {
        const bool overlaps = merge.topLeft.row <= range.bottomRight.row
            && merge.bottomRight.row >= range.topLeft.row
            && merge.topLeft.col <= range.bottomRight.col
            && merge.bottomRight.col >= range.topLeft.col;
        if (!overlaps) {
            kept.append(merge);
        }
    }
    kept.append(range);
    sheet.replaceContents(sheet.cells(), sheet.styleIds(), sheet.columnWidths(),
                          sheet.rowHeights(), kept);
    pushUndo(before, tr("Merge cells"));
    setDirty(true);
    emit contentChanged();
}

void SpreadsheetDocument::unmergeCells(const CellAddr &anchor)
{
    if (refuseOnProtectedSheet(m_workbook.sheetAt(m_activeSheet).protection().formatCells, tr("unmerge cells"))) {
        return;
    }
    CalcEng::Sheet &sheet = m_workbook.sheetAt(m_activeSheet);
    const CellRange *covering = sheet.mergeCovering(anchor);
    if (covering == nullptr) {
        return;
    }
    const CellAddr topLeft = covering->topLeft;
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    sheet.removeMergeAt(topLeft);
    pushUndo(before, tr("Unmerge cells"));
    setDirty(true);
    emit contentChanged();
}

void SpreadsheetDocument::setColumnWidths(const QList<qint32> &columns, qreal px)
{
    if (refuseOnProtectedSheet(m_workbook.sheetAt(m_activeSheet).protection().formatColumns, tr("change column widths"))) {
        return;
    }
    if (columns.isEmpty() || px <= 0) {
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    CalcEng::Sheet &sheet = m_workbook.sheetAt(m_activeSheet);
    for (const qint32 col : columns) {
        sheet.setColumnWidth(col, px);
    }
    pushUndo(before, tr("Column width"));
    setDirty(true);
    emit contentChanged();
}

void SpreadsheetDocument::setRowHeights(const QList<qint32> &rows, qreal px)
{
    if (refuseOnProtectedSheet(m_workbook.sheetAt(m_activeSheet).protection().formatRows, tr("change row heights"))) {
        return;
    }
    if (rows.isEmpty() || px <= 0) {
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    CalcEng::Sheet &sheet = m_workbook.sheetAt(m_activeSheet);
    for (const qint32 row : rows) {
        sheet.setRowHeight(row, px);
    }
    pushUndo(before, tr("Row height"));
    setDirty(true);
    emit contentChanged();
}

void SpreadsheetDocument::fillRange(const CellRange &range, bool down)
{
    if (refuseCellEdit(range)) {
        return;
    }
    if (!range.isValid() || range.cellCount() < 2) {
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    CalcEng::Sheet &sheet = m_workbook.sheetAt(m_activeSheet);
    for (qint32 row = range.topLeft.row; row <= range.bottomRight.row; ++row) {
        for (qint32 col = range.topLeft.col; col <= range.bottomRight.col; ++col) {
            const CellAddr source = down ? CellAddr{range.topLeft.row, col}
                                         : CellAddr{row, range.topLeft.col};
            const CellAddr target{row, col};
            if (target == source) {
                continue;
            }
            const CalcEng::Cell *from = sheet.cellAt(source);
            if (from == nullptr) {
                sheet.clearCell(target);
                m_workbook.setCellValue(m_activeSheet, target, CalcEng::Value());
                continue;
            }
            if (from->ast) {
                // A filled formula means the same thing relative to its own
                // row/column, which is what shiftNode expresses.
                const CalcEng::NodePtr shifted = CalcEng::shiftNode(
                    from->ast, target.row - source.row, target.col - source.col);
                m_workbook.installFormula(m_activeSheet, target, shifted,
                                          CalcEng::nodeText(shifted), from->dynamicArray);
            } else {
                m_workbook.setCellValue(m_activeSheet, target, from->value);
            }
            sheet.setStyleIdAt(target, sheet.styleIdAt(source));
        }
    }
    pushUndo(before, down ? tr("Fill down") : tr("Fill right"));
    setDirty(true);
    emit contentChanged();
}

void SpreadsheetDocument::removeSheet(qint32 index)
{
    if (refuseStructureChange()) {
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    if (!CalcEng::removeSheet(m_workbook, index)) {
        return;
    }
    m_activeSheet = qBound(qint32(0), m_activeSheet >= index ? m_activeSheet - 1 : m_activeSheet,
                           m_workbook.sheetCount() - 1);
    pushUndo(before, tr("Delete sheet"));
    setDirty(true);
    emit contentChanged();
    emit structureChanged();
}

void SpreadsheetDocument::renameSheet(qint32 index, const QString &name)
{
    if (refuseStructureChange()) {
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    if (!CalcEng::renameSheet(m_workbook, index, name)) {
        return;
    }
    pushUndo(before, tr("Rename sheet"));
    setDirty(true);
    emit contentChanged();
    emit structureChanged();
}

void SpreadsheetDocument::moveSheet(qint32 from, qint32 to)
{
    if (refuseStructureChange()) {
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    if (!CalcEng::moveSheet(m_workbook, from, to)) {
        return;
    }
    m_activeSheet = to;
    pushUndo(before, tr("Move sheet"));
    setDirty(true);
    emit contentChanged();
    emit structureChanged();
}

void SpreadsheetDocument::duplicateSheet(qint32 index, const QString &name)
{
    if (refuseStructureChange()) {
        return;
    }
    if (index < 0 || index >= m_workbook.sheetCount() || name.isEmpty()
        || m_workbook.sheetIndex(name) >= 0) {
        return;
    }
    if (m_workbook.sheetAt(index).isChartSheet()) {
        // Its chart lives in the parts carried from the file it came from;
        // a copy would have nothing to save (M69).
        emit editRefused(tr("A chart sheet cannot be duplicated in QindaCalc."));
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    const CalcEng::Sheet &source = m_workbook.sheetAt(index);
    CalcEng::Sheet &copy = m_workbook.insertSheet(index + 1, name);
    copy.replaceContents(source.cells(), source.styleIds(), source.columnWidths(),
                         source.rowHeights(), source.merges());
    // Its tables come along under names of their own (Excel's Table1 copy
    // is Table2), so the copied [@Col] references find their table (M69).
    QList<CalcEng::TableSpec> tables;
    for (CalcEng::TableSpec table : m_workbook.sheetAt(index).tables()) {
        table.name = CalcEng::nextTableName(m_workbook);
        table.sourcePart.clear();
        tables.append(table);
        copy.setTables(tables);
    }
    copy.setCondFormats(source.condFormats());
    copy.setValidations(source.validations());
    copy.setAutoFilter(source.autoFilter());
    copy.setComments(source.comments());
    // Its PivotTables come along on the same caches under names of their own
    // (Excel's Move or Copy); a read-only one stays with its sheet (M74).
    for (CalcEng::PivotTable pivot : source.pivots()) {
        if (pivot.modelled) {
            pivot.name = CalcEng::nextPivotName(m_workbook);
            pivot.sourcePart.clear();
            pivot.readFingerprint.clear();
            pivot.customFormats = false; // Excel's formats stay with the source pivot
            copy.mutablePivots().append(pivot);
        }
    }
    // A copy keeps its links, outline and protection (Excel's Move or Copy)
    // but is no source sheet: nothing is carried for it (M66, D-075).
    copy.setHyperlinks(source.hyperlinks());
    copy.setOutline(source.outline());
    copy.setProtection(source.protection());
    // M78 (D-087): its Page Setup, print area and titles, scenarios, data
    // tables and the rules kept as written come along too.
    copy.setPageSetup(source.pageSetup());
    copy.setPrintArea(source.printArea());
    copy.setPrintTitles(source.printTitleRows(), source.printTitleCols());
    copy.setScenarios(source.scenarios());
    copy.setDataTables(source.dataTables());
    copy.setKeptRules(source.keptRules());
    // M82 (D-091): and its part of each Custom View, as Excel copies it.
    copy.setCustomViews(source.customViews());
    // The copied formulas' unqualified references now mean the NEW sheet,
    // which is what a duplicate should do; the index still has to be rebuilt
    // because a sheet was inserted in the middle of the list.
    m_workbook.reindexAll();
    m_activeSheet = index + 1;
    pushUndo(before, tr("Duplicate sheet"));
    setDirty(true);
    emit contentChanged();
    emit structureChanged();
}

} // namespace QindaOffice::Apps::QindaCalc
