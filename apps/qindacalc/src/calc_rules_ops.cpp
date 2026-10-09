// SPDX-License-Identifier: GPL-3.0-or-later
// SpreadsheetDocument's rule edits: conditional formatting, data
// validation, the autofilter and cell comments. Split from
// calc_structure_ops.cpp to keep both files inside the line budget.
//
// Same shape as every other document mutation: snapshot, mutate, push ONE
// undo command, mark dirty, notify. The rules themselves live in
// calc_engine (sheet_rules.h) so the grid, the printer and the recovery
// payload all evaluate them identically.

#include "spreadsheet_document.h"

#include "calc_undo.h"

#include "sheet.h"

namespace QindaOffice::Apps::QindaCalc {

void SpreadsheetDocument::setCondFormats(const QList<CalcEng::CondFormatRule> &rules)
{
    if (refuseOnProtectedSheet(true, tr("change conditional formatting"))) {
        return;
    }
    CalcEng::Sheet &sheet = m_workbook.sheetAt(m_activeSheet);
    if (sheet.condFormats() == rules) {
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    sheet.setCondFormats(rules);
    pushUndo(before, tr("Conditional formatting"));
    setDirty(true);
    emit contentChanged();
}

void SpreadsheetDocument::setValidations(const QList<CalcEng::ValidationRule> &rules)
{
    if (refuseOnProtectedSheet(true, tr("change data validation"))) {
        return;
    }
    CalcEng::Sheet &sheet = m_workbook.sheetAt(m_activeSheet);
    if (sheet.validations() == rules) {
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    sheet.setValidations(rules);
    pushUndo(before, tr("Data validation"));
    setDirty(true);
    emit contentChanged();
}

void SpreadsheetDocument::setAutoFilter(const CalcEng::AutoFilter &filter)
{
    if (refuseOnProtectedSheet(m_workbook.sheetAt(m_activeSheet).protection().autoFilter, tr("filter"))) {
        return;
    }
    CalcEng::Sheet &sheet = m_workbook.sheetAt(m_activeSheet);
    if (sheet.autoFilter() == filter) {
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    sheet.setAutoFilter(filter);
    pushUndo(before, tr("Filter"));
    setDirty(true);
    emit contentChanged();
}

void SpreadsheetDocument::setFreeze(qint32 rows, qint32 cols)
{
    CalcEng::Sheet &sheet = m_workbook.sheetAt(m_activeSheet);
    if (sheet.freezeRows() == rows && sheet.freezeCols() == cols) {
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    sheet.setFreeze(rows, cols);
    pushUndo(before, tr("Freeze panes"));
    setDirty(true);
    emit contentChanged();
}

void SpreadsheetDocument::setSheetHidden(qint32 index, bool hidden)
{
    if (refuseStructureChange()) {
        return;
    }
    if (index < 0 || index >= m_workbook.sheetCount()) {
        return;
    }
    CalcEng::Sheet &sheet = m_workbook.sheetAt(index);
    if (sheet.isHidden() == hidden) {
        return;
    }
    // Hiding the last visible sheet would leave a window with no grid.
    if (hidden) {
        int visible = 0;
        for (qint32 s = 0; s < m_workbook.sheetCount(); ++s) {
            visible += m_workbook.sheetAt(s).isHidden() ? 0 : 1;
        }
        if (visible <= 1) {
            return;
        }
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    sheet.setHidden(hidden);
    // The active sheet must stay a visible one.
    if (hidden && m_activeSheet == index) {
        for (qint32 s = 0; s < m_workbook.sheetCount(); ++s) {
            if (!m_workbook.sheetAt(s).isHidden()) {
                setActiveSheet(s);
                break;
            }
        }
    }
    pushUndo(before, hidden ? tr("Hide sheet") : tr("Unhide sheet"));
    setDirty(true);
    emit contentChanged();
    emit structureChanged();
}

void SpreadsheetDocument::setComment(const CalcEng::CellAddr &addr,
                                     const CalcEng::CellComment &comment)
{
    if (refuseOnProtectedSheet(m_workbook.sheetAt(m_activeSheet).protection().objects, tr("edit notes"))) {
        return;
    }
    CalcEng::Sheet &sheet = m_workbook.sheetAt(m_activeSheet);
    const CalcEng::CellComment *existing = sheet.commentAt(addr);
    if ((existing == nullptr && comment.text.isEmpty())
        || (existing != nullptr && *existing == comment)) {
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    sheet.setCommentAt(addr, comment);
    pushUndo(before, comment.text.isEmpty() ? tr("Delete comment") : tr("Comment"));
    setDirty(true);
    emit contentChanged();
}

} // namespace QindaOffice::Apps::QindaCalc
