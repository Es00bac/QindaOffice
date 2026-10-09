// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to calc_engine's structural edits: the arithmetic every moved
// thing shares — a cell, a reference, a rule's range, a hyperlink, an
// outline level (structure.cpp, structure_features.cpp). Split out in M66
// (D-075) so the files that move different kinds of state use ONE
// definition of where an index lands.

#include "structure.h"

namespace QindaOffice::Calc {
class Workbook;
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

} // namespace QindaOffice::Calc::StructureMath
