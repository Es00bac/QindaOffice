// SPDX-License-Identifier: GPL-3.0-or-later
// Word's table borders and shading on a QTextTable (docx_table_format.h);
// properties, widths, AutoFit and row heights: docx_table_props.cpp.

#include "docx_table_format.h"

#include "docx_properties.h"
#include "ooxml_units.h"

#include <QSet>
#include <QTextCursor>
#include <QTextTable>

#include <optional>

namespace QindaOffice::Docx {

namespace Units = Ooxml::Units;

namespace {

const QString kTop = QStringLiteral("top");
const QString kBottom = QStringLiteral("bottom");
const QString kLeft = QStringLiteral("left");
const QString kRight = QStringLiteral("right");
const QString kInsideH = QStringLiteral("insideH");
const QString kInsideV = QStringLiteral("insideV");

// The table-level borders in effect (kTableBorders: the style's with the
// table's own over them, as the reader merged them).
WordBorders tableLevel(const QTextTable *table)
{
    return parseBordersSpec(table->format().stringProperty(kTableBorders));
}

// The table-level side a cell's `side` takes by position.
QString tableSideFor(const QTextTable *table, const QTextTableCell &cell, const QString &side)
{
    if (side == kTop) {
        return cell.row() == 0 ? kTop : kInsideH;
    }
    if (side == kBottom) {
        return cell.row() + cell.rowSpan() >= table->rows() ? kBottom : kInsideH;
    }
    if (side == kLeft) {
        return cell.column() == 0 ? kLeft : kInsideV;
    }
    return cell.column() + cell.columnSpan() >= table->columns() ? kRight : kInsideV;
}

void showSide(QTextTableCellFormat &f, const QString &side, const WordBorder &border)
{
    const DisplayBorder d = displayBorder(border);
    if (side == kTop) {
        f.setTopBorder(d.width);
        f.setTopBorderStyle(d.style);
        f.setTopBorderBrush(d.color);
    } else if (side == kBottom) {
        f.setBottomBorder(d.width);
        f.setBottomBorderStyle(d.style);
        f.setBottomBorderBrush(d.color);
    } else if (side == kLeft) {
        f.setLeftBorder(d.width);
        f.setLeftBorderStyle(d.style);
        f.setLeftBorderBrush(d.color);
    } else {
        f.setRightBorder(d.width);
        f.setRightBorderStyle(d.style);
        f.setRightBorderBrush(d.color);
    }
}

// Sets (or, with nullopt, drops) the cell's own sides and shows what the
// cell then draws there; one format change per cell.
using SideChanges = QMap<QString, std::optional<WordBorder>>;
void setOwnSides(QTextTable *table, QTextTableCell cell, const SideChanges &changes, const WordBorders &tableBorders)
{
    if (changes.isEmpty()) {
        return;
    }
    QTextTableCellFormat f = cell.format().toTableCellFormat();
    WordBorders own = parseBordersSpec(f.stringProperty(kCellBorders));
    for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
        if (it.value()) {
            own.insert(it.key(), *it.value());
        } else {
            own.remove(it.key());
        }
        const WordBorder shown = it.value() ? *it.value()
                                            : tableBorders.value(tableSideFor(table, cell, it.key()), WordBorder::none());
        showSide(f, it.key(), shown);
    }
    f.setProperty(kCellBorders, bordersSpec(own));
    cell.setFormat(f);
}

WordBorder shownSide(const QTextTable *table, const QTextTableCell &cell, const QString &side, const WordBorders &tableBorders)
{
    const WordBorders own = parseBordersSpec(cell.format().stringProperty(kCellBorders));
    if (own.contains(side)) {
        return own.value(side);
    }
    return tableBorders.value(tableSideFor(table, cell, side), WordBorder::none());
}

// The cells across `side` of `cell` (one per grid column or row it spans).
QList<QTextTableCell> neighbours(const QTextTable *table, const QTextTableCell &cell, const QString &side)
{
    QList<QTextTableCell> out;
    QSet<int> seen;
    auto add = [&](int r, int c) {
        if (r < 0 || c < 0 || r >= table->rows() || c >= table->columns()) {
            return;
        }
        const QTextTableCell n = table->cellAt(r, c);
        if (n.isValid() && n.firstPosition() != cell.firstPosition() && !seen.contains(n.firstPosition())) {
            seen.insert(n.firstPosition());
            out.append(n);
        }
    };
    for (int k = 0; k < (side == kTop || side == kBottom ? cell.columnSpan() : cell.rowSpan()); ++k) {
        if (side == kTop) {
            add(cell.row() - 1, cell.column() + k);
        } else if (side == kBottom) {
            add(cell.row() + cell.rowSpan(), cell.column() + k);
        } else if (side == kLeft) {
            add(cell.row() + k, cell.column() - 1);
        } else {
            add(cell.row() + k, cell.column() + cell.columnSpan());
        }
    }
    return out;
}

QString opposite(const QString &side)
{
    return side == kTop ? kBottom : side == kBottom ? kTop : side == kLeft ? kRight : kLeft;
}

WordBorder pixelBorder(qreal widthPx, const QBrush &brush)
{
    if (widthPx <= 0) {
        return WordBorder::none();
    }
    WordBorder b;
    b.size = qBound(2, qRound(Units::pxToPt(widthPx) * 8), 96);
    b.color = brush.style() != Qt::NoBrush && brush.color() != Qt::black ? brush.color().name().mid(1).toUpper()
                                                                           : QStringLiteral("auto");
    return b;
}

} // namespace

