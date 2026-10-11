// SPDX-License-Identifier: GPL-3.0-or-later
#include "calc_undo.h"

#include "eval.h"
#include "pivot_edit.h"
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
        // AGENT-GUARD: snapshot the sheet's FULL style map, not per-cell
        // reads over cells() — restyleRange styles EMPTY cells too, and the
        // per-cell iteration silently dropped them (undo/redo lost formatting
        // on blank cells).
        ss.styleIds = sheet.styleIds();
        ss.merges = sheet.merges();
        ss.condFormats = sheet.condFormats();
        ss.validations = sheet.validations();
        ss.filter = sheet.autoFilter();
        ss.comments = sheet.comments();
        ss.tables = sheet.tables();
        ss.freezeRows = sheet.freezeRows();
        ss.freezeCols = sheet.freezeCols();
        ss.hidden = sheet.isHidden();
        ss.cursor = sheet.cursor();
        ss.printArea = sheet.printArea();
        ss.printTitleRows = sheet.printTitleRows();
        ss.printTitleCols = sheet.printTitleCols();
        ss.pageSetup = sheet.pageSetup();
        ss.hyperlinks = sheet.hyperlinks();
        ss.outline = sheet.outline();
        ss.protection = sheet.protection();
        ss.source = sheet.source();
        ss.chartSheet = sheet.isChartSheet();
        ss.pivots = sheet.pivots();
        ss.scenarios = sheet.scenarios();
        ss.dataTables = sheet.dataTables();
        ss.keptRules = sheet.keptRules();
        ss.customViews = sheet.customViews();
        ss.sparklines = sheet.sparklineGroups();
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
    snap.definedNames = workbook.definedNames();
    snap.source = workbook.source;
    snap.charts = workbook.charts;
    snap.images = workbook.images;
    snap.protection = workbook.protection;
    snap.pivotCaches = workbook.pivotCaches;
    snap.customViews = workbook.customViews;
    return snap;
}

void restoreSnapshot(CalcEng::Workbook *workbook, const WorkbookSnapshot &snapshot)
{
    CalcEng::Workbook fresh;
    for (const SheetSnapshot &ss : snapshot.sheets) {
        CalcEng::Sheet &sheet = fresh.addSheet(ss.name);
        for (auto it = ss.cells.constBegin(); it != ss.cells.constEnd(); ++it) {
            const quint64 key = it.key();
            const CalcEng::CellAddr addr{qint32(key & 0xffffffffu), qint32(key >> 32)};
            sheet.ensureCell(addr) = it.value();
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
        sheet.setCondFormats(ss.condFormats);
        sheet.setValidations(ss.validations);
        sheet.setAutoFilter(ss.filter);
        sheet.setComments(ss.comments);
        sheet.setTables(ss.tables);
        sheet.setFreeze(ss.freezeRows, ss.freezeCols);
        sheet.setHidden(ss.hidden);
        sheet.setCursor(ss.cursor);
        sheet.setPrintArea(ss.printArea);
        sheet.setPrintTitles(ss.printTitleRows, ss.printTitleCols);
        sheet.setPageSetup(ss.pageSetup);
        sheet.setHyperlinks(ss.hyperlinks);
        sheet.setOutline(ss.outline);
        sheet.setProtection(ss.protection);
        sheet.setSource(ss.source);
        sheet.setChartSheet(ss.chartSheet);
        sheet.setPivots(ss.pivots);
        sheet.setScenarios(ss.scenarios);
        sheet.setDataTables(ss.dataTables);
        sheet.setKeptRules(ss.keptRules);
        sheet.setCustomViews(ss.customViews);
        sheet.setSparklineGroups(ss.sparklines);
    }
    for (const CalcEng::CellStyle &style : snapshot.styles) {
        fresh.addStyle(style);
    }
    // Named ranges: defineName resolves through the host; the fresh book
    // already holds its sheets, so definitions land correctly.
    for (auto it = snapshot.names.constBegin(); it != snapshot.names.constEnd(); ++it) {
        fresh.defineName(it.key(), it.value());
    }
    // AGENT-GUARD: names before the dependency index below — a formula
    // using a formula name depends on what the name's body references.
    fresh.setDefinedNames(snapshot.definedNames);
    fresh.source = snapshot.source;
    fresh.charts = snapshot.charts;
    fresh.images = snapshot.images;
    fresh.protection = snapshot.protection;
    fresh.pivotCaches = snapshot.pivotCaches;
    fresh.customViews = snapshot.customViews;

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
    // recalcAll marks the formula cells; the value cells it used to be
    // handed alongside them could never change, but they made the dirty set
    // ten times bigger and the sort that orders it far more than that.
    fresh.recalcAll();
    // A pivot restored from the recovery payload has no layout yet (M74).
    for (qint32 s = 0; s < fresh.sheetCount(); ++s) {
        for (qsizetype i = 0; i < fresh.sheetAt(s).pivots().size(); ++i) {
            if (!fresh.sheetAt(s).pivots().at(i).render) {
                CalcEng::rebuildPivotRender(fresh, {s, qint32(i)});
            }
        }
    }
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
    // QUndoStack::push() calls redo() the moment the command is pushed, but
    // this command was built FROM the workbook's current state: "after" is a
    // snapshot of what is already there. Restoring it would rebuild every
    // sheet and recalculate every formula to reach the state it started in —
    // seconds of work per edit on a hundred-thousand-cell workbook. The
    // notification still goes out, so the views refresh either way.
    if (!m_pushed) {
        m_pushed = true;
        m_document->noteSnapshotApplied(m_after);
        return;
    }
    apply(m_after);
}

} // namespace QindaOffice::Apps::QindaCalc
