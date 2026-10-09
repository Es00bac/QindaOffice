// SPDX-License-Identifier: GPL-3.0-or-later
// SpreadsheetDocument's PivotTable edits (M74, D-083): Insert ▸
// PivotTable, every change to a pivot's definition, refresh, clear, move,
// a new source, grouping, Show Details — one undo step each — and the
// guards that refuse a write into a pivot's output or a structural edit
// through one, with Excel's messages. The work is calc_engine's
// (pivot_edit.h); this layer owns undo, protection and the notifications.

#include "spreadsheet_document.h"

#include "calc_undo.h"

#include "pivot_cache.h"
#include "sheet.h"
#include "structure.h"
#include "table_edit.h"

namespace QindaOffice::Apps::QindaCalc {

using CalcEng::CellAddr;
using CalcEng::CellRange;
using CalcEng::PivotRef;

namespace {

bool overlaps(const CellRange &a, const CellRange &b)
{
    return a.isValid() && b.isValid() && a.topLeft.row <= b.bottomRight.row && a.bottomRight.row >= b.topLeft.row
        && a.topLeft.col <= b.bottomRight.col && a.bottomRight.col >= b.topLeft.col;
}

} // namespace

qint32 SpreadsheetDocument::insertSheetBeforeActive()
{
    QString name;
    for (int n = 1; name.isEmpty() || m_workbook.sheetIndex(name) >= 0; ++n) {
        name = tr("Sheet%1").arg(n);
    }
    m_workbook.addSheet(name);
    const qint32 at = qBound(qint32(0), m_activeSheet, m_workbook.sheetCount() - 1);
    CalcEng::moveSheet(m_workbook, m_workbook.sheetCount() - 1, at); // Excel puts it before the active sheet
    setActiveSheet(at);
    return at;
}

bool SpreadsheetDocument::finishPivotEdit(const WorkbookSnapshot &before, bool done, const QString &undoText,
                                          const QString &error)
{
    if (!done) {
        emit editRefused(error.isEmpty() ? tr("The PivotTable could not be changed.") : error);
        return false;
    }
    pushUndo(before, undoText);
    setDirty(true);
    emit contentChanged();
    return true;
}

PivotRef SpreadsheetDocument::insertPivotTable(const CalcEng::PivotSource &source, bool newSheet, qint32 sheet,
                                               const CellAddr &at)
{
    QString error;
    if (!CalcEng::readPivotSource(m_workbook, source, &error).has_value()) {
        emit editRefused(error);
        return {};
    }
    if (newSheet && refuseStructureChange()) {
        return {};
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    const qint32 target = newSheet ? insertSheetBeforeActive() : sheet;
    const CellAddr place = newSheet ? CellAddr{2, 0} : at; // A3, where Excel puts a new sheet's pivot
    const PivotRef ref = CalcEng::createPivotTable(m_workbook, source, target, place, &error);
    if (!ref.isValid()) {
        restoreSnapshot(&m_workbook, before);
        m_activeSheet = before.activeSheet;
        emit editRefused(error);
        return {};
    }
    setActiveSheet(target);
    finishPivotEdit(before, true, tr("Insert PivotTable"), {});
    if (newSheet) {
        emit structureChanged();
    }
    return ref;
}

bool SpreadsheetDocument::changePivot(const PivotRef &ref, const PivotChange &change, const QString &undoText,
                                      bool replace)
{
    const CalcEng::PivotTable *pivot = CalcEng::pivotOf(m_workbook, ref);
    const CalcEng::PivotCache *cache = pivot != nullptr ? CalcEng::pivotCacheOf(m_workbook, pivot->cacheId) : nullptr;
    if (cache == nullptr) {
        return false;
    }
    if (!pivot->modelled) {
        emit editRefused(tr("PivotTable “%1” %2; QindaCalc keeps it as it is, read-only.").arg(pivot->name, pivot->note));
        return false;
    }
    if (refuseOnProtectedSheet(m_workbook.sheetAt(ref.sheet).protection().pivotTables, tr("change PivotTables"))) {
        return false;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    const CalcEng::PivotTable original = *pivot;
    CalcEng::PivotTable changed = original;
    change(changed, *cache);
    CalcEng::pivotNormalize(changed, *cache);
    m_workbook.sheetAt(ref.sheet).mutablePivots()[ref.index] = changed;
    QString error;
    if (!CalcEng::renderPivotTable(m_workbook, ref, &error, replace)) {
        m_workbook.sheetAt(ref.sheet).mutablePivots()[ref.index] = original;
        emit editRefused(error);
        return false;
    }
    return finishPivotEdit(before, true, undoText, {});
}

bool SpreadsheetDocument::refreshPivot(const PivotRef &ref)
{
    const CalcEng::PivotTable *pivot = CalcEng::pivotOf(m_workbook, ref);
    if (pivot == nullptr) {
        return false;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    QString error;
    const bool done = CalcEng::refreshPivotCache(m_workbook, pivot->cacheId, &error);
    return finishPivotEdit(before, done, tr("Refresh PivotTable"), error);
}

bool SpreadsheetDocument::refreshAllPivots()
{
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    QStringList errors;
    const bool all = CalcEng::refreshAllPivots(m_workbook, &errors);
    pushUndo(before, tr("Refresh All"));
    setDirty(true);
    emit contentChanged();
    if (!all) {
        emit editRefused(errors.join(QLatin1Char(' ')));
    }
    return all;
}

bool SpreadsheetDocument::clearPivot(const PivotRef &ref)
{
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    QString error;
    const bool done = CalcEng::clearPivotTable(m_workbook, ref, &error);
    return finishPivotEdit(before, done, tr("Clear PivotTable"), error);
}

bool SpreadsheetDocument::movePivot(const PivotRef &ref, qint32 sheet, const CellAddr &at)
{
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    QString error;
    const bool done = CalcEng::movePivotTable(m_workbook, ref, sheet, at, &error);
    if (done && sheet != m_activeSheet) {
        setActiveSheet(sheet);
    }
    return finishPivotEdit(before, done, tr("Move PivotTable"), error);
}

bool SpreadsheetDocument::changePivotSource(const PivotRef &ref, const CalcEng::PivotSource &source)
{
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    QString error;
    const bool done = CalcEng::changePivotSource(m_workbook, ref, source, &error);
    return finishPivotEdit(before, done, tr("Change Data Source"), error);
}

bool SpreadsheetDocument::groupPivot(const PivotRef &ref, qint32 field, const QList<CalcEng::PivotGroupBy> &parts,
                                     double start, double end, double interval)
{
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    QString error;
    const bool done = CalcEng::pivotGroup(m_workbook, ref, field, parts, start, end, interval, &error);
    return finishPivotEdit(before, done, tr("Group"), error);
}

bool SpreadsheetDocument::groupPivotItems(const PivotRef &ref, qint32 field, const QList<qint32> &items)
{
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    QString error;
    const bool done = CalcEng::pivotGroupSelection(m_workbook, ref, field, items, &error);
    return finishPivotEdit(before, done, tr("Group Selection"), error);
}

bool SpreadsheetDocument::ungroupPivot(const PivotRef &ref, qint32 field)
{
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    QString error;
    const bool done = CalcEng::pivotUngroup(m_workbook, ref, field, &error);
    return finishPivotEdit(before, done, tr("Ungroup"), error);
}

bool SpreadsheetDocument::showPivotDetails(const PivotRef &ref, const CellAddr &addr)
{
    const std::optional<CalcEng::PivotDetails> details = CalcEng::pivotDetails(m_workbook, ref, addr);
    if (!details.has_value() || details->rows.isEmpty()) {
        emit editRefused(tr("Show Details needs a value of the PivotTable that some records add up to."));
        return false;
    }
    if (refuseStructureChange()) {
        return false;
    }
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    const qint32 sheet = insertSheetBeforeActive();
    m_workbook.beginBatch();
    for (qsizetype c = 0; c < details->headers.size(); ++c) {
        m_workbook.setCellValue(sheet, {0, qint32(c)}, details->headers.at(c));
    }
    QHash<QString, qint32> formats;
    for (qsizetype r = 0; r < details->rows.size(); ++r) {
        const QList<CalcEng::Value> &row = details->rows.at(r);
        for (qsizetype c = 0; c < row.size(); ++c) {
            const CellAddr at{qint32(r) + 1, qint32(c)};
            m_workbook.setCellValue(sheet, at, row.at(c));
            const QString format = details->formats.value(c);
            if (!format.isEmpty() && CalcEng::isNumber(row.at(c))) {
                if (!formats.contains(format)) {
                    CalcEng::CellStyle style;
                    style.numberFormat = format;
                    formats.insert(format, m_workbook.addStyle(style));
                }
                m_workbook.sheetAt(sheet).setStyleIdAt(at, formats.value(format));
            }
        }
    }
    m_workbook.endBatch();
    // Excel shows the records as a table of their own.
    QString error;
    static_cast<void>(CalcEng::createTable(
        m_workbook, sheet,
        {{0, 0}, {qint32(details->rows.size()), qint32(qMax(qsizetype(1), details->headers.size())) - 1}}, &error));
    finishPivotEdit(before, true, tr("Show Details"), {});
    emit structureChanged();
    return true;
}

bool SpreadsheetDocument::refusePivotEdit(const CellRange &range)
{
    for (const CalcEng::PivotTable &pivot : m_workbook.sheetAt(m_activeSheet).pivots()) {
        if (overlaps(pivot.extent, range)) {
            emit editRefused(tr("We can't make this change for the selected cells because it will affect a PivotTable. "
                                "Use the field list to change the report. If you are trying to insert or delete cells, "
                                "move the PivotTable and try again."));
            return true;
        }
    }
    return false;
}

bool SpreadsheetDocument::refusePivotSplit(bool rows, qint32 at, qint32 count, bool insert)
{
    if (!CalcEng::pivotSplitBy(m_workbook, m_activeSheet, rows, at, count, insert)) {
        return false;
    }
    emit editRefused(tr("Cannot change this part of a PivotTable report: the rows or columns would cut through it. "
                        "Move the PivotTable (PivotTable Analyze ▸ Move PivotTable) and try again."));
    return true;
}

} // namespace QindaOffice::Apps::QindaCalc
