// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "chart_spec.h"
#include "image_spec.h"
#include "dependencies.h"
#include "eval.h"
#include "custom_view.h"
#include "sheet.h"

#include <QDateTime>

#include <memory>
#include <vector>

namespace QindaOffice::Calc {

// A defined name the RangeRef table cannot hold (D-069): scoped to one sheet
// (Excel's localSheetId), bound to a constant or a formula, hidden or
// commented. The engine evaluates the body wherever it parses (`ast`), and
// the file round-trip carries `formula` verbatim either way, so a name the
// engine cannot evaluate is never dropped.
struct DefinedName {
    QString name;       // as the file spells it
    qint32 sheet = -1;  // localSheetId: the sheet it is scoped to; -1 = workbook
    QString formula;    // body without '=', engine spelling (_xlfn. prefixes stripped)
    NodePtr ast;        // parsed body; null when it does not parse
    bool hidden = false;
    QString comment;
    bool operator==(const DefinedName &) const = default;
};

// What the package a workbook was opened from carried that the engine does
// not model but a save must give back (D-069, D-068 rule 2): the theme part
// verbatim and the document properties. calc_io fills it on read and writes
// it on save; theme_colors resolves document colours from it. Document state: the
// app's undo snapshots and recovery payload carry it like the sheets.
struct WorkbookSource {
    QByteArray themeXml; // xl/theme/theme1.xml as read; empty = not from a package
    QString title;
    QString subject;
    QString creator;
    QString keywords;
    QString description;
    QString category;
    QDateTime created;
    // M66 (D-075): the parts a save carries without modelling them — pivot
    // tables and caches, slicers, timelines, query tables, connections,
    // external links, the data model, custom XML, threaded comments,
    // controls, printer settings, unmodelled drawing objects and the VBA
    // project — as a reduced copy of the source package (an OPC zip; cell
    // data stripped). calc_io builds it on read and copies from it on
    // save (xlsx_carry*); each sheet's SheetSource says which source sheet
    // it is. Empty: nothing to carry.
    QByteArray carried;
    QString codeName;          // <workbookPr codeName>: the VBA project's name for the workbook
    bool macroEnabled = false; // the source had a VBA project (an .xlsm)
    // M74 (D-083): the pivot table parts of `carried` the model took over
    // (Sheet::pivots). A save carries one of these only while a PivotTable
    // still names it — one deleted in QindaCalc stays deleted.
    QStringList pivotParts;
    // M78 (D-087): the source package's dxf table (xl/styles.xml <dxf>
    // elements, as written). A save writes them first, unchanged, so kept
    // conditional formats, carried PivotTable formats and formats nobody
    // edited keep their dxfId.
    QStringList dxfs;
    bool operator==(const WorkbookSource &) const = default;
};

// The spreadsheet document (plan section 7.2): ordered sheets, the shared
// cell-style table, named ranges, the dependency index and the recalc
// engine. Implements EvalHost so formulas evaluate against it directly.
//
// Input parsing (plan section 7.2): leading '=' starts a formula; leading '
// forces text; otherwise the text parses as a locale number/percent/date or
// boolean, falling back to text.
class Workbook final : public EvalHost {
public:
    Workbook();

    [[nodiscard]] qint32 sheetCount() const override { return qint32(m_sheets.size()); }
    [[nodiscard]] QString sheetName(qint32 index) const override;
    [[nodiscard]] qint32 sheetIndex(const QString &name) const override;
    // The active tab (view state that round-trips to xlsx; the document
    // layer maps it to its own active sheet on load/save).
    [[nodiscard]] qint32 activeTab() const { return m_activeTab; }
    void setActiveTab(qint32 index) { m_activeTab = qBound(qint32(0), index, qMax(qint32(0), sheetCount() - 1)); }
    Sheet &addSheet(const QString &name);
    [[nodiscard]] Sheet &sheetAt(qint32 index);
    [[nodiscard]] const Sheet &sheetAt(qint32 index) const;

    // Sheet-list mutation (structure.cpp wraps these with the formula and
    // chart fixups a user-visible Delete/Rename/Move Sheet also needs).
    // AGENT-GUARD: removing or moving a sheet shifts every sheet INDEX, and
    // the dependency graph is keyed by index — call reindexAll() after.
    bool removeSheetAt(qint32 index);
    bool renameSheetAt(qint32 index, const QString &name);
    bool moveSheetTo(qint32 from, qint32 to);
    // Inserts a new sheet at `index` (clamped); returns it.
    Sheet &insertSheet(qint32 index, const QString &name);
    // Rebuilds the name -> index map after a direct sheet-vector mutation.
    void rebuildSheetIndex();

