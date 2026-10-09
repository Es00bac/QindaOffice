// SPDX-License-Identifier: GPL-3.0-or-later
// SpreadsheetDocument's cell edits: typed input, batches of input, clear,
// paste and restyling (split from spreadsheet_document.cpp in M66 to keep
// both files inside the line budget). Every one asks the protection guards
// first (calc_feature_ops.cpp, D-075): a locked cell of a protected sheet
// refuses the edit and the window says why.

#include "spreadsheet_document.h"

#include "calc_undo.h"

#include "sheet.h"
#include "table_edit.h"

namespace QindaOffice::Apps::QindaCalc {

namespace {

// The anchors of the legacy array formulas lying wholly in `range`.
QList<CalcEng::CellAddr> arrayAnchorsIn(const CalcEng::Sheet &sheet, const CalcEng::CellRange &range)
{
    QList<CalcEng::CellAddr> anchors;
    for (auto it = sheet.cells().constBegin(); it != sheet.cells().constEnd(); ++it) {
        const CalcEng::CellRange &block = it.value().arrayRange;
        if (it.value().isArrayAnchor() && range.contains(block.topLeft) && range.contains(block.bottomRight)) {
            anchors.append(block.topLeft);
        }
    }
    return anchors;
}

} // namespace

void SpreadsheetDocument::applyInput(const CalcEng::CellAddr &addr, const QString &input)
{
    if (refuseCellEdit({addr, addr})) {
        return;
    }
    // AGENT-GUARD: validation is enforced HERE, on the one path every UI
    // edit takes (in-cell editor, formula bar, paste of a single cell, AI
    // apply). Checking it in the view would leave the other paths open.
    if (const CalcEng::ValidationRule *rule =
            m_workbook.sheetAt(m_activeSheet).validationAt(addr);
        rule != nullptr && !CalcEng::validationAccepts(*rule, input)) {
        emit inputRejected(addr, CalcEng::validationDescription(*rule));
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    // Tables react the way Excel's do (M69): a retyped header renames its
    // column, the row below grows the table, a formula fills its column.
    CalcEng::setTableAwareInput(m_workbook, m_activeSheet, addr, input);
    pushUndo(before, tr("Set cell"));
    setDirty(true);
    emit contentChanged();
}

void SpreadsheetDocument::applyInputs(const QList<QPair<CalcEng::CellAddr, QString>> &edits,
                                     const QString &undoText)
{
    if (edits.isEmpty()) {
        return;
    }
    for (const auto &[addr, input] : edits) {
        Q_UNUSED(input);
        if (refuseCellEdit({addr, addr})) {
            return; // the whole batch, or none of it
        }
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    for (const auto &[addr, input] : edits) {
        CalcEng::setTableAwareInput(m_workbook, m_activeSheet, addr, input);
    }
    pushUndo(before, undoText);
    setDirty(true);
    emit contentChanged();
}

void SpreadsheetDocument::clearCells(const CalcEng::CellRange &range)
{
    if (!range.isValid()) {
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    // A selection holding a whole PivotTable deletes it, Excel's Delete
    // (M74, D-083); part of one is refused below.
    bool pivotsGone = false;
    const QList<CalcEng::PivotTable> pivots = m_workbook.sheetAt(m_activeSheet).pivots();
    for (qsizetype i = pivots.size() - 1; i >= 0; --i) {
        const CalcEng::CellRange &extent = pivots.at(i).extent;
        if (extent.isValid() && range.contains(extent.topLeft) && range.contains(extent.bottomRight)) {
            pivotsGone = CalcEng::removePivotTable(m_workbook, {m_activeSheet, qint32(i)}) || pivotsGone;
        }
    }
    if (refuseCellEdit(range)) {
        if (pivotsGone) {
            restoreSnapshot(&m_workbook, before);
        }
        return;
    }
    CalcEng::Sheet &sheet = m_workbook.sheetAt(m_activeSheet);
    // A whole array formula in the range goes with its block (M69).
    for (const CalcEng::CellAddr &anchor : arrayAnchorsIn(sheet, range)) {
        m_workbook.setCellValue(m_activeSheet, anchor, CalcEng::Value());
    }
    QSet<CalcEng::CellKey> cleared;
    for (qint32 row = range.topLeft.row; row <= range.bottomRight.row; ++row) {
        for (qint32 col = range.topLeft.col; col <= range.bottomRight.col; ++col) {
            if (sheet.clearCell({row, col})) {
                const CalcEng::CellKey key(m_activeSheet, row, col);
                m_workbook.dependencies().updateCell(key, {});
                cleared.insert(key);
            }
        }
    }
    if (cleared.isEmpty() && !pivotsGone) {
        return;
    }
    m_workbook.recalc(cleared);
    CalcEng::syncTableHeaders(m_workbook, m_activeSheet); // a cleared header is Column<N> (M69)
    pushUndo(before, pivotsGone ? tr("Delete PivotTable") : tr("Clear cells"));
    setDirty(true);
    emit contentChanged();
}

void SpreadsheetDocument::pasteCells(const CalcEng::CellAddr &at, const QStringList &rows)
{
    if (rows.isEmpty() || !at.isValid()) {
        return;
    }
    qint32 width = 0;
    for (const QString &line : rows) {
        width = qMax(width, qint32(line.count(QLatin1Char('\t'))) + 1);
    }
    if (refuseCellEdit({at, {at.row + qint32(rows.size()) - 1, at.col + width - 1}})) {
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    CalcEng::Sheet &sheet = m_workbook.sheetAt(m_activeSheet);
    QSet<CalcEng::CellKey> cleared;
    qint32 row = at.row;
    for (const QString &line : rows) {
        qint32 col = at.col;
        const QStringList cells = line.split(QLatin1Char('\t'));
        for (const QString &input : cells) {
            if (!input.isEmpty()) {
                // setCellInput parses formulas ('=' prefix), numbers,
                // booleans and dates, falling back to text; a paste below a
                // table grows it, as typing does (M69).
                CalcEng::setTableAwareInput(m_workbook, m_activeSheet, {row, col}, input);
            } else if (sheet.clearCell({row, col})) {
                // An empty source cell clears the target (Excel semantics):
                // paste over existing data must be idempotent with the copy.
                const CalcEng::CellKey key(m_activeSheet, row, col);
                m_workbook.dependencies().updateCell(key, {});
                cleared.insert(key);
            }
            ++col;
        }
        ++row;
    }
    if (!cleared.isEmpty()) {
        m_workbook.recalc(cleared);
        CalcEng::syncTableHeaders(m_workbook, m_activeSheet);
    }
    pushUndo(before, tr("Paste"));
    setDirty(true);
    emit contentChanged();
}

void SpreadsheetDocument::restyleRange(const CalcEng::CellRange &range,
                                       const StylePatch &patch, const QString &undoText)
{
    if (!range.isValid()) {
        return;
    }
    // Protection (M66): a cell's own lock cannot change while the sheet is
    // protected, and formatting reaches locked cells only when allowed.
    const CalcEng::SheetProtection &protection = m_workbook.sheetAt(m_activeSheet).protection();
    if (refuseOnProtectedSheet(patch.locked.has_value() || patch.hiddenFormula.has_value(), tr("lock or unlock cells"))
        || (protection.enabled && protection.formatCells && refuseLockedCells(range))) {
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    restyleCells(range, patch);
    pushUndo(before, undoText);
    setDirty(true);
    emit contentChanged();
}

void SpreadsheetDocument::restyleCells(const CalcEng::CellRange &range, const StylePatch &patch)
{
    CalcEng::Sheet &sheet = m_workbook.sheetAt(m_activeSheet);
    for (qint32 row = range.topLeft.row; row <= range.bottomRight.row; ++row) {
        for (qint32 col = range.topLeft.col; col <= range.bottomRight.col; ++col) {
            const CalcEng::CellAddr addr{row, col};
            const qint32 oldId = sheet.styleIdAt(addr);
            const CalcEng::CellStyle *old = oldId != 0 ? m_workbook.style(oldId) : nullptr;
            CalcEng::CellStyle style = old != nullptr ? *old : CalcEng::CellStyle{};
            if (patch.bold) {
                style.bold = *patch.bold;
            }
            if (patch.italic) {
                style.italic = *patch.italic;
            }
            if (patch.underline) {
                style.underline = *patch.underline;
            }
            if (patch.strikeout) {
                style.strikeout = *patch.strikeout;
            }
            if (patch.alignment) {
                style.alignment = *patch.alignment;
            }
            if (patch.vAlign) {
                style.vAlign = *patch.vAlign;
            }
            if (patch.numberFormat) {
                style.numberFormat = *patch.numberFormat;
            }
            if (patch.fontFamily) {
                style.fontFamily = *patch.fontFamily;
            }
            if (patch.fontSize) {
                style.fontSize = *patch.fontSize;
            }
            if (patch.fg) {
                style.fg = *patch.fg;
            }
            if (patch.bg) {
                style.bg = *patch.bg;
            }
            if (patch.wrap) {
                style.wrap = *patch.wrap;
            }
            if (patch.borders) {
                style.borders = *patch.borders;
            }
            if (patch.locked) {
                style.locked = *patch.locked;
            }
            if (patch.hiddenFormula) {
                style.hiddenFormula = *patch.hiddenFormula;
            }
            const qint32 id = m_workbook.addStyle(style);
            sheet.setStyleIdAt(addr, id);
        }
    }
}

} // namespace QindaOffice::Apps::QindaCalc
