// SPDX-License-Identifier: GPL-3.0-or-later
#include "calc_undo.h"

#include "eval.h"
#include "spreadsheet_document.h"

namespace QindaOffice::Apps::QindaCalc {

WorkbookSnapshot takeSnapshot(const CalcEng::Workbook &workbook, qint32 activeSheet)
{
    WorkbookSnapshot snap;
    snap.activeSheet = activeSheet;
    for (qint32 s = 0; s < workbook.sheetCount(); ++s) {
        const CalcEng::Sheet &sheet = workbook.sheetAt(s);
        SheetSnapshot ss;
        ss.name = sheet.name();
        ss.cells = sheet.cells();
        for (const qint32 col : sheet.sizedColumns()) {
            ss.colWidths.insert(col, sheet.columnWidth(col));
        }
        for (const qint32 row : sheet.sizedRows()) {
            ss.rowHeights.insert(row, sheet.rowHeight(row));
        }
        // Style ids ride along via the sheet's styleIdAt map; iterate the
        // cells and read each (sparse, so only set ones are copied).
        for (auto it = sheet.cells().constBegin(); it != sheet.cells().constEnd(); ++it) {
            const CalcEng::CellAddr addr{qint32(it.key() & 0xffffffffu),
                                         qint32(it.key() >> 32)};
            const qint32 id = sheet.styleIdAt(addr);
            if (id != 0) {
                ss.styleIds.insert(it.key(), id);
            }
        }
        ss.merges = sheet.merges();
        snap.sheets.append(ss);
    }
    // The style table is indexed by position; copy through the public
    // accessor. Id 0 is the ctor-seeded reserved default: it must NOT be
    // snapshotted, or every restore would re-append it after the fresh
    // book's own seed and shift every real style id by one (cell style
    // ids would point at the wrong entries).
    for (qint32 id = 1; id < workbook.styleCount(); ++id) {
        const CalcEng::CellStyle *style = workbook.style(id);
        if (style != nullptr) {
            snap.styles.append(*style);
        }
    }
    snap.names = workbook.namedRanges();
    snap.charts = workbook.charts;
    return snap;
}

void restoreSnapshot(CalcEng::Workbook *workbook, const WorkbookSnapshot &snapshot)
{
    CalcEng::Workbook fresh;
    QSet<CalcEng::CellKey> allKeys;
    for (const SheetSnapshot &ss : snapshot.sheets) {
        CalcEng::Sheet &sheet = fresh.addSheet(ss.name);
        for (auto it = ss.cells.constBegin(); it != ss.cells.constEnd(); ++it) {
            const quint64 key = it.key();
            const CalcEng::CellAddr addr{qint32(key & 0xffffffffu), qint32(key >> 32)};
            sheet.ensureCell(addr) = it.value();
            allKeys.insert(CalcEng::CellKey(fresh.sheetCount() - 1, addr.row, addr.col));
        }
        for (auto it = ss.colWidths.constBegin(); it != ss.colWidths.constEnd(); ++it) {
            sheet.setColumnWidth(it.key(), it.value());
        }
        for (auto it = ss.rowHeights.constBegin(); it != ss.rowHeights.constEnd(); ++it) {
            sheet.setRowHeight(it.key(), it.value());
        }
        for (auto it = ss.styleIds.constBegin(); it != ss.styleIds.constEnd(); ++it) {
            const quint64 key = it.key();
            sheet.setStyleIdAt({qint32(key & 0xffffffffu), qint32(key >> 32)}, it.value());
        }
        for (const CalcEng::CellRange &merge : ss.merges) {
            sheet.addMerge(merge);
        }
    }
    for (const CalcEng::CellStyle &style : snapshot.styles) {
        fresh.addStyle(style);
    }
    // Named ranges: defineName resolves through the host; the fresh book
    // already holds its sheets, so definitions land correctly.
    for (auto it = snapshot.names.constBegin(); it != snapshot.names.constEnd(); ++it) {
        fresh.defineName(it.key(), it.value());
    }
    fresh.charts = snapshot.charts;

    // Re-register the dependency index from the restored ASTs and recalc
    // everything once (restores volatile and cycle-detected state).
    fresh.dependencies().clear();
    for (qint32 s = 0; s < fresh.sheetCount(); ++s) {
        CalcEng::Sheet &sheet = fresh.sheetAt(s);
        for (auto it = sheet.cells().begin(); it != sheet.cells().end(); ++it) {
            const quint64 key = it.key();
            const CalcEng::CellAddr addr{qint32(key & 0xffffffffu), qint32(key >> 32)};
            if (it.value().ast == nullptr) {
                continue;
            }
            const CalcEng::CellKey cellKey(s, addr.row, addr.col);
            CalcEng::EvalContext ctx{&fresh, s, addr};
            fresh.dependencies().updateCell(cellKey,
                                            CalcEng::collectTargets(it.value().ast, ctx));
            if (it.value().isVolatile) {
                fresh.dependencies().addVolatile(cellKey);
            }
        }
    }
    fresh.recalc(allKeys);
    *workbook = std::move(fresh);
}

WorkbookUndoCommand::WorkbookUndoCommand(SpreadsheetDocument *document,
                                         const WorkbookSnapshot &before,
                                         const WorkbookSnapshot &after, const QString &text,
                                         QUndoCommand *parent)
    : QUndoCommand(text, parent)
    , m_document(document)
    , m_before(before)
    , m_after(after)
{
}

void WorkbookUndoCommand::apply(const WorkbookSnapshot &snapshot)
{
    m_document->applySnapshot(snapshot);
}

void WorkbookUndoCommand::undo()
{
    apply(m_before);
}

void WorkbookUndoCommand::redo()
{
    apply(m_after);
}

} // namespace QindaOffice::Apps::QindaCalc
