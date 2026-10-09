// SPDX-License-Identifier: GPL-3.0-or-later
// Dynamic arrays (M66, D-075): a dynamic-array formula whose result is an
// array — FILTER, SORT, SEQUENCE, a range, array arithmetic — spills it
// into the cells below and to its right, the way Excel 365 does.
//
// AGENT-NOTE: spilled values are ordinary cells of the sheet (Cell::
// spillAnchor names their anchor), so painting, the readers of those
// cells, copy, find and the xlsx writer — which stores spilled cells as
// plain cached values, exactly as Excel does — need nothing special. The
// engine owns them: an anchor's evaluation rewrites its spill, a cell the
// user writes into a spill blocks it (#SPILL!), and anything that moves
// cells (reindexAll, recalcAll) drops every spilled cell and lets the
// anchors spill again. Which cells an anchor spilled into is derived state
// (m_spills), never saved and never snapshotted.

#include "workbook.h"

#include "eval_array.h"

namespace QindaOffice::Calc {

namespace {

// The largest spill written (a quarter of a million cells). A whole-column
// result beyond it is #SPILL! — Excel would spill it, but each spilled
// cell here is a real cell, and a million of them is not a sheet anyone
// reads (D-075 documents the limit).
constexpr qint64 kMaxSpillCells = qint64(1) << 18;

bool blocksSpill(const Sheet &sheet, const CellAddr &at, const CellAddr &anchor)
{
    const Cell *cell = sheet.cellAt(at);
    if (cell != nullptr && (cell->ast || (!isBlank(cell->value) && cell->spillAnchor != anchor))) {
        return true; // user content: a value or a formula of its own
    }
    // Excel refuses to spill into merged cells or a table.
    return sheet.mergeCovering(at) != nullptr || sheet.tableCovering(at) != nullptr;
}

} // namespace

void Workbook::recalcPass(const QSet<CellKey> &edited, bool withVolatile, QSet<CellKey> *spillChanged)
{
    // An anchor whose cell no longer holds a dynamic formula (overwritten,
    // cleared, retyped as a value) lets go of its spill first.
    for (const CellKey &key : edited) {
        if (!m_spills.contains(key)) {
            continue;
        }
        const Cell *cell = sheetAt(key.sheet()).cellAt({key.row(), key.col()});
        if (cell == nullptr || !cell->ast || !cell->dynamicArray) {
            dropSpill(key, spillChanged);
        }
    }
    QSet<CellKey> seeds = edited;
    seeds |= anchorsCovering(edited);
    QSet<CellKey> dirty = m_dependencies.dependentsOf(seeds);
    if (withVolatile) {
        dirty |= m_dependencies.volatileCells();
    }
    dirty |= seeds;
    QSet<CellKey> cycles;
    const QVector<CellKey> order = topoOrder(dirty, m_dependencies, &cycles);
    for (const CellKey &key : order) {
        Sheet &sheet = sheetAt(key.sheet());
        Cell *cell = sheet.cellAt({key.row(), key.col()});
        if (cell == nullptr || !cell->ast) {
            continue;
        }
        EvalContext ctx{this, key.sheet(), {key.row(), key.col()}};
        if (!cell->dynamicArray) {
            cell->value = evalNode(cell->ast, ctx);
            continue;
        }
        // AGENT-GUARD: hold the AST and never touch `cell` again here —
        // spilling inserts cells, which may rehash the sheet under it.
        const NodePtr ast = cell->ast;
        applySpill(key.sheet(), {key.row(), key.col()}, evalArg(ast, ctx), spillChanged);
    }
    for (const CellKey &key : cycles) {
        if (Cell *cell = sheetAt(key.sheet()).cellAt({key.row(), key.col()})) {
            cell->value = Err::Circular;
        }
        dropSpill(key, spillChanged);
    }
}

QSet<CellKey> Workbook::anchorsCovering(const QSet<CellKey> &edited) const
{
    QSet<CellKey> anchors;
    if (m_spills.isEmpty() || edited.isEmpty()) {
        return anchors;
    }
    for (auto spill = m_spills.constBegin(); spill != m_spills.constEnd(); ++spill) {
        const CellKey &anchorKey = spill.key();
        const CellAddr anchor{anchorKey.row(), anchorKey.col()};
        const Sheet &sheet = sheetAt(anchorKey.sheet());
        for (const CellKey &key : edited) {
            const CellAddr at{key.row(), key.col()};
            if (key.sheet() != anchorKey.sheet() || at == anchor || !spill->extent.contains(at)) {
                continue;
            }
            // The anchor's own spilled cell changing is the anchor's doing;
            // anything else in its way (a value typed there, a cell
            // cleared) means the spill must be worked out again.
            const Cell *cell = sheet.cellAt(at);
            if (cell != nullptr && cell->isSpilled() && cell->spillAnchor == anchor) {
                continue;
            }
            anchors.insert(anchorKey);
            break;
        }
    }
    return anchors;
}

void Workbook::applySpill(qint32 sheetIndex, const CellAddr &anchor, const Arg &result, QSet<CellKey> *changed)
{
    const CellKey key(sheetIndex, anchor.row, anchor.col);
    Sheet &sheet = sheetAt(sheetIndex);
    const auto setAnchorValue = [&sheet, &anchor](const Value &value) {
        if (Cell *cell = sheet.cellAt(anchor)) {
            cell->value = value;
        }
    };
    ArrayValue grid;
    if (const auto *view = std::get_if<RangeView>(&result)) {
        if (view->isValid() && view->range.cellCount() > kMaxSpillCells) {
            // =A:A and the like: too large to spill here (see the limit).
            dropSpill(key, changed);
            m_spills.insert(key, {CellRange{anchor, anchor}, true});
            setAnchorValue(Err::Spill);
            return;
        }
        EvalContext ctx{this, sheetIndex, anchor};
        const Value failed = arrayFromArg(ctx, result, &grid);
        if (!view->isValid() || isErr(failed)) {
            dropSpill(key, changed);
            setAnchorValue(view->isValid() ? failed : Value(Err::Ref));
            return;
        }
    } else if (const auto *array = std::get_if<ArrayValue>(&result)) {
        grid = *array;
    } else {
        // A scalar result does not spill.
        dropSpill(key, changed);
        setAnchorValue(std::get<Value>(result));
        return;
    }
    if (grid.rows <= 0 || grid.cols <= 0 || grid.cells.size() != grid.rows * grid.cols) {
        dropSpill(key, changed);
        setAnchorValue(Err::Calc);
        return;
    }
    // An empty element of a spilled array shows as 0, as in Excel.
    const auto element = [&grid](qint32 r, qint32 c) {
        const Value &value = grid.cells.at(r * grid.cols + c);
        return isBlank(value) ? Value(0.0) : value;
    };
    CellRange extent{anchor, {anchor.row + grid.rows - 1, anchor.col + grid.cols - 1}};
    bool blocked = extent.bottomRight.row >= Sheet::kMaxRows || extent.bottomRight.col >= Sheet::kMaxCols
        || extent.cellCount() > kMaxSpillCells || sheet.tableCovering(anchor) != nullptr;
    extent.bottomRight.row = qMin(extent.bottomRight.row, Sheet::kMaxRows - 1);
    extent.bottomRight.col = qMin(extent.bottomRight.col, Sheet::kMaxCols - 1);
    for (qint32 r = extent.topLeft.row; !blocked && r <= extent.bottomRight.row; ++r) {
        for (qint32 c = extent.topLeft.col; !blocked && c <= extent.bottomRight.col; ++c) {
            blocked = CellAddr{r, c} != anchor && blocksSpill(sheet, {r, c}, anchor);
        }
    }
    if (blocked) {
        dropSpill(key, changed);
        // The attempted extent is kept: a change inside it (the blocker
        // cleared) makes anchorsCovering work the spill out again.
        m_spills.insert(key, {extent, true});
        setAnchorValue(Err::Spill);
        return;
    }
    // Spilled cells the new result no longer reaches are cleared.
    if (const auto old = m_spills.constFind(key); old != m_spills.constEnd() && !old->blocked) {
        for (qint32 r = old->extent.topLeft.row; r <= old->extent.bottomRight.row; ++r) {
            for (qint32 c = old->extent.topLeft.col; c <= old->extent.bottomRight.col; ++c) {
                const Cell *cell = sheet.cellAt({r, c});
                if (!extent.contains({r, c}) && cell != nullptr && cell->isSpilled() && cell->spillAnchor == anchor) {
                    sheet.dropCellContent({r, c});
                    changed->insert(CellKey(sheetIndex, r, c));
                }
            }
        }
    }
    for (qint32 r = 0; r < grid.rows; ++r) {
        for (qint32 c = 0; c < grid.cols; ++c) {
            const CellAddr at{anchor.row + r, anchor.col + c};
            if (at == anchor) {
                continue;
            }
            Cell &spilled = sheet.ensureCell(at);
            const Value value = element(r, c);
            if (spilled.spillAnchor != anchor || !(spilled.value == value)) {
                changed->insert(CellKey(sheetIndex, at.row, at.col));
            }
            spilled.formula.clear();
            spilled.ast.reset();
            spilled.isVolatile = false;
            spilled.dynamicArray = false;
            spilled.value = value;
            spilled.spillAnchor = anchor;
        }
    }
    m_spills.insert(key, {extent, false});
    setAnchorValue(element(0, 0));
}

void Workbook::dropSpill(const CellKey &anchorKey, QSet<CellKey> *changed)
{
    const auto it = m_spills.constFind(anchorKey);
    if (it == m_spills.constEnd()) {
        return;
    }
    const SpillState state = it.value();
    m_spills.remove(anchorKey);
    if (state.blocked || anchorKey.sheet() >= sheetCount()) {
        return; // a blocked spill wrote nothing
    }
    Sheet &sheet = sheetAt(anchorKey.sheet());
    const CellAddr anchor{anchorKey.row(), anchorKey.col()};
    for (qint32 r = state.extent.topLeft.row; r <= state.extent.bottomRight.row; ++r) {
        for (qint32 c = state.extent.topLeft.col; c <= state.extent.bottomRight.col; ++c) {
            const Cell *cell = sheet.cellAt({r, c});
            if (cell != nullptr && cell->isSpilled() && cell->spillAnchor == anchor) {
                sheet.dropCellContent({r, c});
                changed->insert(CellKey(anchorKey.sheet(), r, c));
            }
        }
    }
}

void Workbook::resetSpills()
{
    m_spills.clear();
    for (qint32 s = 0; s < sheetCount(); ++s) {
        Sheet &sheet = sheetAt(s);
        QList<CellAddr> spilled;
        for (const auto &[packed, cell] : sheet.cells().asKeyValueRange()) {
            if (cell.isSpilled()) {
                spilled.append({qint32(packed & 0xffffffffu), qint32(packed >> 32)});
            }
        }
        for (const CellAddr &addr : std::as_const(spilled)) {
            sheet.dropCellContent(addr);
        }
    }
}

std::optional<Workbook::SpillState> Workbook::spillAt(qint32 sheet, const CellAddr &anchor) const
{
    const auto it = m_spills.constFind(CellKey(sheet, anchor.row, anchor.col));
    if (it == m_spills.constEnd()) {
        return std::nullopt;
    }
    return it.value();
}

CellAddr Workbook::spillAnchorOf(qint32 sheet, const CellAddr &addr) const
{
    if (sheet < 0 || sheet >= sheetCount()) {
        return {};
    }
    if (m_spills.contains(CellKey(sheet, addr.row, addr.col))) {
        return addr;
    }
    const Cell *cell = sheetAt(sheet).cellAt(addr);
    return cell != nullptr && cell->isSpilled() ? cell->spillAnchor : CellAddr{};
}

std::optional<CellRange> Workbook::spillExtent(qint32 sheet, const CellAddr &anchor) const
{
    const std::optional<SpillState> state = spillAt(sheet, anchor);
    if (!state || state->blocked) {
        return std::nullopt;
    }
    return state->extent;
}

} // namespace QindaOffice::Calc