    // Rebuilds the whole dependency index from the stored ASTs and
    // re-evaluates every formula once. Used after a structural edit
    // (rows/columns/sheets), where per-cell incremental updates would be
    // both wrong (positions and sheet indices moved) and quadratic.
    void reindexAll();

    // Parses and installs cell input; returns false when the input is a
    // formula that failed to parse (it is stored as text instead).
    bool setCellInput(qint32 sheet, const CellAddr &addr, const QString &input);
    // Installs an already-parsed formula (used by calc_io; shared formulas
    // arrive shifted from the master). Takes the canonical text for storage.
    void installFormula(qint32 sheet, const CellAddr &addr, const NodePtr &ast,
                        const QString &formula);
    // The same, saying whether the formula spills its array results (a
    // dynamic-array formula, M66/D-075). setCellInput installs every typed
    // formula this way, as Excel 365 does; a file says per cell (cm).
    void installFormula(qint32 sheet, const CellAddr &addr, const NodePtr &ast,
                        const QString &formula, bool dynamicArray);
    // Installs a pre-parsed literal (number/text/bool/error) without parsing.
    void setCellValue(qint32 sheet, const CellAddr &addr, const Value &value);
    // M78 (D-087): the value typed `input` would store — a number, a date
    // serial, a boolean, an error or text, as setCellInput parses it (a
    // formula, "=…", gives its text: evaluate it to know its result).
    [[nodiscard]] static Value inputValue(const QString &input);

    // Recalculates the transitive dependents of `edited` plus all volatile
    // cells in Kahn topological order; cells on a cycle get Err::Circular.
    void recalc(const QSet<CellKey> &edited);
    // Re-evaluates every formula cell in topological order. Used after bulk
    // loads (xlsx import) where installFormula's incremental recalc ran while
    // later sheets were still missing, leaving forward cross-sheet references
    // at their stale first-pass values.
    void recalcAll();
    // Bulk load: between begin and end, installFormula/setCellValue/
    // setCellInput record dependencies but do NOT recalculate; endBulkLoad
    // rebuilds the dependency index (references to sheets added later in
    // the load resolve only then) and recalculates once. AGENT-GUARD: the per-cell recalc re-evaluated
    // every dependent and every volatile cell on EVERY import — a 95k-cell
    // workbook with 10k formulas took minutes to open (quadratic); one pass
    // at the end is what a load needs anyway.
    void beginBulkLoad() { m_bulkLoading = true; }
    void endBulkLoad();
    [[nodiscard]] bool isBulkLoading() const { return m_bulkLoading; }
    // Edits made between beginBatch and endBatch (nesting) record what
    // they changed and recalculate it once, at the end — for an operation
    // that writes many cells into a live workbook, like filling a
    // calculated column (M69, D-078). Unlike a bulk load the dependency
    // index stays live, so no reindex is needed.
    void beginBatch() { ++m_batchDepth; }
    void endBatch();

    // Style table shared by the sheets.
    [[nodiscard]] qint32 addStyle(const CellStyle &style);
    [[nodiscard]] const CellStyle *style(qint32 id) const;
    // Table length including the reserved id-0 default (snapshot/undo seam).
    [[nodiscard]] qint32 styleCount() const { return qint32(m_styles.size()); }

    // Named ranges (simple refs in v1). Names match without regard to case,
    // as in Excel, and keep the spelling they were defined with (M61: the
    // table used to upper-case them, so a save renamed "Total" to "TOTAL").
    void defineName(const QString &name, const RangeRef &target);
    [[nodiscard]] std::optional<RangeRef> namedRange(const QString &name) const;
    // Full table (snapshot/undo seam; defineName stays the mutation path).
    [[nodiscard]] const QHash<QString, RangeRef> &namedRanges() const { return m_names; }
    // Drops a name (structural edits delete names whose range is gone).
    bool removeName(const QString &name);

