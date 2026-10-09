// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Editing a table (M67, D-076): what PowerPoint's Table Layout and Table
// Design tabs do, on the model alone, so the canvas, the commands, the
// assistant and the tests share one implementation — a new table, insert
// and delete rows and columns, merge and split cells, cell fill and
// borders, column widths and row heights, and the style options (header
// row, banded rows, …).
//
// Every operation keeps the table well formed: one cell per grid column in
// every row; a merged block is an anchor cell (gridSpan × rowSpan) whose
// other cells are hMerge / vMerge, exactly as DrawingML spells it; one
// TableOrigin id per row and column (show_table.h), new rows and columns
// getting fresh ids so the writer can tell them from the source's.
//
// AGENT-NOTE: the model stores EFFECTIVE text formatting (show_text.h), so
// the white bold a table style gives its header row is baked into those
// runs. Whatever moves a cell into another style region (inserting a row
// above the header, switching the header row off) re-derives that look
// (restyleText) — and the pptx writer leaves a run's colour and weight to
// the style whenever they are the style's (pptx_write_table.cpp), so the
// table keeps restyling itself in PowerPoint.
//
// Operations return false and leave the table untouched when they do not
// apply (out of range, deleting the last row or column, splitting a merged
// block into parts that do not divide it).

#include "presentation.h"

namespace QindaOffice::Show {

// Cells by grid position, inclusive on all four sides.
struct TableRange {
    int top = 0;
    int left = 0;
    int bottom = 0;
    int right = 0;
    bool operator==(const TableRange &) const = default;
    [[nodiscard]] bool isValid() const { return top >= 0 && left >= 0 && bottom >= top && right >= left; }
    [[nodiscard]] bool contains(int row, int column) const
    {
        return row >= top && row <= bottom && column >= left && column <= right;
    }
    [[nodiscard]] int rows() const { return bottom - top + 1; }
    [[nodiscard]] int columns() const { return right - left + 1; }
    [[nodiscard]] static TableRange cell(int row, int column) { return {row, column, row, column}; }
};

namespace TableEdit {

enum class Borders { All, Outside, Inside, InsideHorizontal, InsideVertical, Top, Bottom, Left, Right, None };
enum class StyleOption { HeaderRow, TotalRow, BandedRows, FirstColumn, LastColumn, BandedColumns };

// A rows × columns table `width` wide with rows `rowHeight` tall, in the
// given style with PowerPoint's default options (header row, banded rows);
// every cell holds one empty paragraph sized `textSize`.
[[nodiscard]] Table create(int rows, int columns, qreal width, qreal rowHeight, const QString &styleId,
                           const TableLook &look, qreal textSize = 18);

// Resets TableOrigin to one fresh id per row and column when it does not
// match the table (a table that did not come from a .pptx). Every
// operation below calls it first.
void normalizeOrigin(Table &table);

// The merged block holding (row, column): its anchor and spans; the cell
// itself when it is not merged.
[[nodiscard]] TableRange blockAt(const Table &table, int row, int column);
// `range` grown until every merged block it touches lies inside it.
[[nodiscard]] TableRange expandToBlocks(const Table &table, TableRange range);

// `count` rows at `at` (0..rows), formatted like row `like`; a merged
// block the new rows land inside grows over them.
bool insertRows(Table &table, int at, int count, int like);
bool deleteRows(Table &table, int first, int count);
bool insertColumns(Table &table, int at, int count, int like);
bool deleteColumns(Table &table, int first, int count);

// One merged block over `range` (grown to whole blocks); the text of every
// cell moves into the anchor, one paragraph list after the other.
bool merge(Table &table, TableRange range);
// The block at (row, column) as `columns` × `rows` cells: a merged block is
// re-divided (both must divide its spans); a single cell gains grid lines,
// the cells around it spanning them.
bool split(Table &table, int row, int column, int columns, int rows);

// Fill of every cell in `range`: a colour, transparent = no fill, an
// invalid colour = the table style's.
void setFill(Table &table, TableRange range, const QColor &color);
// Borders of `range` (the facing edges of cells next to it follow, as in
// PowerPoint); `pen` is the line, Borders::None draws none.
void setBorders(Table &table, TableRange range, Borders which, const CellBorder &pen);

bool setColumnWidth(Table &table, int column, qreal width);
bool setRowHeight(Table &table, int row, qreal height);
// Moves the boundary between column `column` and the next by `delta`
// points, the next column giving up what this one takes (the outer right
// boundary just widens the last column), never below the minimum width.
bool moveColumnBoundary(Table &table, int column, qreal delta);
inline constexpr qreal kMinimumColumnWidth = 8;
inline constexpr qreal kMinimumRowHeight = 4;

void setStyleOption(Table &table, StyleOption option, bool on);
[[nodiscard]] bool styleOption(const Table &table, StyleOption option);

// Re-derives the run colour and weight a table style gives each region for
// cells that moved into another region between `before` and `after` (cells
// are matched through TableOrigin ids).
void restyleText(Table &after, const Table &before);

// The run formatting text typed into (row, column) takes: the cell's own
// first run, else the nearest run in its row, its column, the table, with
// the region look of this cell; `fallback` when the table holds no text.
[[nodiscard]] TextRun textLook(const Table &table, int row, int column, const TextRun &fallback = TextRun());

// The size the table draws at: the sum of its columns × its rows grown to
// their text. fitFrame() sets a table shape's rect to it (keeping its
// top-left), as PowerPoint keeps a table's frame.
[[nodiscard]] QSizeF naturalSize(const Table &table);
void fitFrame(Shape &shape);

} // namespace TableEdit

} // namespace QindaOffice::Show
