// SPDX-License-Identifier: GPL-3.0-or-later
// A table's look and size (table_edit.h): a new table, cell fills and
// borders, the style options, the region text a table style gives its
// cells, the look of text typed into a cell, column widths and row heights,
// and the size the table draws at.

#include "table_edit.h"

#include "slide_renderer.h"

#include <QHash>

namespace QindaOffice::Show::TableEdit {

namespace {

QColor regionText(const Table &table, const TableRegion &region)
{
    if (region.text.isValid()) {
        return region.text;
    }
    return table.look.whole.text.isValid() ? table.look.whole.text : QColor(Qt::black);
}

// `run` moved from a cell in region `was` to one in region `now`: the
// colour and weight the style gave it follow the new region.
void adapt(TextRun &run, const Table &from, const TableRegion &was, const Table &to, const TableRegion &now)
{
    const QColor wasText = regionText(from, was);
    const QColor nowText = regionText(to, now);
    if (run.color == wasText) {
        run.color = nowText;
    }
    if (was.bold != now.bold && run.bold == was.bold) {
        run.bold = now.bold;
    }
}

QHash<int, int> indexOf(const QList<int> &ids)
{
    QHash<int, int> out;
    for (int i = 0; i < ids.size(); ++i) {
        out.insert(ids.at(i), i);
    }
    return out;
}

TableRange clamped(const Table &table, TableRange range)
{
    range.top = qMax(0, range.top);
    range.left = qMax(0, range.left);
    range.bottom = qMin(int(table.rows.size()) - 1, range.bottom);
    range.right = qMin(int(table.columns.size()) - 1, range.right);
    return range;
}

} // namespace

Table create(int rows, int columns, qreal width, qreal rowHeight, const QString &styleId, const TableLook &look,
             qreal textSize)
{
    Table table;
    rows = qBound(1, rows, 100);
    columns = qBound(1, columns, 63);
    for (int c = 0; c < columns; ++c) {
        table.columns.append(qMax(kMinimumColumnWidth, width / columns));
    }
    TableCell cell;
    TextParagraph empty;
    empty.alignment = int(Qt::AlignLeft); // as the reader reads a written one back
    empty.endPointSize = textSize;
    cell.paragraphs = {empty};
    for (int r = 0; r < rows; ++r) {
        TableRow row;
        row.height = qMax(kMinimumRowHeight, rowHeight);
        for (int c = 0; c < columns; ++c) {
            row.cells.append(cell);
        }
        table.rows.append(row);
    }
    table.styleId = styleId;
    table.look = look;
    // PowerPoint's defaults for a new table: Header Row and Banded Rows.
    table.firstRow = true;
    table.bandRows = true;
    normalizeOrigin(table);
    return table;
}

void setFill(Table &table, TableRange range, const QColor &color)
{
    range = expandToBlocks(table, clamped(table, range));
    if (!range.isValid()) {
        return;
    }
    for (int r = range.top; r <= range.bottom; ++r) {
        for (int c = range.left; c <= range.right && c < table.rows.at(r).cells.size(); ++c) {
            TableCell &cell = table.rows[r].cells[c];
            cell.fillStyle = FillStyle();
            cell.noFill = color.isValid() && color.alpha() == 0;
            cell.fill = color.isValid() && color.alpha() > 0 ? color : QColor();
        }
    }
}

void setBorders(Table &table, TableRange range, Borders which, const CellBorder &pen)
{
    range = expandToBlocks(table, clamped(table, range));
    if (!range.isValid()) {
        return;
    }
    CellBorder line = pen;
    line.set = true;
    CellBorder none;
    none.set = true; // an explicit "no line": the style's line is off too
    const CellBorder &value = which == Borders::None ? none : line;
    // Which edges of the range the command draws: outer ones by side, and
    // the inner ones between blocks.
    const auto wanted = [which](bool outer, bool horizontal, Borders side) {
        switch (which) {
        case Borders::All:
        case Borders::None:
            return true;
        case Borders::Outside:
            return outer;
        case Borders::Inside:
            return !outer;
        case Borders::InsideHorizontal:
            return !outer && horizontal;
        case Borders::InsideVertical:
            return !outer && !horizontal;
        case Borders::Top:
        case Borders::Bottom:
        case Borders::Left:
        case Borders::Right:
            return outer && side == which;
        }
        return false;
    };
    const int rows = int(table.rows.size());
    const int columns = int(table.columns.size());
    for (int r = range.top; r <= range.bottom; ++r) {
        for (int c = range.left; c <= range.right && c < table.rows.at(r).cells.size(); ++c) {
            const TableRange b = blockAt(table, r, c);
            TableCell &cell = table.rows[r].cells[c];
            // Only a block's own edges are lines; a merged block has no
            // inner ones.
            if (r == b.top && wanted(b.top == range.top, true, Borders::Top)) {
                cell.top = value;
            }
            if (r == b.bottom && wanted(b.bottom == range.bottom, true, Borders::Bottom)) {
                cell.bottom = value;
            }
            if (c == b.left && wanted(b.left == range.left, false, Borders::Left)) {
                cell.left = value;
            }
            if (c == b.right && wanted(b.right == range.right, false, Borders::Right)) {
                cell.right = value;
            }
        }
    }
    // The cells across an outer edge face the same line (PowerPoint writes
    // both sides of a shared edge).
    for (int c = range.left; c <= range.right; ++c) {
        if (range.top > 0 && wanted(true, true, Borders::Top) && c < table.rows.at(range.top - 1).cells.size()) {
            table.rows[range.top - 1].cells[c].bottom = value;
        }
        if (range.bottom + 1 < rows && wanted(true, true, Borders::Bottom) && c < table.rows.at(range.bottom + 1).cells.size()) {
            table.rows[range.bottom + 1].cells[c].top = value;
        }
    }
    for (int r = range.top; r <= range.bottom; ++r) {
        TableRow &row = table.rows[r];
        if (range.left > 0 && wanted(true, false, Borders::Left) && range.left - 1 < row.cells.size()) {
            row.cells[range.left - 1].right = value;
        }
        if (range.right + 1 < columns && wanted(true, false, Borders::Right) && range.right + 1 < row.cells.size()) {
            row.cells[range.right + 1].left = value;
        }
    }
}

bool styleOption(const Table &table, StyleOption option)
{
    switch (option) {
    case StyleOption::HeaderRow:
        return table.firstRow;
    case StyleOption::TotalRow:
        return table.lastRow;
    case StyleOption::BandedRows:
        return table.bandRows;
    case StyleOption::FirstColumn:
        return table.firstColumn;
    case StyleOption::LastColumn:
        return table.lastColumn;
    case StyleOption::BandedColumns:
        return table.bandColumns;
    }
    return false;
}

void setStyleOption(Table &table, StyleOption option, bool on)
{
    normalizeOrigin(table);
    const Table before = table;
    bool *flag = nullptr;
    switch (option) {
    case StyleOption::HeaderRow:
        flag = &table.firstRow;
        break;
    case StyleOption::TotalRow:
        flag = &table.lastRow;
        break;
    case StyleOption::BandedRows:
        flag = &table.bandRows;
        break;
    case StyleOption::FirstColumn:
        flag = &table.firstColumn;
        break;
    case StyleOption::LastColumn:
        flag = &table.lastColumn;
        break;
    case StyleOption::BandedColumns:
        flag = &table.bandColumns;
        break;
    }
    if (flag == nullptr || *flag == on) {
        return;
    }
    *flag = on;
    restyleText(table, before);
}

void restyleText(Table &after, const Table &before)
{
    if (before.origin.rows.size() != before.rows.size() || before.origin.columns.size() != before.columns.size()
        || after.origin.rows.size() != after.rows.size() || after.origin.columns.size() != after.columns.size()) {
        return;
    }
    const QHash<int, int> rowsBefore = indexOf(before.origin.rows);
    const QHash<int, int> columnsBefore = indexOf(before.origin.columns);
    for (int r = 0; r < after.rows.size(); ++r) {
        const int r0 = rowsBefore.value(after.origin.rows.at(r), -1);
        for (int c = 0; c < after.rows.at(r).cells.size() && c < after.columns.size(); ++c) {
            const int c0 = columnsBefore.value(after.origin.columns.at(c), -1);
            if (r0 < 0 || c0 < 0) {
                continue; // a new cell: no text of its own yet
            }
            const TableRegion was = before.regionAt(r0, c0);
            const TableRegion now = after.regionAt(r, c);
            if (regionText(before, was) == regionText(after, now) && was.bold == now.bold) {
                continue;
            }
            for (TextParagraph &paragraph : after.rows[r].cells[c].paragraphs) {
                for (TextRun &run : paragraph.runs) {
                    adapt(run, before, was, after, now);
                }
            }
        }
    }
}

TextRun textLook(const Table &table, int row, int column, const TextRun &fallback)
{
    const int rows = int(table.rows.size());
    const int columns = int(table.columns.size());
    const auto firstRun = [&table](int r, int c) -> const TextRun * {
        if (r < 0 || c < 0 || r >= table.rows.size() || c >= table.rows.at(r).cells.size()) {
            return nullptr;
        }
        for (const TextParagraph &p : table.rows.at(r).cells.at(c).paragraphs) {
            for (const TextRun &run : p.runs) {
                if (!run.text.isEmpty()) {
                    return &run;
                }
            }
        }
        return nullptr;
    };
    // The cell itself, then its row and its column outwards, then any cell.
    QList<std::pair<int, int>> order{{row, column}};
    for (int d = 1; d < qMax(rows, columns); ++d) {
        order += {{row, column - d}, {row, column + d}};
    }
    for (int d = 1; d < rows; ++d) {
        order += {{row - d, column}, {row + d, column}};
    }
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < columns; ++c) {
            order.append({r, c});
        }
    }
    const TableRegion now = table.regionAt(row, column);
    TextRun look = fallback;
    bool found = false;
    for (const auto &[r, c] : order) {
        if (const TextRun *run = firstRun(r, c)) {
            look = *run;
            adapt(look, table, table.regionAt(r, c), table, now);
            found = true;
            break;
        }
    }
    if (!found) {
        look.color = regionText(table, now);
        look.bold = now.bold;
    }
    look.text.clear();
    look.hyperlink.clear();
    look.fieldType.clear();
    look.fieldId.clear();
    // An empty paragraph remembers the size typing into it takes.
    if (row >= 0 && row < rows && column >= 0 && column < table.rows.at(row).cells.size() && firstRun(row, column) == nullptr) {
        const QList<TextParagraph> &paragraphs = table.rows.at(row).cells.at(column).paragraphs;
        if (!paragraphs.isEmpty() && paragraphs.first().endPointSize > 0) {
            look.pointSize = paragraphs.first().endPointSize;
        }
    }
    return look;
}

