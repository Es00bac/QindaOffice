// SPDX-License-Identifier: GPL-3.0-or-later
#include "structure.h"

#include "chart_spec.h"
#include "sheet.h"
#include "workbook.h"

#include <algorithm>

namespace QindaOffice::Calc {
namespace {

// One axis of a structural edit: `count` slots inserted at `at`, or the
// `count` slots starting at `at` removed.
struct Band {
    qint32 at = 0;
    qint32 count = 1;
    bool insert = true;
};

[[nodiscard]] Band bandOf(const StructureEdit &edit)
{
    return {edit.at, edit.count,
            edit.op == StructureOp::InsertRows || edit.op == StructureOp::InsertColumns};
}

[[nodiscard]] bool editsRows(const StructureEdit &edit)
{
    return edit.op == StructureOp::InsertRows || edit.op == StructureOp::RemoveRows;
}

// Where a single coordinate lands. Returns -1 when the slot itself is gone.
[[nodiscard]] qint32 movedIndex(qint32 value, const Band &band)
{
    if (band.insert) {
        return value >= band.at ? value + band.count : value;
    }
    if (value >= band.at && value < band.at + band.count) {
        return -1;
    }
    return value >= band.at + band.count ? value - band.count : value;
}

// Where a span [lo, hi] lands. Returns false when the whole span is gone
// (the reference becomes #REF!). A span that only overlaps the removed band
// shrinks; one that straddles an insertion point grows.
[[nodiscard]] bool movedSpan(qint32 &lo, qint32 &hi, const Band &band)
{
    if (band.insert) {
        if (lo >= band.at) {
            lo += band.count;
        }
        if (hi >= band.at) {
            hi += band.count;
        }
        return true;
    }
    const qint32 first = band.at;
    const qint32 last = band.at + band.count - 1;
    if (lo >= first && hi <= last) {
        return false;
    }
    const auto clampLow = [&](qint32 v) {
        if (v > last) {
            return v - band.count;
        }
        return v >= first ? first : v;
    };
    const auto clampHigh = [&](qint32 v) {
        if (v > last) {
            return v - band.count;
        }
        return v >= first ? first - 1 : v;
    };
    lo = clampLow(lo);
    hi = clampHigh(hi);
    return lo <= hi;
}

// True when `ref` addresses the sheet the edit happened on. An empty sheet
// qualifier means "the sheet the formula lives on".
[[nodiscard]] bool refTargetsEditedSheet(const RangeRef &ref, bool ownerIsEdited,
                                         const QString &editedSheetName)
{
    if (ref.sheet.isEmpty()) {
        return ownerIsEdited;
    }
    return ref.sheet.compare(editedSheetName, Qt::CaseInsensitive) == 0;
}

// Adjusts a reference in place; false means the reference died (#REF!).
[[nodiscard]] bool adjustRef(RangeRef &ref, const StructureEdit &edit, bool ownerIsEdited,
                             const QString &editedSheetName)
{
    if (!refTargetsEditedSheet(ref, ownerIsEdited, editedSheetName)) {
        return true;
    }
    const Band band = bandOf(edit);
    if (editsRows(edit)) {
        // A whole-column reference (A:C) spans every row by definition, so
        // inserting rows leaves it alone.
        if (ref.wholeCols) {
            return true;
        }
        return movedSpan(ref.range.topLeft.row, ref.range.bottomRight.row, band);
    }
    if (ref.wholeRows) {
        return true;
    }
    return movedSpan(ref.range.topLeft.col, ref.range.bottomRight.col, band);
}

// Rewrites an AST so its references follow the edit. Dead references become
// literal #REF! nodes, which is what a spreadsheet must show rather than
// quietly re-pointing at whatever slid into the deleted cell's place.
[[nodiscard]] NodePtr adjustNode(const NodePtr &node, const StructureEdit &edit,
                                 bool ownerIsEdited, const QString &editedSheetName)
{
    if (!node) {
        return node;
    }
    switch (node->kind) {
    case Node::Kind::Number:
    case Node::Kind::Text:
    case Node::Kind::Bool:
    case Node::Kind::Error:
    case Node::Kind::Name:
        return node;
    case Node::Kind::Ref: {
        RangeRef ref = node->ref;
        if (!adjustRef(ref, edit, ownerIsEdited, editedSheetName)) {
            return Node::makeError(Err::Ref);
        }
        return Node::makeRef(ref);
    }
    case Node::Kind::Unary:
        return Node::makeUnary(node->op,
                               adjustNode(node->left, edit, ownerIsEdited, editedSheetName));
    case Node::Kind::Binary:
        return Node::makeBinary(node->op,
                                adjustNode(node->left, edit, ownerIsEdited, editedSheetName),
                                adjustNode(node->right, edit, ownerIsEdited, editedSheetName));
    case Node::Kind::Call: {
        QVector<NodePtr> args;
        args.reserve(node->args.size());
        for (const NodePtr &arg : node->args) {
            args.append(adjustNode(arg, edit, ownerIsEdited, editedSheetName));
        }
        return Node::makeCall(node->text, std::move(args));
    }
    }
    return node;
}

// Rewrites every formula in the workbook for `edit`. Callers reindex after.
void rewriteFormulas(Workbook &book, const StructureEdit &edit)
{
    const QString editedSheetName = book.sheetName(edit.sheet);
    for (qint32 s = 0; s < book.sheetCount(); ++s) {
        Sheet &sheet = book.sheetAt(s);
        QHash<quint64, Cell> cells = sheet.cells();
        bool touched = false;
        for (auto it = cells.begin(); it != cells.end(); ++it) {
            if (!it.value().ast) {
                continue;
            }
            const NodePtr adjusted =
                adjustNode(it.value().ast, edit, s == edit.sheet, editedSheetName);
            if (adjusted == it.value().ast) {
                continue;
            }
            it.value().ast = adjusted;
            it.value().formula = nodeText(adjusted);
            touched = true;
        }
        if (touched) {
            sheet.replaceContents(std::move(cells), sheet.styleIds(), sheet.columnWidths(),
                                  sheet.rowHeights(), sheet.merges());
        }
    }
}

// Named ranges are absolute addresses, so they follow the edit exactly like
// a formula reference; one whose range is entirely removed is dropped.
void rewriteNames(Workbook &book, const StructureEdit &edit)
{
    const QString editedSheetName = book.sheetName(edit.sheet);
    const QHash<QString, RangeRef> names = book.namedRanges();
    for (const auto &[name, target] : names.asKeyValueRange()) {
        RangeRef ref = target;
        // Names are pinned to a sheet at definition time, so "owner is the
        // edited sheet" is never the fallback here.
        if (!adjustRef(ref, edit, false, editedSheetName)) {
            book.removeName(name);
            continue;
        }
        if (!(ref.range == target.range)) {
            book.defineName(name, ref);
        }
    }
}

// Charts carry a range and an anchor cell on one sheet; both move with the
// edit, and a chart whose whole data range is removed goes with it.
void rewriteCharts(Workbook &book, const StructureEdit &edit)
{
    const Band band = bandOf(edit);
    const bool rows = editsRows(edit);
    QList<ChartSpec> kept;
    kept.reserve(book.charts.size());
    for (ChartSpec spec : std::as_const(book.charts)) {
        if (spec.sheet != edit.sheet) {
            kept.append(spec);
            continue;
        }
        CellRange range = parseChartRange(spec.rangeText);
        if (range.isValid()) {
            qint32 lo = rows ? range.topLeft.row : range.topLeft.col;
            qint32 hi = rows ? range.bottomRight.row : range.bottomRight.col;
            if (!movedSpan(lo, hi, band)) {
                continue; // the chart's data is gone
            }
            if (rows) {
                range.topLeft.row = lo;
                range.bottomRight.row = hi;
            } else {
                range.topLeft.col = lo;
                range.bottomRight.col = hi;
            }
            spec.rangeText = addrText(range.topLeft) + QLatin1Char(':')
                + addrText(range.bottomRight);
        }
        qint32 &anchor = rows ? spec.anchorRow : spec.anchorCol;
        const qint32 moved = movedIndex(anchor, band);
        anchor = moved < 0 ? band.at : moved;
        kept.append(spec);
    }
    book.charts = kept;
}

// Moves the sheet's sparse maps. Cells and styles that fall in a removed
// band are dropped; merges follow their span and collapse with it.
void moveSheetContents(Sheet &sheet, const StructureEdit &edit)
{
    const Band band = bandOf(edit);
    const bool rows = editsRows(edit);

    QHash<quint64, Cell> cells;
    cells.reserve(sheet.cells().size());
    for (const auto &[packed, cell] : sheet.cells().asKeyValueRange()) {
        CellAddr addr{qint32(packed & 0xffffffffu), qint32(packed >> 32)};
        const qint32 moved = movedIndex(rows ? addr.row : addr.col, band);
        if (moved < 0) {
            continue;
        }
        (rows ? addr.row : addr.col) = moved;
        cells.insert(addr.key(), cell);
    }

    QHash<quint64, qint32> styles;
    styles.reserve(sheet.styleIds().size());
    for (const auto &[packed, id] : sheet.styleIds().asKeyValueRange()) {
        CellAddr addr{qint32(packed & 0xffffffffu), qint32(packed >> 32)};
        const qint32 moved = movedIndex(rows ? addr.row : addr.col, band);
        if (moved < 0) {
            continue;
        }
        (rows ? addr.row : addr.col) = moved;
        styles.insert(addr.key(), id);
    }

    QHash<qint32, qreal> colWidths;
    QHash<qint32, qreal> rowHeights;
    const auto moveGeometry = [&band](const QHash<qint32, qreal> &in) {
        QHash<qint32, qreal> out;
        out.reserve(in.size());
        for (const auto &[index, size] : in.asKeyValueRange()) {
            const qint32 moved = movedIndex(index, band);
            if (moved >= 0) {
                out.insert(moved, size);
            }
        }
        return out;
    };
    colWidths = rows ? sheet.columnWidths() : moveGeometry(sheet.columnWidths());
    rowHeights = rows ? moveGeometry(sheet.rowHeights()) : sheet.rowHeights();

    QList<CellRange> merges;
    for (CellRange merge : sheet.merges()) {
        qint32 lo = rows ? merge.topLeft.row : merge.topLeft.col;
        qint32 hi = rows ? merge.bottomRight.row : merge.bottomRight.col;
        if (!movedSpan(lo, hi, band)) {
            continue;
        }
        if (rows) {
            merge.topLeft.row = lo;
            merge.bottomRight.row = hi;
        } else {
            merge.topLeft.col = lo;
            merge.bottomRight.col = hi;
        }
        merges.append(merge);
    }

    sheet.replaceContents(std::move(cells), std::move(styles), std::move(colWidths),
                          std::move(rowHeights), std::move(merges));
}

} // namespace

void applyStructureEdit(Workbook &book, const StructureEdit &edit)
{
    if (edit.count <= 0 || edit.at < 0 || edit.sheet < 0 || edit.sheet >= book.sheetCount()) {
        return;
    }
    // AGENT-GUARD: formulas are rewritten BEFORE the cells move, because the
    // rewrite reads the pre-edit sheet name/contents and must see every
    // formula exactly once — including the ones that are about to be
    // deleted, which simply disappear with their cells.
    rewriteFormulas(book, edit);
    rewriteNames(book, edit);
    rewriteCharts(book, edit);
    moveSheetContents(book.sheetAt(edit.sheet), edit);
    book.reindexAll();
}

} // namespace QindaOffice::Calc