    // Names beyond that table (D-069): sheet-scoped, constant and formula
    // names, in file order. Replaces the whole list; an entry with a null
    // `ast` has its `formula` parsed here. Callers reindex/recalc after.
    void setDefinedNames(QList<DefinedName> names);
    [[nodiscard]] const QList<DefinedName> &definedNames() const { return m_definedNames; }
    // EvalHost: the body a name stands for when used on `sheet` — a name
    // scoped to that sheet first, then a workbook-wide formula or constant
    // name; null when the RangeRef table (namedRange) or nobody answers. A
    // body that does not parse answers #NAME?.
    [[nodiscard]] NodePtr nameFormula(const QString &name, qint32 sheet) const override;
    // EvalHost: the formula text of a cell (ISFORMULA, FORMULATEXT).
    [[nodiscard]] QString cellFormula(qint32 sheet, qint32 row, qint32 col) const override;

    [[nodiscard]] bool rowExcluded(qint32 sheet, qint32 row, bool includeHidden) const override;

    // EvalHost implementation.
    [[nodiscard]] Value cellValue(qint32 sheet, qint32 row, qint32 col) const override;
    [[nodiscard]] QVector<QPair<CellAddr, Value>> nonBlankCellsIn(
        qint32 sheet, const CellRange &range) const override;

    // The full conditional-format evaluation for a cell (classic ops AND
    // Expression/DataBar rules against live values). Painting and printing
    // both go through this, so paper and screen cannot disagree. The data
    // bar extent cache is invalidated by every recalculation.
    [[nodiscard]] CondFormat condFormatFor(qint32 sheet, const CellAddr &addr) const;

    [[nodiscard]] DependencyGraph &dependencies() { return m_dependencies; }

    // ---- dynamic arrays (M66, D-075; spill.cpp) ----
    // The spill of an anchor whose last result was an array: the cells it
    // covers (or tried to, when blocked — then the anchor shows #SPILL!).
    // A one-cell array is a one-cell extent. Derived state: every
    // recalculation keeps it current; it is never saved or snapshotted.
    struct SpillState {
        CellRange extent;
        bool blocked = false;
    };
    [[nodiscard]] std::optional<SpillState> spillAt(qint32 sheet, const CellAddr &anchor) const;
    // The anchor whose spill covers `addr` (the anchor itself included), or
    // an invalid address.
    [[nodiscard]] CellAddr spillAnchorOf(qint32 sheet, const CellAddr &addr) const;
    // EvalHost: A1# (ANCHORARRAY) — the unblocked spill of an anchor.
    [[nodiscard]] std::optional<CellRange> spillExtent(qint32 sheet, const CellAddr &anchor) const override;

    // ---- legacy array formulas (M69, D-078; spill.cpp) ----
    // Installs a Ctrl+Shift+Enter array formula over `block`: the anchor is
    // its top-left cell, every cell of the block shows its element of the
    // result (a scalar fills the block, one row or column repeats, cells
    // beyond the result are #N/A — Excel's rules). Whatever the block held
    // is replaced. Recalculates unless bulk loading.
    void installArrayFormula(qint32 sheet, const CellRange &block, const NodePtr &ast, const QString &formula);
    // The legacy array block covering `addr`, if any (its anchor is the
    // block's top-left cell).
    [[nodiscard]] std::optional<CellRange> arrayBlockAt(qint32 sheet, const CellAddr &addr) const;

    // ---- Excel tables (M69, D-078; table_refs.cpp, table_edit.h) ----
    // EvalHost: a table by name (case-insensitive) on any sheet, and the
    // table covering a cell.
    [[nodiscard]] const TableSpec *tableNamed(const QString &name, qint32 *sheet) const override;
    [[nodiscard]] const TableSpec *tableAt(qint32 sheet, const CellAddr &addr) const override;
    // After a table changed shape or names (it grew, a column was renamed,
    // one was made): every formula with a structured reference has its
    // dependencies collected again and is recalculated. Much cheaper than
    // reindexAll, which the structural edits use anyway.
    void refreshStructuredRefs();

    // ---- data tables (M78, D-087; data_table.h) ----
    // Works out every data table of the workbook ({=TABLE(r,c)}): for each
    // input value the input cell takes it, the workbook recalculates, and
    // the formula's value lands in the table's cell; the input cell gets its
    // own content back. Called at the end of every top-level recalculation
    // (never while bulk loading, never from inside itself).
    void recomputeDataTables();

