// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cell.h"
#include "data_table.h"
#include "hyperlink.h"
#include "outline.h"
#include "pivot.h"
#include "protection.h"
#include "scenario.h"
#include "sheet_rules.h"
#include "sheet_source.h"

#include <QSet>

#include <functional>

namespace QindaOffice::Calc {

// One sparse worksheet (plan section 7.2). Geometry bounds follow the Excel
// grid (1048576 rows x 16384 columns); the hash keeps empty space free.
class Sheet {
public:
    static constexpr qint32 kMaxRows = 1048576;
    static constexpr qint32 kMaxCols = 16384;

    explicit Sheet(QString name = {});

    [[nodiscard]] const QString &name() const { return m_name; }
    void setName(const QString &name) { m_name = name; }

    [[nodiscard]] const QHash<quint64, Cell> &cells() const { return m_cells; }
    [[nodiscard]] Cell *cellAt(const CellAddr &addr);
    [[nodiscard]] const Cell *cellAt(const CellAddr &addr) const;
    // Stores the cell; returns a pointer valid until the next mutation.
    Cell &ensureCell(const CellAddr &addr);
    bool clearCell(const CellAddr &addr);

    [[nodiscard]] qreal columnWidth(qint32 col) const;  // px, 96 dpi
    [[nodiscard]] qreal rowHeight(qint32 row) const;    // px, 96 dpi
    void setColumnWidth(qint32 col, qreal px);
    void setRowHeight(qint32 row, qreal px);
    // Explicitly-sized geometry only (for round-tripping to files).
    [[nodiscard]] QList<qint32> sizedColumns() const { return m_colWidths.keys(); }
    [[nodiscard]] QList<qint32> sizedRows() const { return m_rowHeights.keys(); }

    [[nodiscard]] const QList<CellRange> &merges() const { return m_merges; }
    void addMerge(const CellRange &range) { m_merges.append(range); }
    void clearMerges() { m_merges.clear(); }
    // Drops the merge whose top-left is `anchor`; returns false when there
    // is none (Format > Unmerge on a cell that is not merged).
    bool removeMergeAt(const CellAddr &anchor);
    // The merge covering `addr`, or nullptr. Painting and hit-testing both
    // go through this so a merged block reads and clicks as one cell.
    [[nodiscard]] const CellRange *mergeCovering(const CellAddr &addr) const;

    // Explicit geometry, for the structural-edit rewrite (structure.cpp).
    [[nodiscard]] const QHash<qint32, qreal> &columnWidths() const { return m_colWidths; }
    [[nodiscard]] const QHash<qint32, qreal> &rowHeights() const { return m_rowHeights; }
    // Replaces the sparse maps wholesale. AGENT-GUARD: only structure.cpp
    // calls this — inserting or removing a row moves every map at once and
    // a partial rewrite would leave styles pointing at the wrong cells.
    void replaceContents(QHash<quint64, Cell> cells, QHash<quint64, qint32> styleIds,
                         QHash<qint32, qreal> colWidths, QHash<qint32, qreal> rowHeights,
                         QList<CellRange> merges);

    // --- Rules (sheet_rules.h). Not cell content: they address cells by
    // range, so structure.cpp moves them with the rows and columns they
    // cover, and calc_undo snapshots them like everything else.
    [[nodiscard]] const QList<CondFormatRule> &condFormats() const { return m_condFormats; }
    void setCondFormats(QList<CondFormatRule> rules) { m_condFormats = std::move(rules); }
    // The merged formatting of every rule matching `addr` with `value`, in
    // precedence order (first-wins per field, stopIfTrue ends the walk).
    // Classic ops only — Expression/DataBar rules are skipped here; the app
    // evaluates them through Workbook::condFormatFor.
    [[nodiscard]] CondFormat condFormatFor(const CellAddr &addr, const Value &value) const;

