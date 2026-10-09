// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Word's table formatting on a QTextTable (M70, D-079): cell shading and
// borders applied the way Word applies them, table, row, column and cell
// properties, AutoFit and Distribute. Every operation sets the Word values
// the writer reads (kTableBorders, kCellBorders, kTableWidth, kRowHeight,
// … in docx_properties.h) and Qt's display of them (per-side cell borders,
// padding, column constraints, row padding) together.
//
// Borders follow Word: a command on some cells writes the sides it sets
// into those cells' own w:tcBorders and gives a neighbour outside the
// selection the same side on the edge they share (Word resolves a shared
// edge between both cells; Qt draws the wider); a command on the whole
// table writes w:tblBorders and drops the cells' own sides it covers.
//
// Not edit blocks: QindaWrite groups each command into one undo step.
// GUI thread. Tables Write did not read from Word are first given Word's
// model (ensureWordTable): collapsed borders, no cell spacing.

#include "docx_borders.h"

#include <QColor>
#include <QList>
#include <QTextCharFormat>
#include <QTextTableFormat>

class QTextCursor;
class QTextDocument;
class QTextTable;
class QTextTableCell;

namespace QindaOffice::Docx {

// A rectangle of grid cells.
struct CellRange {
    int row = 0;
    int column = 0;
    int rows = 0;
    int columns = 0;
    [[nodiscard]] bool isValid() const { return rows > 0 && columns > 0; }
    [[nodiscard]] bool contains(int r, int c) const
    {
        return r >= row && r < row + rows && c >= column && c < column + columns;
    }
    [[nodiscard]] bool coversTable(const QTextTable *table) const;
    bool operator==(const CellRange &) const = default;
};

// The cells `cursor` selects: the rectangle of a selection across cells,
// else the cell it is in (grown to that cell's merge); invalid outside a
// table.
[[nodiscard]] CellRange selectedCells(const QTextCursor &cursor);
// The whole table, one row, one column.
[[nodiscard]] CellRange wholeTable(const QTextTable *table);
// The distinct cells of `range` (a merged cell once), in reading order.
[[nodiscard]] QList<QTextTableCell> cellsIn(const QTextTable *table, const CellRange &range);
// The cell that carries row `row`'s properties (kRowXml, kRowHeight, …):
// the leftmost cell starting in that row.
[[nodiscard]] QTextTableCell rowAnchor(const QTextTable *table, int row);

// Word gives an inserted row or column the formatting of the one beside
// it: the cell at (row, column) takes the format of the cell at (fromRow,
// fromColumn) — shading, borders, its Word markup, the row's properties —
// and its first paragraph that cell's first paragraph format. Merged cells
// keep what Qt gave them. Qt's insertRows/appendRows copy ONE cell's format
// (or none, at the table's end) into every new cell.
void copyNeighbourFormat(QTextTable *table, int row, int column, int fromRow, int fromColumn);

// Gives a table Write did not read from Word Word's model (collapsed
// borders, cell spacing 0, its border as w:tblBorders, its padding as cell
// margins). Returns false when it already had it.
bool ensureWordTable(QTextTable *table);
// The format of a table Write inserts: Word's Insert Table — Table Grid's
// 0.5 pt single black lines, 0.08 in cell margins, `columns` equal fixed
// columns across `contentWidthPx`.
[[nodiscard]] QTextTableFormat newWordTableFormat(int columns, qreal contentWidthPx);
// Gives a freshly inserted table's cells their borders and margins.
void initWordTable(QTextTable *table);

// ---- borders and shading ----------------------------------------------------------

// Word's Borders button: which edges of the selection the pen draws.
enum class BorderTarget { All, Outside, Inside, Top, Bottom, Left, Right, InsideHorizontal, InsideVertical, None };

// Sets the edges of `range` by class — "top", "bottom", "left", "right"
// (the selection's outer edges) and "insideH", "insideV" (between its
// cells) — to the given borders (nil removes); classes absent from `edges`
// are left as they are.
void setRangeBorders(QTextTable *table, const CellRange &range, const WordBorders &edges);
// The Borders button: setRangeBorders with `pen` on the target's classes.
void applyTableBorders(QTextTable *table, const CellRange &range, BorderTarget target, const WordBorder &pen);
// What the Borders and Shading dialog shows for `range`: each edge class
// as the first cell on it draws it (absent: none).
[[nodiscard]] WordBorders rangeBorders(const QTextTable *table, const CellRange &range);

// Cell shading (w:shd w:val="clear" w:color="auto" w:fill="RRGGBB"); an
// invalid colour removes it.
void applyCellShading(QTextTable *table, const CellRange &range, const QColor &fill);
void applyCellVerticalAlignment(QTextTable *table, const CellRange &range,
                                QTextCharFormat::VerticalAlignment alignment);

// ---- properties -------------------------------------------------------------------

struct TableProperties {
    WordWidth width;                    // preferred width
    Qt::Alignment alignment = Qt::AlignLeft;
    int indent = 0;                     // twips from the left margin (left alignment)
    QString layout = QStringLiteral("autofit"); // autofit | fixed
    CellMargins margins;                // default cell margins (twips; -1 = Word's default)
    int headerRows = 0;                 // rows repeated at the top of each page
};
[[nodiscard]] TableProperties tableProperties(const QTextTable *table);
void setTableProperties(QTextTable *table, const TableProperties &properties, qreal contentWidthPx);

struct RowProperties {
    int height = 0;       // twips; 0 = automatic
    bool exact = false;   // "exactly" rather than "at least"
    bool cantSplit = false;
};
[[nodiscard]] RowProperties rowProperties(const QTextTable *table, int row);
void setRowProperties(QTextTable *table, int row, const RowProperties &properties);

// Column widths in twips as laid out (fixed constraints as set).
[[nodiscard]] QList<int> columnWidths(const QTextTable *table, qreal contentWidthPx);
// Fixes column `column` at `twips` (the others keep their widths).
void setColumnWidth(QTextTable *table, int column, int twips, qreal contentWidthPx);

// A cell's own margins (twips; -1 sides follow the table's).
[[nodiscard]] CellMargins cellMargins(const QTextTable *table, int row, int column);
void setCellMargins(QTextTable *table, const CellRange &range, const CellMargins &margins);

enum class AutoFit { Contents, Window, Fixed };
void autoFit(QTextTable *table, AutoFit mode, qreal contentWidthPx);
// Equal widths for the range's columns (their total kept); equal heights
// ("at least" the tallest's average) for its rows.
void distributeColumns(QTextTable *table, const CellRange &range, qreal contentWidthPx);
void distributeRows(QTextTable *table, const CellRange &range);

// Re-pads row `row` up to its Word height (kRowHeight), replacing the
// padding a previous height added (kRowPad, kRowPadTop). Lays the
// document out.
void refreshRowHeight(QTextTable *table, int row);
// The same for every row of `table`, or of every table in `document`
// (nested ones too), laying the document out once: after content changes
// that make rows taller or shorter (a label sheet filled, a merge).
void refreshRowHeights(QTextTable *table);
void refreshRowHeights(QTextDocument *document);
// Replaces the padding a row height put on a cell format with `pad` px (0
// takes it off): on the bottom, split top and bottom for a vertically
// centred cell, on top for a bottom-aligned one, so the content sits as
// the cell's alignment asks.
void setRowPad(QTextTableCellFormat &format, qreal pad);

} // namespace QindaOffice::Docx