    // ---- protection (M66, D-075) ----
    // True when the sheet is protected and the cell's format is locked.
    [[nodiscard]] bool isCellLocked(qint32 sheet, const CellAddr &addr) const;
    // True when the formula of the cell is hidden by protection.
    [[nodiscard]] bool isFormulaHidden(qint32 sheet, const CellAddr &addr) const;
    WorkbookProtection protection;

    // ---- PivotTables (M74, D-083; pivot_edit.h) ----
    // EvalHost: GETPIVOTDATA's lookup (pivot_query.cpp).
    [[nodiscard]] Value pivotData(qint32 sheet, const CellAddr &addr, const QString &dataField,
                                  const QList<QPair<QString, Value>> &pairs) const override;
    // The caches the sheets' PivotTables summarize (PivotTable::cacheId is
    // PivotCache::id). Document state like the charts: undo and recovery
    // carry it.
    QList<PivotCache> pivotCaches;

    // Floating charts anchored on the sheets (plan section 7.2); exported
    // to xlsx, dropped with a warning on import (QXlsx has no chart read).
    QList<ChartSpec> charts;

    // Floating images (M39, D-040), encoded bytes as the file carried them.
    QList<ImageSpec> images;

    // M82 (D-091): Excel's Custom Views by name (custom_view.h); each
    // sheet holds its part of them (Sheet::customViews). Document state:
    // undo and recovery carry it.
    QList<CustomViewSpec> customViews;

    // The source package's theme and document properties (D-069).
    WorkbookSource source;

private:
    static Value parseLiteral(const QString &input, bool *isFormula, NodePtr *ast,
                              QString *formulaText);
    // spill.cpp: one recalculation pass over `edited`; the cells a spill
    // wrote or cleared land in `spillChanged` for the next pass.
    void recalcPass(const QSet<CellKey> &edited, bool withVolatile, QSet<CellKey> *spillChanged);
    void applySpill(qint32 sheet, const CellAddr &anchor, const Arg &result, QSet<CellKey> *changed);
    // A legacy array formula's result into its fixed block (M69).
    void applyArrayBlock(qint32 sheet, const CellAddr &anchor, const CellRange &block, const Arg &result,
                         QSet<CellKey> *changed);
    // A cell about to hold something else lets go of the array block it
    // anchors: its other cells are emptied (and land in `changed`).
    void releaseArrayBlock(qint32 sheet, const CellAddr &addr, QSet<CellKey> *changed);
    void dropSpill(const CellKey &anchor, QSet<CellKey> *changed);
    // Removes every spilled cell and forgets every spill (the next full
    // recalculation rebuilds them) — after edits that move cells.
    void resetSpills();
    [[nodiscard]] QSet<CellKey> anchorsCovering(const QSet<CellKey> &edited) const;
    // Clears the spill-owner mark of a cell about to hold user content.
    static void claimCell(Cell &cell) { cell.spillAnchor = CellAddr{}; }

    std::vector<std::unique_ptr<Sheet>> m_sheets;
    QHash<QString, qint32> m_sheetIndex;
    qint32 m_activeTab = 0;
    QVector<CellStyle> m_styles;
    QHash<QString, RangeRef> m_names;      // as spelled -> target
    QHash<QString, QString> m_nameSpelling; // upper-case -> the key in m_names
    QList<DefinedName> m_definedNames;
    QHash<QString, QList<qsizetype>> m_definedIndex; // upper-case name -> entries
    DependencyGraph m_dependencies;
    bool m_bulkLoading = false;
    // The statistics conditional formats measure against (data-bar and
    // colour-scale extremes, Top 10, averages, duplicates — M78), cleared on
    // every recalculation. Mutable: condFormatFor is a const evaluation.
    mutable CondFormatCache m_condCache;
    // M78 (D-087): data tables are being worked out (data_table.cpp) — the
    // recalculations they cause must not start another round.
    bool m_computingTables = false;
    QHash<CellKey, SpillState> m_spills; // anchor → its spill (see spillAt)
    int m_batchDepth = 0;
    QSet<CellKey> m_batchEdited;
    // The recalculation an edit asks for: none while bulk loading, deferred
    // while batching, at once otherwise.
    void editedCells(const QSet<CellKey> &edited);
};

} // namespace QindaOffice::Calc