    [[nodiscard]] const QList<ValidationRule> &validations() const { return m_validations; }
    void setValidations(QList<ValidationRule> rules) { m_validations = std::move(rules); }
    // The first rule covering `addr`, or nullptr. One cell obeys one rule:
    // stacking contradictory rules is a trap with no good resolution.
    [[nodiscard]] const ValidationRule *validationAt(const CellAddr &addr) const;

    [[nodiscard]] const AutoFilter &autoFilter() const { return m_filter; }
    void setAutoFilter(AutoFilter filter) { m_filter = std::move(filter); }
    // Rows the active filter hides. Empty when no filter is active, so the
    // view can treat "no filter" and "filter matching everything" alike.
    [[nodiscard]] QSet<qint32> filteredOutRows() const;
    // The distinct display values in `column` of the filter's body, sorted,
    // for the filter dropdown. `display` renders one cell the way the grid
    // does (the caller owns number formatting).
    [[nodiscard]] QStringList filterValuesFor(
        qint32 column, const std::function<QString(const CellAddr &)> &display) const;

    [[nodiscard]] const QHash<quint64, CellComment> &comments() const { return m_comments; }
    void setComments(QHash<quint64, CellComment> comments) { m_comments = std::move(comments); }
    [[nodiscard]] const CellComment *commentAt(const CellAddr &addr) const;
    void setCommentAt(const CellAddr &addr, const CellComment &comment);
    bool clearCommentAt(const CellAddr &addr);

    // Structured tables (sheet_rules.h): range-addressed like every other
    // rule, moved by structure.cpp and carried by undo/recovery.
    [[nodiscard]] const QList<TableSpec> &tables() const { return m_tables; }
    void setTables(QList<TableSpec> tables) { m_tables = std::move(tables); }
    // The table covering `addr`, or nullptr (banding paints through this).
    [[nodiscard]] const TableSpec *tableCovering(const CellAddr &addr) const;

