// SPDX-License-Identifier: GPL-3.0-or-later
// Table structure (table_edit.h): merged blocks, rows and columns in and
// out, merge and split. The look and size half — a new table, fills,
// borders, style options, region text, column widths and row heights — is
// table_edit_look.cpp.
//
// AGENT-NOTE: merges are edited as a list of blocks (anchor + spans) and
// written back onto the cells by applyBlocks(), never by flipping hMerge /
// vMerge flags in place: an insert or delete moves block edges, and the
// flags of every cell they cover follow from the blocks alone.

#include "table_edit.h"

#include <QHash>

namespace QindaOffice::Show::TableEdit {

namespace {

int rowCount(const Table &t)
{
    return int(t.rows.size());
}

int columnCount(const Table &t)
{
    return int(t.columns.size());
}

bool hasText(const TableCell &cell)
{
    for (const TextParagraph &p : cell.paragraphs) {
        for (const TextRun &r : p.runs) {
            if (!r.text.isEmpty()) {
                return true;
            }
        }
    }
    return false;
}

// One empty paragraph with the cell's first paragraph's properties, sized
// like its first run, so typing into it continues the cell's look.
TextParagraph emptyLike(const TableCell &cell)
{
    TextParagraph p;
    qreal size = 0;
    if (!cell.paragraphs.isEmpty()) {
        p = cell.paragraphs.first();
        size = p.endPointSize;
        for (const TextParagraph &paragraph : cell.paragraphs) {
            for (const TextRun &run : paragraph.runs) {
                if (size <= 0 || !run.text.isEmpty()) {
                    size = run.pointSize;
                }
                if (!run.text.isEmpty()) {
                    break;
                }
            }
        }
    }
    p.runs.clear();
    p.endPointSize = size > 0 ? size : 18;
    return p;
}

// A cell formatted like `cell` (margins, anchor, fill, borders) and empty.
TableCell blankLike(const TableCell &cell)
{
    TableCell out = cell;
    out.paragraphs = {emptyLike(cell)};
    out.gridSpan = 1;
    out.rowSpan = 1;
    out.hMerge = false;
    out.vMerge = false;
    return out;
}

// One cell per grid column in every row.
void squareUp(Table &t)
{
    const int columns = columnCount(t);
    for (TableRow &row : t.rows) {
        while (row.cells.size() < columns) {
            row.cells.append(row.cells.isEmpty() ? blankLike(TableCell()) : blankLike(row.cells.last()));
        }
        while (row.cells.size() > columns) {
            row.cells.removeLast();
        }
    }
}

// The merged blocks (anchors spanning more than one cell), clamped to the grid.
QList<TableRange> blocks(const Table &t)
{
    QList<TableRange> out;
    const int rows = rowCount(t);
    const int columns = columnCount(t);
    for (int r = 0; r < rows; ++r) {
        const TableRow &row = t.rows.at(r);
        for (int c = 0; c < row.cells.size() && c < columns; ++c) {
            const TableCell &cell = row.cells.at(c);
            if (cell.hMerge || cell.vMerge || (cell.gridSpan <= 1 && cell.rowSpan <= 1)) {
                continue;
            }
            out.append({r, c, qMin(rows - 1, r + qMax(1, cell.rowSpan) - 1), qMin(columns - 1, c + qMax(1, cell.gridSpan) - 1)});
        }
    }
    return out;
}

// Rewrites every cell's spans and merge flags from `merged`.
void applyBlocks(Table &t, const QList<TableRange> &merged)
{
    for (TableRow &row : t.rows) {
        for (TableCell &cell : row.cells) {
            cell.gridSpan = 1;
            cell.rowSpan = 1;
            cell.hMerge = false;
            cell.vMerge = false;
        }
    }
    for (const TableRange &b : merged) {
        if (!b.isValid() || b.rows() * b.columns() < 2 || b.bottom >= rowCount(t) || b.right >= columnCount(t)) {
            continue;
        }
        for (int r = b.top; r <= b.bottom; ++r) {
            for (int c = b.left; c <= b.right; ++c) {
                TableCell &cell = t.rows[r].cells[c];
                if (r == b.top && c == b.left) {
                    cell.gridSpan = b.columns();
                    cell.rowSpan = b.rows();
                } else {
                    cell.hMerge = c > b.left;
                    cell.vMerge = r > b.top;
                }
            }
        }
    }
}

bool intersects(const TableRange &a, const TableRange &b)
{
    return a.left <= b.right && b.left <= a.right && a.top <= b.bottom && b.top <= a.bottom;
}

bool inside(const TableRange &inner, const TableRange &outer)
{
    return inner.top >= outer.top && inner.bottom <= outer.bottom && inner.left >= outer.left && inner.right <= outer.right;
}

QList<int> freshIds(const QList<int> &ids, int sourceCount, int count)
{
    int next = sourceCount;
    for (const int id : ids) {
        next = qMax(next, id + 1);
    }
    QList<int> out;
    for (int i = 0; i < count; ++i) {
        out.append(next++);
    }
    return out;
}

void insertIds(QList<int> &ids, int sourceCount, int at, int count)
{
    const QList<int> fresh = freshIds(ids, sourceCount, count);
    for (int i = 0; i < count; ++i) {
        ids.insert(at + i, fresh.at(i));
    }
}

bool coveredBy(const QList<TableRange> &merged, int row, int column)
{
    for (const TableRange &b : merged) {
        if (b.contains(row, column)) {
            return true;
        }
    }
    return false;
}

// Inserts `count` grid columns at `at` (0..columns) like column `like`,
// with blocks shifted; blocks the new columns land inside grow over them.
void insertGridColumns(Table &t, QList<TableRange> &merged, int at, int count, int like, qreal width)
{
    for (TableRange &b : merged) {
        if (b.left < at && at <= b.right) {
            b.right += count;
        } else if (b.left >= at) {
            b.left += count;
            b.right += count;
        }
    }
    const QList<TableRange> before = blocks(t);
    for (int r = 0; r < rowCount(t); ++r) {
        // The new cells look like the anchor of the block at the template.
        TableRange source = TableRange::cell(r, like);
        for (const TableRange &b : before) {
            if (b.contains(r, like)) {
                source = b;
            }
        }
        const TableCell look = blankLike(t.rows.at(source.top).cells.at(source.left));
        for (int i = 0; i < count; ++i) {
            t.rows[r].cells.insert(at, look);
        }
    }
    for (int i = 0; i < count; ++i) {
        t.columns.insert(at, width);
    }
    insertIds(t.origin.columns, t.origin.sourceColumns, at, count);
}

void insertGridRows(Table &t, QList<TableRange> &merged, int at, int count, int like, qreal height)
{
    for (TableRange &b : merged) {
        if (b.top < at && at <= b.bottom) {
            b.bottom += count;
        } else if (b.top >= at) {
            b.top += count;
            b.bottom += count;
        }
    }
    const QList<TableRange> before = blocks(t);
    TableRow fresh;
    fresh.height = height;
    for (int c = 0; c < columnCount(t); ++c) {
        TableRange source = TableRange::cell(like, c);
        for (const TableRange &b : before) {
            if (b.contains(like, c)) {
                source = b;
            }
        }
        fresh.cells.append(blankLike(t.rows.at(source.top).cells.at(source.left)));
    }
    for (int i = 0; i < count; ++i) {
        t.rows.insert(at, fresh);
    }
    insertIds(t.origin.rows, t.origin.sourceRows, at, count);
}

} // namespace

void normalizeOrigin(Table &table)
{
    TableOrigin &o = table.origin;
    if (o.rows.size() == table.rows.size() && o.columns.size() == table.columns.size()) {
        return;
    }
    o = TableOrigin();
    for (int r = 0; r < rowCount(table); ++r) {
        o.rows.append(r);
    }
    for (int c = 0; c < columnCount(table); ++c) {
        o.columns.append(c);
    }
}

TableRange blockAt(const Table &table, int row, int column)
{
    for (const TableRange &b : blocks(table)) {
        if (b.contains(row, column)) {
            return b;
        }
    }
    return TableRange::cell(row, column);
}

TableRange expandToBlocks(const Table &table, TableRange range)
{
    const QList<TableRange> merged = blocks(table);
    for (bool grew = true; grew;) {
        grew = false;
        for (const TableRange &b : merged) {
            if (intersects(b, range) && !inside(b, range)) {
                range = {qMin(range.top, b.top), qMin(range.left, b.left), qMax(range.bottom, b.bottom), qMax(range.right, b.right)};
                grew = true;
            }
        }
    }
    return range;
}

bool insertRows(Table &table, int at, int count, int like)
{
    const int rows = rowCount(table);
    if (count < 1 || rows == 0 || columnCount(table) == 0 || at < 0 || at > rows) {
        return false;
    }
    normalizeOrigin(table);
    squareUp(table);
    const Table before = table;
    like = qBound(0, like, rows - 1);
    QList<TableRange> merged = blocks(table);
    insertGridRows(table, merged, at, count, like, table.rows.at(like).height);
    applyBlocks(table, merged);
    restyleText(table, before);
    return true;
}

bool deleteRows(Table &table, int first, int count)
{
    const int rows = rowCount(table);
    if (count < 1 || first < 0 || first + count > rows || count >= rows) {
        return false;
    }
    normalizeOrigin(table);
    squareUp(table);
    const Table before = table;
    const int last = first + count - 1;
    QList<TableRange> merged;
    for (TableRange b : blocks(table)) {
        const int above = qMax(0, qMin(b.bottom, first - 1) - b.top + 1);
        const int below = qMax(0, b.bottom - qMax(b.top, last + 1) + 1);
        if (above + below == 0) {
            continue; // the whole block goes
        }
        if (b.top >= first && b.top <= last) {
            // Its anchor row goes: the anchor's content moves down to the
            // first row of the block that stays.
            table.rows[last + 1].cells[b.left] = table.rows.at(b.top).cells.at(b.left);
        }
        b.top = b.top < first ? b.top : (b.top > last ? b.top - count : first);
        b.bottom = b.top + above + below - 1;
        merged.append(b);
    }
    table.rows.remove(first, count);
    table.origin.rows.remove(first, count);
    applyBlocks(table, merged);
    restyleText(table, before);
    return true;
}

bool insertColumns(Table &table, int at, int count, int like)
{
    const int columns = columnCount(table);
    if (count < 1 || columns == 0 || rowCount(table) == 0 || at < 0 || at > columns) {
        return false;
    }
    normalizeOrigin(table);
    squareUp(table);
    const Table before = table;
    like = qBound(0, like, columns - 1);
    QList<TableRange> merged = blocks(table);
    insertGridColumns(table, merged, at, count, like, table.columns.at(like));
    applyBlocks(table, merged);
    restyleText(table, before);
    return true;
}

bool deleteColumns(Table &table, int first, int count)
{
    const int columns = columnCount(table);
    if (count < 1 || first < 0 || first + count > columns || count >= columns) {
        return false;
    }
    normalizeOrigin(table);
    squareUp(table);
    const Table before = table;
    const int last = first + count - 1;
    QList<TableRange> merged;
    for (TableRange b : blocks(table)) {
        const int left = qMax(0, qMin(b.right, first - 1) - b.left + 1);
        const int right = qMax(0, b.right - qMax(b.left, last + 1) + 1);
        if (left + right == 0) {
            continue;
        }
        if (b.left >= first && b.left <= last) {
            table.rows[b.top].cells[last + 1] = table.rows.at(b.top).cells.at(b.left);
        }
        b.left = b.left < first ? b.left : (b.left > last ? b.left - count : first);
        b.right = b.left + left + right - 1;
        merged.append(b);
    }
    for (TableRow &row : table.rows) {
        row.cells.remove(first, count);
    }
    table.columns.remove(first, count);
    table.origin.columns.remove(first, count);
    applyBlocks(table, merged);
    restyleText(table, before);
    return true;
}

bool merge(Table &table, TableRange range)
{
    if (!range.isValid() || range.bottom >= rowCount(table) || range.right >= columnCount(table)) {
        return false;
    }
    normalizeOrigin(table);
    squareUp(table);
    range = expandToBlocks(table, range);
    if (range.rows() * range.columns() < 2) {
        return false;
    }
    QList<TableRange> merged;
    for (const TableRange &b : blocks(table)) {
        if (!inside(b, range)) {
            merged.append(b);
        }
    }
    merged.append(range);
    // PowerPoint keeps every cell's text, in reading order, in the anchor.
    QList<TextParagraph> text;
    for (int r = range.top; r <= range.bottom; ++r) {
        for (int c = range.left; c <= range.right; ++c) {
            const TableCell &cell = table.rows.at(r).cells.at(c);
            if (!cell.hMerge && !cell.vMerge && hasText(cell)) {
                text += cell.paragraphs;
            }
        }
    }
    for (int r = range.top; r <= range.bottom; ++r) {
        for (int c = range.left; c <= range.right; ++c) {
            if (r != range.top || c != range.left) {
                TableCell &cell = table.rows[r].cells[c];
                cell.paragraphs = {emptyLike(cell)};
            }
        }
    }
    if (!text.isEmpty()) {
        table.rows[range.top].cells[range.left].paragraphs = text;
    }
    applyBlocks(table, merged);
    return true;
}

bool split(Table &table, int row, int column, int columns, int rows)
{
    if (columns < 1 || rows < 1 || (columns == 1 && rows == 1) || row < 0 || column < 0 || row >= rowCount(table)
        || column >= columnCount(table) || columns > 64 || rows > 64) {
        return false;
    }
    normalizeOrigin(table);
    squareUp(table);
    const TableRange block = blockAt(table, row, column);
    QList<TableRange> merged = blocks(table);
    if (block.rows() > 1 || block.columns() > 1) {
        // A merged block is re-divided into equal parts.
        if (block.columns() % columns != 0 || block.rows() % rows != 0) {
            return false;
        }
        const int w = block.columns() / columns;
        const int h = block.rows() / rows;
        merged.removeAll(block);
        const TableCell anchor = table.rows.at(block.top).cells.at(block.left);
        for (int i = 0; i < rows; ++i) {
            for (int j = 0; j < columns; ++j) {
                const TableRange part{block.top + i * h, block.left + j * w, block.top + (i + 1) * h - 1,
                                      block.left + (j + 1) * w - 1};
                merged.append(part);
                if (i > 0 || j > 0) {
                    table.rows[part.top].cells[part.left] = blankLike(anchor);
                }
            }
        }
        applyBlocks(table, merged);
        return true;
    }
    // A single cell: new grid lines through it; every other cell they
    // cross spans them, as in PowerPoint.
    const Table before = table;
    if (columns > 1) {
        const int extra = columns - 1;
        const qreal width = table.columns.at(column) / columns;
        table.columns[column] = width;
        insertGridColumns(table, merged, column + 1, extra, column, width);
        // A block ending at the split column spans the new ones too.
        for (TableRange &b : merged) {
            if (b.left <= column && b.right == column) {
                b.right += extra;
            }
        }
        for (int r = 0; r < rowCount(table); ++r) {
            if (r != row && !coveredBy(merged, r, column)) {
                merged.append({r, column, r, column + extra});
            }
        }
        applyBlocks(table, merged);
    }
    if (rows > 1) {
        const int extra = rows - 1;
        const qreal height = table.rows.at(row).height / rows;
        table.rows[row].height = height;
        insertGridRows(table, merged, row + 1, extra, row, height);
        for (TableRange &b : merged) {
            if (b.top <= row && b.bottom == row) {
                b.bottom += extra;
            }
        }
        for (int c = 0; c < columnCount(table); ++c) {
            const bool splitCell = c >= column && c < column + columns;
            if (!splitCell && !coveredBy(merged, row, c)) {
                merged.append({row, c, row + extra, c});
            }
        }
    }
    applyBlocks(table, merged);
    restyleText(table, before);
    return true;
}

} // namespace QindaOffice::Show::TableEdit
