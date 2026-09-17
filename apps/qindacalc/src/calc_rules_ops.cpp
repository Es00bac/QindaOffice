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

void SpreadsheetDocument::setComment(const CalcEng::CellAddr &addr,
                                     const CalcEng::CellComment &comment)
{
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
