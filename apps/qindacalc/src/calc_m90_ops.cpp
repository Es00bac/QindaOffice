// SPDX-License-Identifier: GPL-3.0-or-later
// The document's M90 edits (spreadsheet_document.h, D-098): sparkline
// groups, Table Design's Rename and Resize Table — each one undo step.

#include "spreadsheet_document.h"

#include "calc_undo.h"

#include "sheet.h"
#include "table_design.h"

namespace QindaOffice::Apps::QindaCalc {

using CalcEng::CellAddr;

void SpreadsheetDocument::setSparklineGroups(qint32 sheet, const QList<CalcEng::SparklineGroup> &groups,
                                             const QString &undoText)
{
    if (sheet < 0 || sheet >= m_workbook.sheetCount() || m_workbook.sheetAt(sheet).sparklineGroups() == groups) {
        return;
    }
    if (refuseOnProtectedSheet(m_workbook.sheetAt(sheet).protection().objects, tr("change sparklines"))) return;
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    m_workbook.sheetAt(sheet).setSparklineGroups(groups);
    pushUndo(before, undoText);
    setDirty(true);
    emit contentChanged();
}

bool SpreadsheetDocument::renameTable(const CellAddr &addr, const QString &name)
{
    if (refuseStructureChange()) return false;
    const CalcEng::Sheet &sheet = m_workbook.sheetAt(m_activeSheet);
    for (qsizetype i = 0; i < sheet.tables().size(); ++i) {
        if (!sheet.tables().at(i).range.contains(addr)) {
            continue;
        }
        if (sheet.tables().at(i).name == name) {
            return true;
        }
        const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
        QString error;
        if (!CalcEng::renameTable(m_workbook, m_activeSheet, i, name, &error)) {
            emit editRefused(error);
            return false;
        }
        pushUndo(before, tr("Rename Table"));
        setDirty(true);
        emit contentChanged();
        return true;
    }
    return false;
}

bool SpreadsheetDocument::resizeTable(const CellAddr &addr, const CalcEng::CellRange &range)
{
    if (refuseStructureChange()) {
        return false;
    }
    const CalcEng::Sheet &sheet = m_workbook.sheetAt(m_activeSheet);
    for (qsizetype i = 0; i < sheet.tables().size(); ++i) {
        if (!sheet.tables().at(i).range.contains(addr)) {
            continue;
        }
        const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
        QString error;
        if (!CalcEng::resizeTable(m_workbook, m_activeSheet, i, range, &error)) {
            emit editRefused(error); // a refusal changes nothing (table_design.h)
            return false;
        }
        pushUndo(before, tr("Resize Table"));
        setDirty(true);
        emit contentChanged();
        return true;
    }
    return false;
}

} // namespace QindaOffice::Apps::QindaCalc
