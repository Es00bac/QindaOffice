// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "chart_spec.h"
#include "dependencies.h"
#include "eval.h"
#include "sheet.h"

#include <memory>
#include <vector>

namespace QindaOffice::Calc {

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
    // Installs a pre-parsed literal (number/text/bool/error) without parsing.
    void setCellValue(qint32 sheet, const CellAddr &addr, const Value &value);

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
    // runs one recalcAll. AGENT-GUARD: the per-cell recalc re-evaluated
    // every dependent and every volatile cell on EVERY import — a 95k-cell
    // workbook with 10k formulas took minutes to open (quadratic); one pass
    // at the end is what a load needs anyway.
    void beginBulkLoad() { m_bulkLoading = true; }
    void endBulkLoad();
    [[nodiscard]] bool isBulkLoading() const { return m_bulkLoading; }

    // Style table shared by the sheets.
    [[nodiscard]] qint32 addStyle(const CellStyle &style);
    [[nodiscard]] const CellStyle *style(qint32 id) const;
    // Table length including the reserved id-0 default (snapshot/undo seam).
    [[nodiscard]] qint32 styleCount() const { return qint32(m_styles.size()); }

    // Named ranges (simple refs in v1).
    void defineName(const QString &name, const RangeRef &target);
    [[nodiscard]] std::optional<RangeRef> namedRange(const QString &name) const;
    // Full table (snapshot/undo seam; defineName stays the mutation path).
    [[nodiscard]] const QHash<QString, RangeRef> &namedRanges() const { return m_names; }
    // Drops a name (structural edits delete names whose range is gone).
    bool removeName(const QString &name);

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

    // Floating charts anchored on the sheets (plan section 7.2); exported
    // to xlsx, dropped with a warning on import (QXlsx has no chart read).
    QList<ChartSpec> charts;

private:
    static Value parseLiteral(const QString &input, bool *isFormula, NodePtr *ast,
                              QString *formulaText);

    std::vector<std::unique_ptr<Sheet>> m_sheets;
    QHash<QString, qint32> m_sheetIndex;
    QVector<CellStyle> m_styles;
    QHash<QString, RangeRef> m_names;
    DependencyGraph m_dependencies;
    bool m_bulkLoading = false;
    // Data-bar min/max per rule (keyed by rule content), cleared on every
    // recalculation. Mutable: condFormatFor is a const evaluation.
    mutable QHash<quint64, QPair<double, double>> m_dataBarExtents;
};

} // namespace QindaOffice::Calc
