// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to calc_engine's structural edits: the arithmetic every moved
// thing shares — a cell, a reference, a rule's range, a hyperlink, an
// outline level (structure.cpp, structure_features.cpp). Split out in M66
// (D-075) so the files that move different kinds of state use ONE
// definition of where an index lands.

#include "parser.h"
#include "structure.h"

namespace QindaOffice::Calc {
class Sheet;
class Workbook;
struct TableSpec;
}

namespace QindaOffice::Calc::StructureMath {

// One axis of a structural edit: `count` slots inserted at `at`, or the
// `count` slots starting at `at` removed.
struct Band {
    qint32 at = 0;
    qint32 count = 1;
    bool insert = true;
};

[[nodiscard]] inline Band bandOf(const StructureEdit &edit)
{
    return {edit.at, edit.count,
            edit.op == StructureOp::InsertRows || edit.op == StructureOp::InsertColumns};
}

[[nodiscard]] inline bool editsRows(const StructureEdit &edit)
{
    return edit.op == StructureOp::InsertRows || edit.op == StructureOp::RemoveRows;
}

// Where a single coordinate lands. Returns -1 when the slot itself is gone.
[[nodiscard]] inline qint32 movedIndex(qint32 value, const Band &band)
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
[[nodiscard]] inline bool movedSpan(qint32 &lo, qint32 &hi, const Band &band)
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
[[nodiscard]] inline bool refTargetsEditedSheet(const RangeRef &ref, bool ownerIsEdited,
                                         const QString &editedSheetName)
{
    if (ref.sheet.isEmpty()) {
        return ownerIsEdited;
    }
    // AGENT-GUARD: RangeRef.sheet keeps the file's spelling, quotes
    // included ('Q1 Sales'); comparing it raw left every reference to a
    // quoted sheet unmoved by an insert (M61).
    return unquotedSheetName(ref.sheet).compare(editedSheetName, Qt::CaseInsensitive) == 0;
}

// Adjusts a reference in place; false means the reference died (#REF!).
[[nodiscard]] inline bool adjustRef(RangeRef &ref, const StructureEdit &edit, bool ownerIsEdited,
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

// M66 (D-075), structure_features.cpp: moves the edited sheet's
// hyperlinks and outline levels, follows internal link locations on every
// sheet, and records the edit in the sheet's source journal. Called by
// applyStructureEdit BEFORE the cells move (it reads the pre-edit sheet
// name) and before the reindex.
void moveSheetFeatures(Workbook &book, const StructureEdit &edit);
// A sheet rename re-points internal link locations; a removal (`to`
// empty) leaves them as they are — a dead link, the way Excel keeps one.
void requalifyLinkLocations(Workbook &book, const QString &from, const QString &to);

// M78 (D-087): a formula AST through the edit (structure.cpp's own walk):
// references into a removed band become #REF!. For the formulas that live
// outside cells — conditional formats, validations.
[[nodiscard]] NodePtr adjustFormulaNode(const NodePtr &node, const StructureEdit &edit, bool ownerIsEdited,
                                        const QString &editedSheetName);
// M78 (D-087), structure_rules.cpp: the formulas of every sheet's
// conditional formats and validations (operands, Expression and Custom
// formulas, cfvo formulas, a list's formula source) through the edit, as
// formulas placed at their rule's anchor — shifted first to the anchor the
// rule keeps when the edit removes its own. Called by applyStructureEdit
// BEFORE the cells move.
void moveRuleFormulas(Workbook &book, const StructureEdit &edit);
// M78 (D-087), structure_print.cpp: the edited sheet's print area, print
// titles and manual page breaks, and the ranges of the rules it keeps as
// written (KeptRule). Called by moveSheetContents.
void moveSheetPrintAndKept(Sheet &sheet, const StructureEdit &edit);
// M82 (D-091), custom_view.cpp: the edited sheet's parts of the Custom
// Views — hidden lines, filter range, print area, titles and breaks, the
// cells shown — move with the cells.
void moveCustomViews(Sheet &sheet, const StructureEdit &edit);
// M78 (D-087), structure_whatif.cpp: the edited sheet's scenarios
// (changing cells, summary result cells) and data tables (results, input
// cells — a deleted input cell marks the table del1/del2, Excel's #REF!).
// Called by applyStructureEdit before the cells move.
void moveWhatIfForEdit(Workbook &book, const StructureEdit &edit);

// M69 (D-078), structure_tables.cpp: Excel tables through the edit.
// BEFORE the cells move: structured references to columns or tables the
// edit removes become #REF! (a column range keeps what remains of it).
void rewriteTableRefs(Workbook &book, const StructureEdit &edit);
// The edited sheet's tables after the edit (read before the cells move):
// ranges moved, inserted columns added and removed ones dropped from the
// column records, a removed header or totals row gone.
[[nodiscard]] QList<TableSpec> movedTables(const Sheet &sheet, const StructureEdit &edit);
// AFTER the cells moved: inserted columns are named (Column<N>, written
// into their headers) and inserted data rows get the calculated columns.
void fillTablesAfterEdit(Workbook &book, const StructureEdit &edit);

} // namespace QindaOffice::Calc::StructureMath
