// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Tables (DrawingML a:tbl inside a graphic frame; M62, D-070): the grid's
// column widths and row heights, cells with their text, fill, borders and
// merges, and the table style the cells fall back to.
//
// AGENT-NOTE: a cell's own fill/borders are only what the file spells out
// on that cell (a:tcPr). Everything else comes from the table style
// (tableStyles.xml, or PowerPoint's built-in style by GUID), which the
// reader resolves into TableLook once — so the renderer needs no theme, and
// the writer never bakes style colours into cells (a restyled table in
// PowerPoint still restyles).

#include "show_style.h"
#include "show_text.h"

namespace QindaOffice::Show {

struct CellBorder {
    bool set = false; // false = the table style decides
    QColor color;     // invalid with `set` = explicitly no line
    qreal width = 0;  // pt
    QString dash;
    bool operator==(const CellBorder &) const = default;
    [[nodiscard]] bool visible() const { return color.isValid() && color.alpha() > 0 && width > 0; }
};

struct TableCell {
    QList<TextParagraph> paragraphs;
    qreal marginLeft = 7.2; // pt (a:tcPr/@marL …, DrawingML defaults)
    qreal marginRight = 7.2;
    qreal marginTop = 3.6;
    qreal marginBottom = 3.6;
    TextAnchor anchor = TextAnchor::Top;
    QColor fill;          // invalid = the table style's
    bool noFill = false;  // an explicit a:noFill
    FillStyle fillStyle;  // gradient/picture cells (kind Plain = `fill`)
    CellBorder left, right, top, bottom;
    int gridSpan = 1;     // columns this cell covers
    int rowSpan = 1;      // rows this cell covers
    bool hMerge = false;  // covered by the cell to the left
    bool vMerge = false;  // covered by the cell above
    bool operator==(const TableCell &) const = default;
};

struct TableRow {
    qreal height = 0; // pt (a minimum: text may grow it)
    QList<TableCell> cells;
    bool operator==(const TableRow &) const = default;
};

// One region of a table style (whole table, banded row, header row, …).
struct TableRegion {
    QColor fill;      // invalid = none
    QColor text;      // invalid = keep the run's colour
    bool bold = false;
    CellBorder insideH, insideV, outer;
    bool operator==(const TableRegion &) const = default;
};

// A resolved table style.
struct TableLook {
    bool known = false;
    TableRegion whole, band1H, band2H, band1V, band2V, firstRow, lastRow, firstCol, lastCol;
    bool operator==(const TableLook &) const = default;
};

struct Table {
    QList<qreal> columns; // pt
    QList<TableRow> rows;
    QString styleId;      // a:tableStyleId (a GUID)
    bool firstRow = false;
    bool firstColumn = false;
    bool lastRow = false;
    bool lastColumn = false;
    bool bandRows = false;
    bool bandColumns = false;
    TableLook look;
    bool operator==(const Table &) const = default;
    [[nodiscard]] bool isEmpty() const { return rows.isEmpty(); }
    [[nodiscard]] int columnCount() const { return int(columns.size()); }
    // The table style in force at a cell: whole table < banded columns <
    // banded rows < last/first column < last/first row (PowerPoint's
    // order), as switched on by the flags above.
    [[nodiscard]] TableRegion regionAt(int row, int column) const;
};

} // namespace QindaOffice::Show