bool CellRange::coversTable(const QTextTable *table) const
{
    return table != nullptr && row == 0 && column == 0 && rows >= table->rows() && columns >= table->columns();
}

CellRange selectedCells(const QTextCursor &cursor)
{
    QTextTable *table = cursor.currentTable();
    if (table == nullptr) {
        return {};
    }
    if (cursor.hasComplexSelection()) {
        int row = 0;
        int rows = 0;
        int column = 0;
        int columns = 0;
        cursor.selectedTableCells(&row, &rows, &column, &columns);
        if (rows > 0 && columns > 0) {
            return {row, column, rows, columns};
        }
    }
    const QTextTableCell cell = table->cellAt(cursor);
    if (!cell.isValid()) {
        return {};
    }
    return {cell.row(), cell.column(), cell.rowSpan(), cell.columnSpan()};
}

CellRange wholeTable(const QTextTable *table)
{
    return table != nullptr ? CellRange{0, 0, table->rows(), table->columns()} : CellRange{};
}

QList<QTextTableCell> cellsIn(const QTextTable *table, const CellRange &range)
{
    QList<QTextTableCell> out;
    QSet<int> seen;
    for (int r = range.row; r < range.row + range.rows && r < table->rows(); ++r) {
        for (int c = range.column; c < range.column + range.columns && c < table->columns(); ++c) {
            const QTextTableCell cell = table->cellAt(r, c);
            if (cell.isValid() && !seen.contains(cell.firstPosition())) {
                seen.insert(cell.firstPosition());
                out.append(cell);
            }
        }
    }
    return out;
}

QTextTableCell rowAnchor(const QTextTable *table, int row)
{
    for (int c = 0; table != nullptr && c < table->columns(); ++c) {
        const QTextTableCell cell = table->cellAt(row, c);
        if (cell.isValid() && cell.row() == row) {
            return cell;
        }
    }
    return {};
}

void copyNeighbourFormat(QTextTable *table, int row, int column, int fromRow, int fromColumn)
{
    const QTextTableCell from = table->cellAt(fromRow, fromColumn);
    QTextTableCell to = table->cellAt(row, column);
    if (!from.isValid() || !to.isValid() || from.row() != fromRow || from.column() != fromColumn
        || from.rowSpan() != 1 || from.columnSpan() != 1 || to.rowSpan() != 1 || to.columnSpan() != 1
        || to.row() != row || to.column() != column) {
        return;
    }
    QTextCursor(to.firstCursorPosition()).setBlockFormat(from.firstCursorPosition().blockFormat());
    to.setFormat(from.format());
}

