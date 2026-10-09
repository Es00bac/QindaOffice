// SPDX-License-Identifier: GPL-3.0-or-later
// Tables for the renderer: the grid from the column widths, rows grown to
// their text (tableGrid, slide_renderer.h — the canvas hit-tests through
// the same function), merged cells spanning, fills from the cell or else the table
// style's regions (whole table < banded columns < banded rows < last/first
// column < last/first row, PowerPoint's order), borders likewise.

#include "render_parts.h"
#include "slide_renderer.h"
#include "text_painter.h"

#include <QPainter>

namespace QindaOffice::Show {

namespace {

TextBodyStyle cellStyle(const TableCell &cell)
{
    TextBodyStyle style;
    style.insetLeft = cell.marginLeft;
    style.insetRight = cell.marginRight;
    style.insetTop = cell.marginTop;
    style.insetBottom = cell.marginBottom;
    style.anchor = cell.anchor;
    return style;
}

void drawEdge(QPainter *painter, const CellBorder &border, const QPointF &a, const QPointF &b)
{
    if (!border.visible()) {
        return;
    }
    LineStyle line;
    line.dash = border.dash;
    painter->setPen(Render::outlinePen(border.color, border.width, line));
    painter->drawLine(a, b);
}

} // namespace

TableGrid tableGrid(const Table &table, const QRectF &rect)
{
    TableGrid grid;
    const int rows = int(table.rows.size());
    const int columns = int(table.columns.size());
    grid.xs.append(rect.left());
    for (const qreal width : table.columns) {
        grid.xs.append(grid.xs.last() + qMax<qreal>(0.0, width));
    }
    // Rows are at least their height and grow to their tallest single-row
    // cell, as PowerPoint grows them.
    grid.ys.append(rect.top());
    for (int r = 0; r < rows; ++r) {
        const TableRow &row = table.rows.at(r);
        qreal height = row.height;
        for (int c = 0; c < row.cells.size() && c < columns; ++c) {
            const TableCell &cell = row.cells.at(c);
            if (cell.hMerge || cell.vMerge || cell.rowSpan > 1) {
                continue;
            }
            const qreal width = grid.xs.at(qMin(columns, c + qMax(1, cell.gridSpan))) - grid.xs.at(c);
            height = qMax(height, Render::textHeight(cell.paragraphs, width, cellStyle(cell)));
        }
        grid.ys.append(grid.ys.last() + height);
    }
    return grid;
}

QRectF TableGrid::cellRect(int row, int column, int rowSpan, int columnSpan) const
{
    if (row < 0 || column < 0 || row + 1 >= ys.size() || column + 1 >= xs.size()) {
        return {};
    }
    const int lastRow = int(qMin<qsizetype>(ys.size() - 1, row + qMax(1, rowSpan)));
    const int lastColumn = int(qMin<qsizetype>(xs.size() - 1, column + qMax(1, columnSpan)));
    return QRectF(QPointF(xs.at(column), ys.at(row)), QPointF(xs.at(lastColumn), ys.at(lastRow)));
}

namespace Render {

void paintTable(QPainter *painter, const QRectF &rect, const Table &table)
{
    const int rows = int(table.rows.size());
    const int columns = int(table.columns.size());
    if (rows == 0 || columns == 0) {
        paintPlaceholderBox(painter, rect, QStringLiteral("Table"));
        return;
    }
    const TableGrid grid = tableGrid(table, rect);
    const QList<qreal> &xs = grid.xs;
    const QList<qreal> &ys = grid.ys;
    painter->save();
    for (int r = 0; r < rows; ++r) {
        const TableRow &row = table.rows.at(r);
        for (int c = 0; c < row.cells.size() && c < columns; ++c) {
            const TableCell &cell = row.cells.at(c);
            if (cell.hMerge || cell.vMerge) {
                continue;
            }
            const int lastColumn = qMin(columns, c + qMax(1, cell.gridSpan));
            const int lastRow = qMin(rows, r + qMax(1, cell.rowSpan));
            const QRectF box(QPointF(xs.at(c), ys.at(r)), QPointF(xs.at(lastColumn), ys.at(lastRow)));
            const TableRegion region = table.regionAt(r, c);
            QPainterPath area;
            area.addRect(box);
            if (cell.fillStyle.kind != FillKind::Plain) {
                paintFill(painter, area, QColor(), cell.fillStyle);
            } else if (!cell.noFill) {
                paintFill(painter, area, cell.fill.isValid() ? cell.fill : region.fill, FillStyle());
            }
            paintText(painter, box, cell.paragraphs, cellStyle(cell));
        }
    }
    // Borders over every fill: the cell's own, else the style's.
    for (int r = 0; r < rows; ++r) {
        const TableRow &row = table.rows.at(r);
        for (int c = 0; c < row.cells.size() && c < columns; ++c) {
            const TableCell &cell = row.cells.at(c);
            if (cell.hMerge || cell.vMerge) {
                continue;
            }
            const int lastColumn = qMin(columns, c + qMax(1, cell.gridSpan));
            const int lastRow = qMin(rows, r + qMax(1, cell.rowSpan));
            const QRectF box(QPointF(xs.at(c), ys.at(r)), QPointF(xs.at(lastColumn), ys.at(lastRow)));
            const TableRegion region = table.regionAt(r, c);
            const auto pick = [&region](const CellBorder &own, bool outer, bool horizontal) {
                if (own.set) {
                    return own;
                }
                return outer ? region.outer : (horizontal ? region.insideH : region.insideV);
            };
            drawEdge(painter, pick(cell.top, r == 0, true), box.topLeft(), box.topRight());
            drawEdge(painter, pick(cell.bottom, lastRow == rows, true), box.bottomLeft(), box.bottomRight());
            drawEdge(painter, pick(cell.left, c == 0, false), box.topLeft(), box.bottomLeft());
            drawEdge(painter, pick(cell.right, lastColumn == columns, false), box.topRight(), box.bottomRight());
        }
    }
    if (!table.look.known) {
        // An unknown style: a hairline grid keeps the content legible.
        QPen hairline(QColor(160, 160, 160), 0.75);
        painter->setPen(hairline);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(QRectF(QPointF(xs.first(), ys.first()), QPointF(xs.last(), ys.last())));
    }
    painter->restore();
}

} // namespace Render

} // namespace QindaOffice::Show
