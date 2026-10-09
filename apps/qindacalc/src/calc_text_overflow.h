// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Excel's horizontal layout of a cell's text (M82, D-091), shared by the
// grid (spreadsheet_item_paint.cpp) and the printer (calc_print.cpp) so the
// screen and the paper show the same thing:
//  - text runs on over EMPTY neighbours: left-aligned (and General) text to
//    the right, right-aligned text to the left, centred text both ways. It
//    stops at the first cell holding a value or a formula and at a merged
//    range; hidden columns are passed over. A background fill does not stop
//    it (Excel draws the text over a neighbour's fill).
//  - wrapped text, Fill-aligned text (Excel's "Fill", which repeats the text
//    across the cell), a merged cell's text, numbers, booleans and errors
//    never spill. A number too wide for its cell shows #### — a General
//    number first gives up decimal places, then turns scientific, as Excel's
//    General format does.
// Widths are in the caller's units (grid pixels at the zoom, or points on
// paper): column widths come in through a callback and text widths through
// a measuring function, so the same rules serve both devices.
//
// AGENT-CONTRACT: spreadsheet_item_paint.cpp and calc_print.cpp both lay
// text out through here; a rule changed on one side only makes the screen
// and the paper disagree (the M82 grid tests compare them).

#include "cell.h"
#include "sheet.h"

#include <QString>
#include <QtGlobal>

#include <functional>

namespace QindaOffice::Apps::QindaCalc {

namespace CalcEng = QindaOffice::Calc;

// Excel's Fill horizontal alignment (CellStyle::alignment 3, read and
// written as horizontal="fill").
inline constexpr int kAlignFill = 3;

enum class SpillDirection { None, Right, Left, Both };

struct CellHorizontal {
    Qt::Alignment align = Qt::AlignLeft; // AlignLeft, AlignRight or AlignHCenter
    SpillDirection spill = SpillDirection::None;
    bool fill = false;    // the text is repeated across the cell
    bool numeric = false; // a number: it fits, gives up digits, or shows ####
};

// How `value` lays out in a cell styled `style` (nullptr = no style).
// `merged`: the cell anchors a merged range (its text stays inside it).
// General alignment puts numbers right, text left, booleans and errors in
// the middle (Excel's default).
[[nodiscard]] CellHorizontal horizontalLayout(const CalcEng::Value &value, const CalcEng::CellStyle *style,
                                              bool merged);

// Whether the cell at `addr` stops a neighbour's text: it holds a value or
// a formula (a formula showing nothing still stops it), or a merged range
// covers it.
[[nodiscard]] bool stopsSpill(const CalcEng::Sheet &sheet, const CalcEng::CellAddr &addr);

// The columns a text spans: its own cell's, widened over empty neighbours.
struct SpillSpan {
    qint32 first = 0;
    qint32 last = 0;
    [[nodiscard]] bool contains(qint32 col) const { return col >= first && col <= last; }
};

// Column widths in the caller's units; 0 for a hidden column.
using ColumnWidth = std::function<qreal(qint32)>;

// The span a text `need` wide (its insets included) takes from `addr`,
// spilling `direction`. A centred text needs half its overflow on each
// side; a side that is stopped is cut there (the text stays centred, as in
// Excel). `minCol` / `maxCol` bound the search (a pane's columns, or the
// sheet's).
[[nodiscard]] SpillSpan spillSpan(const CalcEng::Sheet &sheet, const CalcEng::CellAddr &addr,
                                  SpillDirection direction, qreal need, const ColumnWidth &width,
                                  qint32 minCol = 0, qint32 maxCol = CalcEng::Sheet::kMaxCols - 1);

// The nearest cell that stops a spill in row `row`, walking from column
// `from` by `step` (-1 left, +1 right) down to `bound` (inclusive), across
// at most `reach` of width — a text further away cannot reach `from`. -1
// when there is none: nothing outside spills in.
[[nodiscard]] qint32 nearestStop(const CalcEng::Sheet &sheet, qint32 row, qint32 from, int step, qint32 bound,
                                 const ColumnWidth &width, qreal reach);

// The text a number shows in `room` (the cell's width less its insets),
// measured by `measure`: `formatted` when it fits; for the General format
// fewer decimal places, then scientific notation (1.2E+07); else as many
// '#' as fit (at least one).
[[nodiscard]] QString fittedNumber(double value, const QString &formatted, bool general, qreal room,
                                   const std::function<qreal(const QString &)> &measure);

// Fill alignment: `text` repeated as many whole times as fit in `room`
// (once when it does not fit at all).
[[nodiscard]] QString filledText(const QString &text, qreal room,
                                 const std::function<qreal(const QString &)> &measure);

} // namespace QindaOffice::Apps::QindaCalc
