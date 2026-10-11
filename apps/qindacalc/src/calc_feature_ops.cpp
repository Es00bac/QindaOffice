// SPDX-License-Identifier: GPL-3.0-or-later
// SpreadsheetDocument's M66 operations (D-075): hyperlinks, outlines and
// hidden lines, sheet and workbook protection, and the guards every other
// edit asks before it touches a protected sheet or a locked structure.
// Same shape as every mutation: snapshot, change, ONE undo step, dirty,
// notify. The model and its rules live in calc_engine (hyperlink.h,
// outline.h, protection.h).

#include "spreadsheet_document.h"

#include "calc_undo.h"

#include "sheet.h"

namespace QindaOffice::Apps::QindaCalc {

using CalcEng::CellAddr;
using CalcEng::CellRange;

namespace {

// Excel's Hyperlink cell style: its theme colour hlink, underlined.
constexpr CalcEng::Rgb kLinkColour{0x05, 0x63, 0xC1};

// The guard reads every cell of a range up to this size; a larger range
// counts as holding a locked cell (it is the default format's state).
constexpr qint64 kMaxGuardCells = qint64(1) << 20;

bool intersects(const CellRange &a, const CellRange &b)
{
    return a.topLeft.row <= b.bottomRight.row && b.topLeft.row <= a.bottomRight.row
        && a.topLeft.col <= b.bottomRight.col && b.topLeft.col <= a.bottomRight.col;
}

} // namespace

bool SpreadsheetDocument::refuseLockedCells(const CellRange &range)
{
    const CalcEng::Sheet &sheet = m_workbook.sheetAt(m_activeSheet);
    if (!sheet.protection().enabled || !range.isValid()) {
        return false;
    }
    bool locked = range.cellCount() > kMaxGuardCells;
    for (qint32 row = range.topLeft.row; !locked && row <= range.bottomRight.row; ++row) {
        for (qint32 col = range.topLeft.col; !locked && col <= range.bottomRight.col; ++col) {
            locked = m_workbook.isCellLocked(m_activeSheet, {row, col});
        }
    }
    if (locked) {
        emit editRefused(tr("The cell you are trying to change is on a protected sheet (“%1”). "
                            "Unprotect the sheet (Review ▸ Unprotect Sheet) to change it.")
                             .arg(sheet.name()));
    }
    return locked;
}

bool SpreadsheetDocument::refuseOnProtectedSheet(bool actionLocked, const QString &action)
{
    const CalcEng::Sheet &sheet = m_workbook.sheetAt(m_activeSheet);
    if (!sheet.protection().enabled || !actionLocked) {
        return false;
    }
    emit editRefused(tr("“%1” is protected: you cannot %2 there. Unprotect the sheet (Review ▸ Unprotect Sheet) first.")
                         .arg(sheet.name(), action));
    return true;
}

bool SpreadsheetDocument::refuseStructureChange()
{
    if (!m_workbook.protection.lockStructure) {
        return false;
    }
    emit editRefused(tr("The workbook's structure is protected: sheets cannot be added, deleted, renamed, moved, "
                        "hidden or shown. Unprotect the workbook (Review ▸ Unprotect Workbook) first."));
    return true;
}

void SpreadsheetDocument::setHyperlink(const CellRange &range, const CalcEng::Hyperlink &link)
{
    CalcEng::Sheet &sheet = m_workbook.sheetAt(m_activeSheet);
    if (!range.isValid() || (link.address.isEmpty() && link.location.isEmpty())
        || refuseOnProtectedSheet(sheet.protection().insertHyperlinks, tr("insert links"))
        || refuseLockedCells(range)) {
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    QList<CalcEng::Hyperlink> links;
    for (const CalcEng::Hyperlink &existing : sheet.hyperlinks()) {
        if (!intersects(existing.range, range)) {
            links.append(existing); // the new link replaces any it overlaps
        }
    }
    CalcEng::Hyperlink added = link;
    added.range = range;
    links.append(added);
    sheet.setHyperlinks(links);
    // The text to display: written into the cell when it is empty or holds
    // text (a number or a formula stays what it is).
    const CalcEng::Cell *cell = sheet.cellAt(range.topLeft);
    const QString text = link.display.isEmpty() ? CalcEng::hyperlinkTarget(link) : link.display;
    if ((cell == nullptr || (!cell->hasFormula() && (CalcEng::isBlank(cell->value) || CalcEng::isText(cell->value))))
        && !text.isEmpty()) {
        m_workbook.setCellValue(m_activeSheet, range.topLeft, text);
    }
    StylePatch look;
    look.underline = true;
    look.fg = kLinkColour;
    restyleCells(range, look);
    pushUndo(before, tr("Insert link"));
    setDirty(true);
    emit contentChanged();
}

void SpreadsheetDocument::removeHyperlinks(const CellRange &range)
{
    CalcEng::Sheet &sheet = m_workbook.sheetAt(m_activeSheet);
    QList<CalcEng::Hyperlink> kept;
    QList<CellRange> removed;
    for (const CalcEng::Hyperlink &link : sheet.hyperlinks()) {
        if (intersects(link.range, range)) {
            removed.append(link.range);
        } else {
            kept.append(link);
        }
    }
    if (removed.isEmpty() || refuseLockedCells(range)) {
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    sheet.setHyperlinks(kept);
    // Remove Link takes the link look with it (Excel's Remove Hyperlinks),
    // where the cells still wear exactly that look.
    for (const CellRange &gone : std::as_const(removed)) {
        for (qint32 row = gone.topLeft.row; row <= gone.bottomRight.row; ++row) {
            for (qint32 col = gone.topLeft.col; col <= gone.bottomRight.col; ++col) {
                const CalcEng::CellStyle *style = m_workbook.style(sheet.styleIdAt({row, col}));
                if (style != nullptr && style->underline && style->fg == kLinkColour) {
                    StylePatch plain;
                    plain.underline = false;
                    plain.fg = std::optional<CalcEng::Rgb>();
                    restyleCells({{row, col}, {row, col}}, plain);
                }
            }
        }
    }
    pushUndo(before, tr("Remove link"));
    setDirty(true);
    emit contentChanged();
}

void SpreadsheetDocument::groupLines(CalcEng::OutlineAxis axis, qint32 first, qint32 last, bool group)
{
    CalcEng::Sheet &sheet = m_workbook.sheetAt(m_activeSheet);
    if (refuseOnProtectedSheet(true, group ? tr("group rows or columns") : tr("ungroup rows or columns"))) {
        return;
    }
    CalcEng::SheetOutline outline = sheet.outline();
    const bool changed = group ? CalcEng::groupLines(outline, axis, first, last)
                               : CalcEng::ungroupLines(outline, axis, first, last);
    if (!changed) {
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    sheet.setOutline(outline);
    m_workbook.recalcAll(); // Visibility changes SUBTOTAL and AGGREGATE (D-098).
    pushUndo(before, group ? tr("Group") : tr("Ungroup"));
    setDirty(true);
    emit contentChanged();
}

bool SpreadsheetDocument::setDetailShown(CalcEng::OutlineAxis axis, qint32 index, bool shown)
{
    const std::optional<CalcEng::OutlineGroup> group =
        CalcEng::groupAt(m_workbook.sheetAt(m_activeSheet).outline(), axis, index);
    if (!group) {
        return false;
    }
    setGroupShown(axis, *group, shown);
    return true;
}

void SpreadsheetDocument::setGroupShown(CalcEng::OutlineAxis axis, const CalcEng::OutlineGroup &group, bool shown)
{
    CalcEng::Sheet &sheet = m_workbook.sheetAt(m_activeSheet);
    if (refuseOnProtectedSheet(true, shown ? tr("show detail") : tr("hide detail"))) {
        return;
    }
    CalcEng::SheetOutline outline = sheet.outline();
    CalcEng::setGroupCollapsed(outline, axis, group, !shown);
    if (outline == sheet.outline()) {
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    sheet.setOutline(outline);
    m_workbook.recalcAll(); // Visibility changes SUBTOTAL and AGGREGATE (D-098).
    pushUndo(before, shown ? tr("Show detail") : tr("Hide detail"));
    setDirty(true);
    emit contentChanged();
}

void SpreadsheetDocument::showOutlineLevel(CalcEng::OutlineAxis axis, int level)
{
    CalcEng::Sheet &sheet = m_workbook.sheetAt(m_activeSheet);
    CalcEng::SheetOutline outline = sheet.outline();
    CalcEng::showOutlineLevel(outline, axis, level);
    if (outline == sheet.outline() || refuseOnProtectedSheet(true, tr("change the outline"))) {
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    sheet.setOutline(outline);
    m_workbook.recalcAll(); // Visibility changes SUBTOTAL and AGGREGATE (D-098).
    pushUndo(before, tr("Show outline level %1").arg(level));
    setDirty(true);
    emit contentChanged();
}

void SpreadsheetDocument::setLinesHidden(CalcEng::OutlineAxis axis, qint32 first, qint32 last, bool hidden)
{
    CalcEng::Sheet &sheet = m_workbook.sheetAt(m_activeSheet);
    const bool rows = axis == CalcEng::OutlineAxis::Rows;
    if (refuseOnProtectedSheet(rows ? sheet.protection().formatRows : sheet.protection().formatColumns,
                               rows ? tr("hide or show rows") : tr("hide or show columns"))) {
        return;
    }
    CalcEng::SheetOutline outline = sheet.outline();
    CalcEng::setLinesHidden(outline, axis, first, last, hidden);
    if (outline == sheet.outline()) {
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    sheet.setOutline(outline);
    m_workbook.recalcAll(); // Visibility changes SUBTOTAL and AGGREGATE (D-098).
    pushUndo(before, hidden ? (rows ? tr("Hide rows") : tr("Hide columns")) : (rows ? tr("Unhide rows") : tr("Unhide columns")));
    setDirty(true);
    emit contentChanged();
}

void SpreadsheetDocument::protectSheet(const CalcEng::SheetProtection &options, const QString &password)
{
    CalcEng::Sheet &sheet = m_workbook.sheetAt(m_activeSheet);
    if (sheet.protection().enabled) {
        return;
    }
    CalcEng::SheetProtection protection = options;
    protection.enabled = true;
    CalcEng::setPassword(&protection, password);
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    sheet.setProtection(protection);
    pushUndo(before, tr("Protect sheet"));
    setDirty(true);
    emit contentChanged();
}

bool SpreadsheetDocument::unprotectSheet(const QString &password)
{
    CalcEng::Sheet &sheet = m_workbook.sheetAt(m_activeSheet);
    if (!sheet.protection().enabled) {
        return true;
    }
    if (!CalcEng::passwordMatches(sheet.protection(), password)) {
        emit editRefused(tr("The password is not correct. Passwords are case-sensitive."));
        return false;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    sheet.setProtection({});
    pushUndo(before, tr("Unprotect sheet"));
    setDirty(true);
    emit contentChanged();
    return true;
}

void SpreadsheetDocument::protectWorkbook(const QString &password)
{
    if (m_workbook.protection.lockStructure) {
        return;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    m_workbook.protection.lockStructure = true;
    CalcEng::setPassword(&m_workbook.protection, password);
    pushUndo(before, tr("Protect workbook"));
    setDirty(true);
    emit contentChanged();
}

bool SpreadsheetDocument::unprotectWorkbook(const QString &password)
{
    if (!m_workbook.protection.isProtected()) {
        return true;
    }
    if (!CalcEng::passwordMatches(m_workbook.protection, password)) {
        emit editRefused(tr("The password is not correct. Passwords are case-sensitive."));
        return false;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    CalcEng::WorkbookProtection kept;
    // A shared workbook's revision lock is not this command's to lift.
    kept.revisionsAlgorithmName = m_workbook.protection.revisionsAlgorithmName;
    kept.revisionsHashValue = m_workbook.protection.revisionsHashValue;
    kept.revisionsSaltValue = m_workbook.protection.revisionsSaltValue;
    kept.revisionsSpinCount = m_workbook.protection.revisionsSpinCount;
    kept.revisionsLegacyHash = m_workbook.protection.revisionsLegacyHash;
    kept.lockRevision = m_workbook.protection.lockRevision;
    m_workbook.protection = kept;
    pushUndo(before, tr("Unprotect workbook"));
    setDirty(true);
    emit contentChanged();
    return true;
}

} // namespace QindaOffice::Apps::QindaCalc