    // --- View & print state (M37): document state, not view state — it
    // round-trips to xlsx and rides undo/recovery like everything else.
    [[nodiscard]] qint32 freezeRows() const { return m_freezeRows; }
    [[nodiscard]] qint32 freezeCols() const { return m_freezeCols; }
    void setFreeze(qint32 rows, qint32 cols)
    {
        m_freezeRows = qMax(qint32(0), rows);
        m_freezeCols = qMax(qint32(0), cols);
    }
    [[nodiscard]] bool isHidden() const { return m_hidden; }
    void setHidden(bool hidden) { m_hidden = hidden; }
    // The cursor the sheet restores when activated (persisted per sheet).
    [[nodiscard]] CellAddr cursor() const { return m_cursor; }
    void setCursor(const CellAddr &addr) { m_cursor = addr; }
    [[nodiscard]] const std::optional<CellRange> &printArea() const { return m_printArea; }
    // AGENT-GUARD (M78, D-087): setting (or clearing) the print area
    // replaces EVERY area, as Excel's Set Print Area does — the further
    // areas of a multi-area print area (PageSetup::morePrintAreas) go too.
    // Whoever restores both (undo, the reader) sets the page setup after.
    void setPrintArea(std::optional<CellRange> area)
    {
        m_printArea = area;
        m_pageSetup.morePrintAreas.clear();
    }
    // Rows/columns repeated on every printed page (Excel's Print_Titles).
    [[nodiscard]] const std::optional<CellRange> &printTitleRows() const { return m_printTitleRows; }
    [[nodiscard]] const std::optional<CellRange> &printTitleCols() const { return m_printTitleCols; }
    void setPrintTitles(std::optional<CellRange> rows, std::optional<CellRange> cols)
    {
        m_printTitleRows = rows;
        m_printTitleCols = cols;
    }
    // Page Layout (M37; since M78, D-087, all of Excel's Page Setup): the
    // file's <pageSetup>, <pageMargins>, <printOptions>, <headerFooter>,
    // <rowBreaks>/<colBreaks> and sheetPr's fitToPage. page_layout.h cuts
    // the pages from it; the header and footer codes are header_footer.h's.
    struct PageSetup {
        int orientation = 0; // 0 portrait, 1 landscape
        // AGENT-NOTE (M78, D-087): 1 × 1 is the file's default (and Excel's
        // dialog's); the counts only matter under fitToPage. Before M78 the
        // defaults were 0 and a non-zero count meant "fit".
        int fitToWidth = 1;  // Fit to: pages wide (0 = automatic)
        int fitToHeight = 1; // pages tall (0 = automatic)
        // ---- M78 (D-087) ----
        // Scaling: "Fit to" (fitToWidth × fitToHeight) when set, else
        // "Adjust to" `scale` percent (10–400).
        bool fitToPage = false;
        int scale = 100;
        int paperSize = 0;       // Excel's paperSize code (page_layout.h); 0: not stated (Letter)
        int firstPageNumber = 1; // used when useFirstPageNumber ("First page number": Auto otherwise)
        bool useFirstPageNumber = false;
        bool overThenDown = false; // page order: over, then down (default: down, then over)
        bool blackAndWhite = false;
        bool draft = false;
        int cellComments = 0;    // 0 none, 1 at end of sheet, 2 as displayed
        int errors = 0;          // cell errors as: 0 displayed, 1 <blank>, 2 --, 3 #N/A
        // Margins in inches (Excel's unit), Normal by default.
        double marginLeft = 0.7;
        double marginRight = 0.7;
        double marginTop = 0.75;
        double marginBottom = 0.75;
        double marginHeader = 0.3;
        double marginFooter = 0.3;
        bool centerHorizontally = false;
        bool centerVertically = false;
        bool gridLines = false;  // Sheet Options ▸ Gridlines ▸ Print
        bool headings = false;   // Sheet Options ▸ Headings ▸ Print
        // Headers and footers as Excel's codes (&L&C&R, &P, &N, &D, …).
        QString oddHeader;
        QString oddFooter;
        QString evenHeader;
        QString evenFooter;
        QString firstHeader;
        QString firstFooter;
        bool differentOddEven = false;
        bool differentFirst = false;
        bool scaleWithDoc = true;
        bool alignWithMargins = true;
        // Manual page breaks: the 0-based rows (columns) a new page starts
        // at — the file's brk id. Ascending, no duplicates.
        QList<qint32> rowBreaks;
        QList<qint32> colBreaks;
        // The areas after the first of a multi-area print area (Excel's
        // _xlnm.Print_Area "A1:D20,F1:H20"); each prints on pages of its
        // own. The first area is Sheet::printArea.
        QList<CellRange> morePrintAreas;
        bool operator==(const PageSetup &) const = default;
    };
    [[nodiscard]] const PageSetup &pageSetup() const { return m_pageSetup; }
    void setPageSetup(const PageSetup &setup) { m_pageSetup = setup; }

    // ---- M78 (D-087): what-if analysis and the rules kept as written ----
    // The sheet's scenarios (scenario.h) and data tables (data_table.h):
    // moved by structural edits (structure_whatif.cpp), carried by undo and
    // recovery.
    [[nodiscard]] const ScenarioSet &scenarios() const { return m_scenarios; }
    void setScenarios(ScenarioSet scenarios) { m_scenarios = std::move(scenarios); }
    [[nodiscard]] const QList<DataTableSpec> &dataTables() const { return m_dataTables; }
    void setDataTables(QList<DataTableSpec> tables) { m_dataTables = std::move(tables); }
    // Conditional formats and validations QindaCalc does not model, kept
    // as the file wrote them (sheet_rules.h, KeptRule).
    [[nodiscard]] const QList<KeptRule> &keptRules() const { return m_keptRules; }
    void setKeptRules(QList<KeptRule> rules) { m_keptRules = std::move(rules); }