bool setColumnWidth(Table &table, int column, qreal width)
{
    if (column < 0 || column >= int(table.columns.size())) {
        return false;
    }
    table.columns[column] = qMax(kMinimumColumnWidth, width);
    return true;
}

bool setRowHeight(Table &table, int row, qreal height)
{
    if (row < 0 || row >= int(table.rows.size())) {
        return false;
    }
    table.rows[row].height = qMax(kMinimumRowHeight, height);
    return true;
}

bool moveColumnBoundary(Table &table, int column, qreal delta)
{
    const int columns = int(table.columns.size());
    if (column < 0 || column >= columns) {
        return false;
    }
    if (column == columns - 1) {
        table.columns[column] = qMax(kMinimumColumnWidth, table.columns.at(column) + delta);
        return true;
    }
    const qreal total = table.columns.at(column) + table.columns.at(column + 1);
    if (total < 2 * kMinimumColumnWidth) {
        return false;
    }
    const qreal left = qBound(kMinimumColumnWidth, table.columns.at(column) + delta, total - kMinimumColumnWidth);
    table.columns[column] = left;
    table.columns[column + 1] = total - left;
    return true;
}

QSizeF naturalSize(const Table &table)
{
    const TableGrid grid = tableGrid(table, QRectF(0, 0, 0, 0));
    return QSizeF(grid.xs.isEmpty() ? 0 : grid.xs.last(), grid.ys.isEmpty() ? 0 : grid.ys.last());
}

void fitFrame(Shape &shape)
{
    if (shape.kind != ShapeKind::Table || shape.table.isEmpty()) {
        return;
    }
    shape.rect = QRectF(shape.rect.normalized().topLeft(), naturalSize(shape.table));
}

} // namespace QindaOffice::Show::TableEdit