bool ensureWordTable(QTextTable *table)
{
    QTextTableFormat tf = table->format();
    if (tf.borderCollapse() && tf.cellSpacing() <= 0 && tf.border() <= 0) {
        return false;
    }
    // AGENT-NOTE: a table from HTML or an older Write keeps Qt's separate
    // borders (a frame plus spaced cells). Word has one model — collapsed
    // edges, each side a border of its own — so the first border or
    // shading command converts the table to it, keeping its look as near
    // as one line per edge can.
    const WordBorder line = pixelBorder(tf.border(), tf.borderBrush());
    WordBorders borders;
    for (const QString &side : {kTop, kLeft, kBottom, kRight, kInsideH, kInsideV}) {
        borders.insert(side, line);
    }
    const qreal padding = qMax<qreal>(0, tf.cellPadding());
    const int margin = qRound(Units::pxToTwips(padding));
    tf.setBorderCollapse(true);
    tf.setBorder(0);
    tf.setCellSpacing(0);
    tf.setCellPadding(0);
    tf.setProperty(kTableBorders, bordersSpec(borders));
    tf.setProperty(kTableCellMargins, marginsSpec({margin, margin, margin, margin}));
    table->setFormat(tf);
    for (const QTextTableCell &cell : cellsIn(table, wholeTable(table))) {
        QTextTableCellFormat f = cell.format().toTableCellFormat();
        f.setPadding(padding);
        for (const QString &side : {kTop, kBottom, kLeft, kRight}) {
            showSide(f, side, borders.value(tableSideFor(table, cell, side)));
        }
        QTextTableCell(cell).setFormat(f);
    }
    return true;
}

QTextTableFormat newWordTableFormat(int columns, qreal contentWidthPx)
{
    QTextTableFormat tf;
    tf.setBorderCollapse(true);
    tf.setBorder(0);
    tf.setCellSpacing(0);
    tf.setCellPadding(0);
    columns = qMax(1, columns);
    tf.setColumnWidthConstraints(
        QList<QTextLength>(columns, QTextLength(QTextLength::FixedLength, qMax<qreal>(24, contentWidthPx) / columns)));
    WordBorders grid;
    const WordBorder line; // Table Grid: single, 0.5 pt, automatic colour
    for (const QString &side : {kTop, kLeft, kBottom, kRight, kInsideH, kInsideV}) {
        grid.insert(side, line);
    }
    tf.setProperty(kTableBorders, bordersSpec(grid));
    tf.setProperty(kTableWidth, widthSpec({QStringLiteral("auto"), 0}));
    tf.setProperty(kTableLayout, QStringLiteral("autofit"));
    tf.setProperty(kTableCellMargins, marginsSpec({-1, 108, -1, 108}));
    return tf;
}

void initWordTable(QTextTable *table)
{
    const WordBorders borders = tableLevel(table);
    const qreal side = Units::twipsToPx(108);
    for (const QTextTableCell &cell : cellsIn(table, wholeTable(table))) {
        QTextTableCellFormat f = cell.format().toTableCellFormat();
        f.setLeftPadding(side);
        f.setRightPadding(side);
        f.setTopPadding(0);
        f.setBottomPadding(0);
        f.setProperty(kCellBorders, QString());
        for (const QString &s : {kTop, kBottom, kLeft, kRight}) {
            showSide(f, s, borders.value(tableSideFor(table, cell, s), WordBorder::none()));
        }
        QTextTableCell(cell).setFormat(f);
    }
}

void setRangeBorders(QTextTable *table, const CellRange &range, const WordBorders &edges)
{
    if (table == nullptr || !range.isValid() || edges.isEmpty()) {
        return;
    }
    ensureWordTable(table);
    if (range.coversTable(table)) {
        // The whole table: Word writes w:tblBorders and the cells' own
        // sides it covers give way to them.
        QTextTableFormat tf = table->format();
        WordBorders own = parseBordersSpec(tf.stringProperty(kTableBorders));
        for (auto it = edges.cbegin(); it != edges.cend(); ++it) {
            own.insert(it.key(), it.value());
        }
        tf.setProperty(kTableBorders, bordersSpec(own));
        table->setFormat(tf);
        const WordBorders level = tableLevel(table);
        for (const QTextTableCell &cell : cellsIn(table, range)) {
            SideChanges drop;
            for (const QString &side : {kTop, kBottom, kLeft, kRight}) {
                if (edges.contains(tableSideFor(table, cell, side))) {
                    drop.insert(side, std::nullopt);
                }
            }
            setOwnSides(table, cell, drop, level);
        }
        return;
    }
    const WordBorders level = tableLevel(table);
    const int lastRow = range.row + range.rows - 1;
    const int lastColumn = range.column + range.columns - 1;
    for (const QTextTableCell &cell : cellsIn(table, range)) {
        const struct {
            QString side;
            QString edge;
        } sides[] = {{kTop, cell.row() <= range.row ? kTop : kInsideH},
                     {kBottom, cell.row() + cell.rowSpan() - 1 >= lastRow ? kBottom : kInsideH},
                     {kLeft, cell.column() <= range.column ? kLeft : kInsideV},
                     {kRight, cell.column() + cell.columnSpan() - 1 >= lastColumn ? kRight : kInsideV}};
        SideChanges mine;
        for (const auto &s : sides) {
            const auto value = edges.constFind(s.edge);
            if (value == edges.cend()) {
                continue;
            }
            mine.insert(s.side, value.value());
            if (s.edge == kInsideH || s.edge == kInsideV) {
                continue; // the cell across is in the selection and gets its own
            }
            for (const QTextTableCell &n : neighbours(table, cell, s.side)) {
                setOwnSides(table, n, {{opposite(s.side), value.value()}}, level);
            }
        }
        setOwnSides(table, cell, mine, level);
    }
}