    // --- M66 (D-075): hyperlinks, outlines and hidden lines, protection,
    // and the sheet's tie to its source package. Range-addressed state
    // like the rules above: structure.cpp moves it, undo and recovery
    // carry it.
    [[nodiscard]] const QList<Hyperlink> &hyperlinks() const { return m_hyperlinks; }
    void setHyperlinks(QList<Hyperlink> links) { m_hyperlinks = std::move(links); }
    // The link covering `addr` (the last one added wins an overlap, as in
    // Excel), or nullptr.
    [[nodiscard]] const Hyperlink *hyperlinkAt(const CellAddr &addr) const;
    [[nodiscard]] const SheetOutline &outline() const { return m_outline; }
    void setOutline(SheetOutline outline) { m_outline = std::move(outline); }
    [[nodiscard]] bool isRowHidden(qint32 row) const { return m_outline.isHidden(OutlineAxis::Rows, row); }
    [[nodiscard]] bool isColumnHidden(qint32 col) const { return m_outline.isHidden(OutlineAxis::Columns, col); }
    [[nodiscard]] const SheetProtection &protection() const { return m_protection; }
    void setProtection(SheetProtection protection) { m_protection = std::move(protection); }
    [[nodiscard]] const SheetSource &source() const { return m_source; }
    void setSource(SheetSource source) { m_source = std::move(source); }
    // Records a structural edit for the carry stage (sheet_source.h); a
    // sheet that did not come from a package has nothing to map.
    void recordStructureStep(const SourceStep &step)
    {
        if (!m_source.part.isEmpty()) {
            m_source.steps.append(step);
        }
    }
    // M69 (D-078): a chart sheet — a sheet that is one chart (Excel's
    // chartsheet part). It has no cells; its chart is the workbook chart
    // whose `sheet` is this sheet, and calc_io carries its parts verbatim.
    [[nodiscard]] bool isChartSheet() const { return m_chartSheet; }
    void setChartSheet(bool chartSheet) { m_chartSheet = chartSheet; }
    // M74 (D-083): the PivotTables that live on the sheet (their output
    // is in its cells; their caches are Workbook::pivotCaches). Moved by
    // structural edits (pivot_structure.cpp), carried by undo and recovery.
    [[nodiscard]] const QList<PivotTable> &pivots() const { return m_pivots; }
    void setPivots(QList<PivotTable> pivots) { m_pivots = std::move(pivots); }
    [[nodiscard]] QList<PivotTable> &mutablePivots() { return m_pivots; }
    // Drops a cell's CONTENT, keeping its style (the spill engine removes
    // spilled values from cells the user may have formatted).
    bool dropCellContent(const CellAddr &addr) { return m_cells.remove(addr.key()) > 0; }

    [[nodiscard]] qint32 styleIdAt(const CellAddr &addr) const;
    void setStyleIdAt(const CellAddr &addr, qint32 id);
    // The full sparse style map (undo snapshots must cover styles set on
    // EMPTY cells — cells() only holds cells with content or formulas).
    [[nodiscard]] const QHash<quint64, qint32> &styleIds() const { return m_styleIds; }

private:
    QString m_name;
    QHash<quint64, Cell> m_cells;
    QHash<qint32, qreal> m_colWidths;
    QHash<qint32, qreal> m_rowHeights;
    QHash<quint64, qint32> m_styleIds;
    QList<CellRange> m_merges;
    QList<CondFormatRule> m_condFormats;
    QList<ValidationRule> m_validations;
    AutoFilter m_filter;
    QHash<quint64, CellComment> m_comments;
    QList<TableSpec> m_tables;
    qint32 m_freezeRows = 0;
    qint32 m_freezeCols = 0;
    bool m_hidden = false;
    CellAddr m_cursor{0, 0};
    std::optional<CellRange> m_printArea;
    std::optional<CellRange> m_printTitleRows;
    std::optional<CellRange> m_printTitleCols;
    PageSetup m_pageSetup;
    QList<Hyperlink> m_hyperlinks;
    SheetOutline m_outline;
    SheetProtection m_protection;
    SheetSource m_source;
    bool m_chartSheet = false;
    QList<PivotTable> m_pivots;
    ScenarioSet m_scenarios;           // M78
    QList<DataTableSpec> m_dataTables; // M78
    QList<KeptRule> m_keptRules;       // M78
};

} // namespace QindaOffice::Calc
