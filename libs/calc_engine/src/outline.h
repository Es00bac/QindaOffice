// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Row and column outlines (M66, D-075): Excel's grouping. Each row and
// column carries an outline level (0 = not grouped, up to 7), a hidden flag
// and a collapsed flag; a GROUP is a maximal run of lines at or above a
// level, and its SUMMARY line sits just below it (or above, when the sheet
// says summaryBelow="0"; columns likewise right/left). Hiding a group's
// detail hides its lines and marks the summary line collapsed — exactly
// the attributes Excel writes, so the model round-trips without
// interpretation.
//
// Hidden lines live here too, grouped or not (Format ▸ Hide Rows writes
// the same `hidden` attribute), so the grid, the printer and the file all
// read one source. Positions are absolute and sparse; structure.cpp moves
// them with inserted and removed rows and columns.

#include <QList>
#include <QMap>

#include <optional>

namespace QindaOffice::Calc {

struct LineOutline {
    quint8 level = 0; // 0..kMaxOutlineLevel
    bool hidden = false;
    bool collapsed = false;
    bool operator==(const LineOutline &) const = default;
    [[nodiscard]] bool isDefault() const { return level == 0 && !hidden && !collapsed; }
};

inline constexpr quint8 kMaxOutlineLevel = 7;

enum class OutlineAxis { Rows, Columns };

struct SheetOutline {
    QMap<qint32, LineOutline> rows; // 0-based row → state; default entries are never stored
    QMap<qint32, LineOutline> cols;
    bool summaryBelow = true;  // a row group's summary row follows it
    bool summaryRight = true;  // a column group's summary column follows it
    bool operator==(const SheetOutline &) const = default;

    [[nodiscard]] const QMap<qint32, LineOutline> &lines(OutlineAxis axis) const
    {
        return axis == OutlineAxis::Rows ? rows : cols;
    }
    [[nodiscard]] QMap<qint32, LineOutline> &lines(OutlineAxis axis)
    {
        return axis == OutlineAxis::Rows ? rows : cols;
    }
    [[nodiscard]] LineOutline line(OutlineAxis axis, qint32 index) const { return lines(axis).value(index); }
    // Stores `state`, dropping the entry when it is the default.
    void setLine(OutlineAxis axis, qint32 index, const LineOutline &state);
    [[nodiscard]] bool isHidden(OutlineAxis axis, qint32 index) const { return line(axis, index).hidden; }
    // The deepest level in use (Excel's outlineLevelRow/outlineLevelCol).
    [[nodiscard]] quint8 maxLevel(OutlineAxis axis) const;
    [[nodiscard]] bool isEmpty() const { return rows.isEmpty() && cols.isEmpty() && summaryBelow && summaryRight; }
};

// One group: lines [first, last] at `level` or deeper, its summary line
// (-1 when it would fall off the sheet's start) and whether its detail is
// hidden right now.
struct OutlineGroup {
    qint32 first = 0;
    qint32 last = 0;
    quint8 level = 1;
    qint32 summary = -1;
    bool collapsed = false;
    bool operator==(const OutlineGroup &) const = default;
};

// Every group of every level, outermost first within each position — what
// the outline gutter draws a bracket and a +/− button for.
[[nodiscard]] QList<OutlineGroup> outlineGroups(const SheetOutline &outline, OutlineAxis axis);

// The innermost group containing `index` (a detail line), or the group
// whose summary line `index` is; nullopt when there is none.
[[nodiscard]] std::optional<OutlineGroup> groupAt(const SheetOutline &outline, OutlineAxis axis, qint32 index);

// Data ▸ Group / Ungroup over [first, last]: one level deeper (at most 7)
// or one shallower. Return false when nothing changed (already at 7, or
// nothing grouped there).
bool groupLines(SheetOutline &outline, OutlineAxis axis, qint32 first, qint32 last);
bool ungroupLines(SheetOutline &outline, OutlineAxis axis, qint32 first, qint32 last);

// Hide Detail / Show Detail of one group: hiding hides every line of the
// group and marks the summary line collapsed; showing unhides the lines
// that no collapsed inner group still hides, and clears the mark.
void setGroupCollapsed(SheetOutline &outline, OutlineAxis axis, const OutlineGroup &group, bool collapse);

// The outline's level buttons: show levels 1..n (collapse every group at
// level n or deeper, expand the shallower ones). Level 8 shows everything.
void showOutlineLevel(SheetOutline &outline, OutlineAxis axis, int level);

// Format ▸ Hide / Unhide over [first, last] (grouped or not).
void setLinesHidden(SheetOutline &outline, OutlineAxis axis, qint32 first, qint32 last, bool hidden);

} // namespace QindaOffice::Calc
