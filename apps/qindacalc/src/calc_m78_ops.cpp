// SPDX-License-Identifier: GPL-3.0-or-later
// SpreadsheetDocument's M78 edits (D-087): Page Setup with the print area
// and titles, scenarios, data tables, Goal Seek's result, and the data
// tools — Text to Columns, Remove Duplicates, a block of values for
// Consolidate. One undo step each, through the workbook snapshot
// (calc_undo.h), after the same refusals every cell edit asks
// (refuseCellEdit: chart sheets, arrays, PivotTables, data tables, locked
// cells).

#include "spreadsheet_document.h"

#include "calc_undo.h"

#include "sheet.h"
#include "structure.h"

namespace QindaOffice::Apps::QindaCalc {

using CalcEng::CellAddr;
using CalcEng::CellRange;

void SpreadsheetDocument::setPrintSettings(qint32 sheetIndex, const CalcEng::Sheet::PageSetup &setup,
                                           const std::optional<CellRange> &area,
                                           const std::optional<CellRange> &titleRows,
                                           const std::optional<CellRange> &titleCols, const QString &undoText)
{
    if (sheetIndex < 0 || sheetIndex >= m_workbook.sheetCount()) {
        return;
    }
    CalcEng::Sheet &sheet = m_workbook.sheetAt(sheetIndex);
    if (sheet.pageSetup() == setup && sheet.printArea() == area && sheet.printTitleRows() == titleRows
        && sheet.printTitleCols() == titleCols) {
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    // AGENT-GUARD: the print area first — setPrintArea clears the page
    // setup's further areas (a multi-area print area), which `setup` holds.
    sheet.setPrintArea(area);
    sheet.setPrintTitles(titleRows, titleCols);
    sheet.setPageSetup(setup);
    pushUndo(before, undoText);
    setDirty(true);
    emit contentChanged();
}

void SpreadsheetDocument::setScenarios(const CalcEng::ScenarioSet &set, const QString &undoText)
{
    CalcEng::Sheet &sheet = m_workbook.sheetAt(m_activeSheet);
    if (sheet.scenarios() == set) {
        return;
    }
    if (refuseOnProtectedSheet(sheet.protection().scenarios, tr("Edit scenarios"))) {
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    sheet.setScenarios(set);
    pushUndo(before, undoText);
    setDirty(true);
    emit contentChanged();
}

bool SpreadsheetDocument::showScenario(qint32 index)
{
    const CalcEng::ScenarioSet &set = m_workbook.sheetAt(m_activeSheet).scenarios();
    if (index < 0 || index >= set.scenarios.size()) {
        return false;
    }
    for (const CellAddr &cell : set.scenarios.at(index).cells) {
        if (refuseCellEdit({cell, cell})) {
            return false;
        }
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    QString error;
    if (!CalcEng::showScenario(m_workbook, m_activeSheet, index, &error)) {
        emit editRefused(error.isEmpty() ? tr("The scenario could not be shown.") : error);
        return false;
    }
    pushUndo(before, tr("Show Scenario"));
    setDirty(true);
    emit contentChanged();
    return true;
}

qint32 SpreadsheetDocument::addScenarioSummary(const QList<CellRange> &results)
{
    if (refuseStructureChange()) {
        return -1;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    const qint32 source = m_activeSheet;
    const CalcEng::ScenarioSummary summary = CalcEng::scenarioSummary(m_workbook, source, results);
    CalcEng::ScenarioSet set = m_workbook.sheetAt(source).scenarios();
    set.resultCells = results;
    m_workbook.sheetAt(source).setScenarios(set);
    // Excel's place and name: before the sheet, "Scenario Summary" (2, 3 …).
    QString name = tr("Scenario Summary");
    for (int n = 2; m_workbook.sheetIndex(name) >= 0; ++n) {
        name = tr("Scenario Summary %1").arg(n);
    }
    m_workbook.addSheet(name);
    const qint32 at = source;
    CalcEng::moveSheet(m_workbook, m_workbook.sheetCount() - 1, at);
    CalcEng::writeScenarioSummary(m_workbook, at, source + 1, summary);
    setActiveSheet(at);
    pushUndo(before, tr("Scenario Summary"));
    setDirty(true);
    emit contentChanged();
    emit structureChanged();
    return at;
}

bool SpreadsheetDocument::addDataTable(const CalcEng::DataTableSpec &spec)
{
    if (!spec.ref.isValid() || refuseCellEdit(spec.ref)) {
        return false;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    CalcEng::Sheet &sheet = m_workbook.sheetAt(m_activeSheet);
    QList<CalcEng::DataTableSpec> tables = sheet.dataTables();
    tables.append(spec);
    sheet.setDataTables(tables);
    m_workbook.recomputeDataTables();
    pushUndo(before, tr("Data Table"));
    setDirty(true);
    emit contentChanged();
    return true;
}

std::shared_ptr<WorkbookSnapshot> SpreadsheetDocument::snapshotNow() const
{
    return std::make_shared<WorkbookSnapshot>(takeSnapshot(m_workbook, m_activeSheet));
}

void SpreadsheetDocument::commitSnapshotStep(const WorkbookSnapshot &before, const QString &undoText, bool sheetsChanged)
{
    pushUndo(before, undoText);
    setDirty(true);
    emit contentChanged();
    if (sheetsChanged) {
        emit structureChanged();
    }
}

void SpreadsheetDocument::restoreWithoutUndo(const WorkbookSnapshot &before)
{
    applySnapshot(before);
}

bool SpreadsheetDocument::applyTextColumns(const QList<CalcEng::TextColumnCell> &cells, const CellRange &block)
{
    if (cells.isEmpty() || refuseCellEdit(block)) {
        return false;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    m_workbook.beginBatch();
    for (const CalcEng::TextColumnCell &cell : cells) {
        if (cell.input.isEmpty()) {
            m_workbook.setCellValue(m_activeSheet, cell.addr, CalcEng::Value());
        } else {
            m_workbook.setCellInput(m_activeSheet, cell.addr, cell.input);
        }
    }
    m_workbook.endBatch();
    // A converted date reads as a date, a percentage as one (Excel's formats).
    for (const CalcEng::TextColumnCell &cell : cells) {
        const QString code = !cell.numberFormat.isEmpty() ? cell.numberFormat
                                                          : (cell.dateFormat ? QStringLiteral("m/d/yyyy") : QString());
        if (!code.isEmpty()) {
            StylePatch format;
            format.numberFormat = code;
            restyleCells({cell.addr, cell.addr}, format);
        }
    }
    pushUndo(before, tr("Text to Columns"));
    setDirty(true);
    emit contentChanged();
    return true;
}

std::optional<CalcEng::RemoveDuplicatesResult>
SpreadsheetDocument::removeDuplicateRows(const CellRange &range, const QList<qint32> &columns, bool header)
{
    if (!range.isValid() || columns.isEmpty() || refuseCellEdit(range)) {
        return std::nullopt;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    const CalcEng::RemoveDuplicatesResult result =
        CalcEng::removeDuplicates(m_workbook, m_activeSheet, range, columns, header);
    if (!result.error.isEmpty()) {
        emit editRefused(result.error);
        return std::nullopt;
    }
    if (result.removed > 0) {
        pushUndo(before, tr("Remove Duplicates"));
        setDirty(true);
        emit contentChanged();
    }
    return result;
}

bool SpreadsheetDocument::writeValues(const CellAddr &at, const QList<QList<CalcEng::Value>> &grid,
                                      const QString &undoText)
{
    qint32 width = 0;
    for (const QList<CalcEng::Value> &row : grid) {
        width = qMax(width, qint32(row.size()));
    }
    if (grid.isEmpty() || width == 0) {
        return false;
    }
    const CellRange block{at, {at.row + qint32(grid.size()) - 1, at.col + width - 1}};
    if (refuseCellEdit(block)) {
        return false;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    m_workbook.beginBatch();
    for (qint32 r = 0; r < grid.size(); ++r) {
        for (qint32 c = 0; c < grid.at(r).size(); ++c) {
            m_workbook.setCellValue(m_activeSheet, {at.row + r, at.col + c}, grid.at(r).at(c));
        }
    }
    m_workbook.endBatch();
    pushUndo(before, undoText);
    setDirty(true);
    emit contentChanged();
    return true;
}

bool SpreadsheetDocument::undoIfTop(const QString &undoText)
{
    if (!m_undoStack.canUndo() || m_undoStack.undoText() != undoText) {
        return false;
    }
    m_undoStack.undo();
    return true;
}

} // namespace QindaOffice::Apps::QindaCalc