void applyTableBorders(QTextTable *table, const CellRange &range, BorderTarget target, const WordBorder &pen)
{
    WordBorders edges;
    auto set = [&](std::initializer_list<QString> sides, const WordBorder &b) {
        for (const QString &s : sides) {
            edges.insert(s, b);
        }
    };
    switch (target) {
    case BorderTarget::All:
        set({kTop, kBottom, kLeft, kRight, kInsideH, kInsideV}, pen);
        break;
    case BorderTarget::Outside:
        set({kTop, kBottom, kLeft, kRight}, pen);
        break;
    case BorderTarget::Inside:
        set({kInsideH, kInsideV}, pen);
        break;
    case BorderTarget::Top:
        set({kTop}, pen);
        break;
    case BorderTarget::Bottom:
        set({kBottom}, pen);
        break;
    case BorderTarget::Left:
        set({kLeft}, pen);
        break;
    case BorderTarget::Right:
        set({kRight}, pen);
        break;
    case BorderTarget::InsideHorizontal:
        set({kInsideH}, pen);
        break;
    case BorderTarget::InsideVertical:
        set({kInsideV}, pen);
        break;
    case BorderTarget::None:
        set({kTop, kBottom, kLeft, kRight, kInsideH, kInsideV}, WordBorder::none());
        break;
    }
    setRangeBorders(table, range, edges);
}

WordBorders rangeBorders(const QTextTable *table, const CellRange &range)
{
    WordBorders out;
    if (table == nullptr || !range.isValid()) {
        return out;
    }
    const WordBorders level = tableLevel(table);
    const QTextTableCell first = table->cellAt(range.row, range.column);
    const QTextTableCell lastRow = table->cellAt(range.row + range.rows - 1, range.column);
    const QTextTableCell lastColumn = table->cellAt(range.row, range.column + range.columns - 1);
    out.insert(kTop, shownSide(table, first, kTop, level));
    out.insert(kLeft, shownSide(table, first, kLeft, level));
    out.insert(kBottom, shownSide(table, lastRow, kBottom, level));
    out.insert(kRight, shownSide(table, lastColumn, kRight, level));
    if (first.row() + first.rowSpan() < range.row + range.rows) {
        out.insert(kInsideH, shownSide(table, first, kBottom, level));
    }
    if (first.column() + first.columnSpan() < range.column + range.columns) {
        out.insert(kInsideV, shownSide(table, first, kRight, level));
    }
    return out;
}

void applyCellShading(QTextTable *table, const CellRange &range, const QColor &fill)
{
    if (table == nullptr) {
        return;
    }
    for (const QTextTableCell &cell : cellsIn(table, range)) {
        QTextTableCellFormat f = cell.format().toTableCellFormat();
        if (fill.isValid() && fill.alpha() > 0) {
            f.setBackground(QColor(fill.rgb())); // Word's fills are opaque RGB
        } else {
            f.clearBackground();
        }
        QTextTableCell(cell).setFormat(f);
    }
}

void applyCellVerticalAlignment(QTextTable *table, const CellRange &range, QTextCharFormat::VerticalAlignment alignment)
{
    if (table == nullptr) {
        return;
    }
    for (const QTextTableCell &cell : cellsIn(table, range)) {
        QTextTableCellFormat f = cell.format().toTableCellFormat();
        f.setVerticalAlignment(alignment);
        // A row height's pad follows the new alignment (docx_table_format.h setRowPad).
        if (const qreal pad = f.doubleProperty(kRowPad) + f.doubleProperty(kRowPadTop); pad > 0) {
            setRowPad(f, pad);
        }
        QTextTableCell(cell).setFormat(f);
    }
}

} // namespace QindaOffice::Docx
