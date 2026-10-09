// SPDX-License-Identifier: GPL-3.0-or-later
// Table, row, column and cell properties, AutoFit, Distribute and row
// heights on a QTextTable (docx_table_format.h); borders and shading are
// in docx_table_format.cpp.

#include "docx_properties.h"
#include "docx_table_format.h"
#include "ooxml_units.h"

#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTable>

#include <numeric>

namespace QindaOffice::Docx {

namespace Units = Ooxml::Units;

namespace {

// Word's cell margins when nothing sets them: 0.075 in left and right.
constexpr int kDefaultSideMargin = 108;

CellMargins tableMargins(const QTextTable *table)
{
    return parseMarginsSpec(table->format().stringProperty(kTableCellMargins));
}

// Shows a cell's margins: its own, else the table's, else Word's default;
// the padding a row height added stays on top of them.
void showMargins(QTextTableCellFormat &f, const CellMargins &table)
{
    const CellMargins own = parseMarginsSpec(f.stringProperty(kCellMargins));
    auto pick = [](int own, int table, int fallback) { return own >= 0 ? own : table >= 0 ? table : fallback; };
    f.setTopPadding(Units::twipsToPx(pick(own.top, table.top, 0)) + f.doubleProperty(kRowPadTop));
    f.setLeftPadding(Units::twipsToPx(pick(own.left, table.left, kDefaultSideMargin)));
    f.setRightPadding(Units::twipsToPx(pick(own.right, table.right, kDefaultSideMargin)));
    f.setBottomPadding(Units::twipsToPx(pick(own.bottom, table.bottom, 0)) + f.doubleProperty(kRowPad));
}

// How tall a cell's content stands (paragraphs, padding, borders), without
// what a row height added; -1 for a nested table (it decides for itself).
qreal contentHeight(const QTextTableCell &cell)
{
    qreal height = 0;
    for (auto it = cell.begin(); !it.atEnd(); ++it) {
        if (it.currentFrame() != nullptr) {
            return -1;
        }
        const QTextBlock b = it.currentBlock();
        const QTextLayout *layout = b.layout();
        height += (layout != nullptr ? layout->boundingRect().height() : 0) + b.blockFormat().topMargin()
            + b.blockFormat().bottomMargin();
    }
    const QTextTableCellFormat f = cell.format().toTableCellFormat();
    return height + f.topPadding() + f.bottomPadding() - f.doubleProperty(kRowPad) - f.doubleProperty(kRowPadTop)
        + f.topBorder() + f.bottomBorder();
}

void layOut(const QTextTable *table)
{
    static_cast<void>(table->document()->documentLayout()->documentSize());
}

QList<QTextLength> fixedConstraints(const QList<int> &twips)
{
    QList<QTextLength> out;
    for (const int w : twips) {
        out.append(QTextLength(QTextLength::FixedLength, Units::twipsToPx(qMax(1, w))));
    }
    return out;
}

} // namespace

TableProperties tableProperties(const QTextTable *table)
{
    TableProperties p;
    const QTextTableFormat tf = table->format();
    if (tf.hasProperty(kTableWidth) && !tf.stringProperty(kTableWidth).isEmpty()) {
        p.width = parseWidthSpec(tf.stringProperty(kTableWidth));
    } else if (tf.width().type() == QTextLength::PercentageLength) {
        p.width = {QStringLiteral("pct"), qRound(tf.width().rawValue() * 50)};
    } else if (tf.width().type() == QTextLength::FixedLength) {
        p.width = {QStringLiteral("dxa"), qRound(Units::pxToTwips(tf.width().rawValue()))};
    }
    const Qt::Alignment a = tf.alignment() & Qt::AlignHorizontal_Mask;
    p.alignment = a == Qt::AlignHCenter || a == Qt::AlignRight ? a : Qt::AlignLeft;
    p.indent = tf.hasProperty(kTableIndent) ? tf.intProperty(kTableIndent) : qRound(Units::pxToTwips(tf.leftMargin()));
    p.layout = tf.stringProperty(kTableLayout) == u"fixed" ? QStringLiteral("fixed") : QStringLiteral("autofit");
    p.margins = tableMargins(table);
    p.headerRows = tf.headerRowCount();
    return p;
}

void setTableProperties(QTextTable *table, const TableProperties &p, qreal contentWidthPx)
{
    ensureWordTable(table);
    const QList<int> before = columnWidths(table, contentWidthPx);
    QTextTableFormat tf = table->format();
    tf.setProperty(kTableWidth, widthSpec(p.width));
    // A preferred width scales the columns to it, as Word does.
    qreal target = 0;
    if (p.width.type == u"dxa" && p.width.value > 0) {
        target = Units::twipsToPx(p.width.value);
        tf.setWidth(QTextLength(QTextLength::FixedLength, target));
    } else if (p.width.type == u"pct" && p.width.value > 0) {
        target = contentWidthPx * p.width.value / 5000.0;
        tf.setWidth(QTextLength(QTextLength::PercentageLength, p.width.value / 50.0));
    } else {
        tf.clearProperty(QTextFormat::FrameWidth);
    }
    const int total = std::accumulate(before.cbegin(), before.cend(), 0);
    if (target > 0 && total > 0) {
        QList<int> scaled;
        for (const int w : before) {
            scaled.append(qRound(w * Units::pxToTwips(target) / total));
        }
        tf.setColumnWidthConstraints(fixedConstraints(scaled));
    }
    tf.setAlignment(p.alignment);
    tf.setProperty(kTableIndent, p.indent);
    tf.setLeftMargin(p.alignment == Qt::AlignLeft ? qMax<qreal>(0, Units::twipsToPx(p.indent)) : 0);
    tf.setProperty(kTableLayout, p.layout);
    tf.setProperty(kTableCellMargins, marginsSpec(p.margins));
    tf.setHeaderRowCount(qBound(0, p.headerRows, table->rows()));
    table->setFormat(tf);
    for (const QTextTableCell &cell : cellsIn(table, wholeTable(table))) {
        QTextTableCellFormat f = cell.format().toTableCellFormat();
        showMargins(f, p.margins);
        QTextTableCell(cell).setFormat(f);
    }
}

RowProperties rowProperties(const QTextTable *table, int row)
{
    RowProperties p;
    const QTextTableCell anchor = rowAnchor(table, row);
    if (!anchor.isValid()) {
        return p;
    }
    const QTextCharFormat f = anchor.format();
    p.height = qRound(Units::pxToTwips(f.doubleProperty(kRowHeight)));
    p.exact = f.stringProperty(kRowHeightRule) == u"exact";
    p.cantSplit = f.boolProperty(kRowCantSplit);
    return p;
}

void setRowProperties(QTextTable *table, int row, const RowProperties &p)
{
    QTextTableCell anchor = rowAnchor(table, row);
    if (!anchor.isValid()) {
        return;
    }
    QTextTableCellFormat f = anchor.format().toTableCellFormat();
    if (p.height > 0) {
        f.setProperty(kRowHeight, Units::twipsToPx(p.height));
        f.setProperty(kRowHeightRule, p.exact ? QStringLiteral("exact") : QStringLiteral("atLeast"));
    } else {
        f.clearProperty(kRowHeight);
        f.clearProperty(kRowHeightRule);
    }
    f.setProperty(kRowCantSplit, p.cantSplit);
    anchor.setFormat(f);
    refreshRowHeight(table, row);
}

QList<int> columnWidths(const QTextTable *table, qreal contentWidthPx)
{
    const int columns = table->columns();
    const QList<QTextLength> constraints = table->format().columnWidthConstraints();
    QList<int> out(columns, 0);
    bool measured = false;
    for (int c = 0; c < columns; ++c) {
        const QTextLength l = c < constraints.size() ? constraints.at(c) : QTextLength();
        if (l.type() == QTextLength::FixedLength) {
            out[c] = qRound(Units::pxToTwips(l.rawValue()));
            continue;
        }
        if (l.type() == QTextLength::PercentageLength) {
            out[c] = qRound(Units::pxToTwips(contentWidthPx * l.rawValue() / 100.0));
            continue;
        }
        // Variable (AutoFit to contents): as laid out, from a cell that
        // starts in this column and spans only it.
        if (!measured) {
            layOut(table);
            measured = true;
        }
        for (int r = 0; r < table->rows(); ++r) {
            const QTextTableCell cell = table->cellAt(r, c);
            if (cell.column() != c || cell.columnSpan() != 1) {
                continue;
            }
            const QTextBlock block = cell.firstCursorPosition().block();
            const QRectF rect = table->document()->documentLayout()->blockBoundingRect(block);
            const QTextTableCellFormat f = cell.format().toTableCellFormat();
            out[c] = qRound(Units::pxToTwips(rect.width() + block.blockFormat().leftMargin()
                                             + block.blockFormat().rightMargin() + f.leftPadding() + f.rightPadding()));
            break;
        }
        if (out[c] <= 0) {
            out[c] = qRound(Units::pxToTwips(contentWidthPx / qMax(1, columns)));
        }
    }
    return out;
}

void setColumnWidth(QTextTable *table, int column, int twips, qreal contentWidthPx)
{
    QList<int> widths = columnWidths(table, contentWidthPx);
    if (column < 0 || column >= widths.size() || twips <= 0) {
        return;
    }
    widths[column] = twips;
    QTextTableFormat tf = table->format();
    tf.setColumnWidthConstraints(fixedConstraints(widths));
    table->setFormat(tf);
}

CellMargins cellMargins(const QTextTable *table, int row, int column)
{
    const QTextTableCell cell = table->cellAt(row, column);
    return cell.isValid() ? parseMarginsSpec(cell.format().stringProperty(kCellMargins)) : CellMargins();
}

void setCellMargins(QTextTable *table, const CellRange &range, const CellMargins &margins)
{
    ensureWordTable(table);
    const CellMargins table_ = tableMargins(table);
    for (const QTextTableCell &cell : cellsIn(table, range)) {
        QTextTableCellFormat f = cell.format().toTableCellFormat();
        f.setProperty(kCellMargins, marginsSpec(margins));
        showMargins(f, table_);
        QTextTableCell(cell).setFormat(f);
    }
}

void autoFit(QTextTable *table, AutoFit mode, qreal contentWidthPx)
{
    ensureWordTable(table);
    const QList<int> widths = columnWidths(table, contentWidthPx);
    const int total = qMax(1, std::accumulate(widths.cbegin(), widths.cend(), 0));
    QTextTableFormat tf = table->format();
    QList<QTextLength> constraints;
    switch (mode) {
    case AutoFit::Contents:
        // Word: no preferred widths; the columns follow their contents.
        constraints = QList<QTextLength>(widths.size(), QTextLength());
        tf.clearProperty(QTextFormat::FrameWidth);
        tf.setProperty(kTableWidth, widthSpec({QStringLiteral("auto"), 0}));
        tf.setProperty(kTableLayout, QStringLiteral("autofit"));
        break;
    case AutoFit::Window:
        for (const int w : widths) {
            constraints.append(QTextLength(QTextLength::PercentageLength, 100.0 * w / total));
        }
        tf.setWidth(QTextLength(QTextLength::PercentageLength, 100));
        tf.setProperty(kTableWidth, widthSpec({QStringLiteral("pct"), 5000}));
        tf.setProperty(kTableLayout, QStringLiteral("autofit"));
        break;
    case AutoFit::Fixed:
        constraints = fixedConstraints(widths);
        tf.clearProperty(QTextFormat::FrameWidth);
        tf.setProperty(kTableLayout, QStringLiteral("fixed"));
        break;
    }
    tf.setColumnWidthConstraints(constraints);
    table->setFormat(tf);
}

void distributeColumns(QTextTable *table, const CellRange &range, qreal contentWidthPx)
{
    QList<int> widths = columnWidths(table, contentWidthPx);
    const int first = qMax(0, range.column);
    const int last = qMin(int(widths.size()), range.column + range.columns) - 1;
    if (last <= first) {
        return;
    }
    int sum = 0;
    for (int c = first; c <= last; ++c) {
        sum += widths[c];
    }
    for (int c = first; c <= last; ++c) {
        widths[c] = sum / (last - first + 1);
    }
    QTextTableFormat tf = table->format();
    tf.setColumnWidthConstraints(fixedConstraints(widths));
    table->setFormat(tf);
}

void distributeRows(QTextTable *table, const CellRange &range)
{
    const int first = qMax(0, range.row);
    const int last = qMin(table->rows(), range.row + range.rows) - 1;
    if (last <= first) {
        return;
    }
    layOut(table);
    qreal sum = 0;
    for (int r = first; r <= last; ++r) {
        const QTextTableCell anchor = rowAnchor(table, r); // none when every cell continues a merge
        qreal tallest = anchor.isValid() ? anchor.format().doubleProperty(kRowHeight) : 0;
        for (int c = 0; c < table->columns(); ++c) {
            const QTextTableCell cell = table->cellAt(r, c);
            if (cell.row() == r && cell.column() == c && cell.rowSpan() == 1) {
                tallest = qMax(tallest, contentHeight(cell));
            }
        }
        sum += tallest;
    }
    const int height = qRound(Units::pxToTwips(sum / (last - first + 1)));
    for (int r = first; r <= last; ++r) {
        RowProperties p = rowProperties(table, r);
        p.height = height;
        p.exact = false;
        setRowProperties(table, r, p);
    }
}

void setRowPad(QTextTableCellFormat &format, qreal pad)
{
    format.setTopPadding(qMax<qreal>(0, format.topPadding() - format.doubleProperty(kRowPadTop)));
    format.setBottomPadding(qMax<qreal>(0, format.bottomPadding() - format.doubleProperty(kRowPad)));
    format.clearProperty(kRowPadTop);
    format.clearProperty(kRowPad);
    if (pad <= 0) {
        return;
    }
    // AGENT-NOTE: Qt centres (or bottoms) a cell's content in what the row
    // leaves after the cell's own padding, so a pad all on the bottom would
    // pin a centred cell's text to the top of a tall row (a label sheet's
    // first label did). Centred cells get it half and half, bottom ones on top.
    qreal top = 0;
    if (format.verticalAlignment() == QTextCharFormat::AlignMiddle) {
        top = pad / 2;
    } else if (format.verticalAlignment() == QTextCharFormat::AlignBottom) {
        top = pad;
    }
    format.setTopPadding(format.topPadding() + top);
    format.setBottomPadding(format.bottomPadding() + pad - top);
    if (top > 0) {
        format.setProperty(kRowPadTop, top);
    }
    if (pad - top > 0) {
        format.setProperty(kRowPad, pad - top);
    }
}

namespace {

// The cells that start in `row` and span only it: the ones whose content
// sets the row's height.
QList<QTextTableCell> ownCells(const QTextTable *table, int row)
{
    QList<QTextTableCell> cells;
    for (int c = 0; c < table->columns(); ++c) {
        const QTextTableCell cell = table->cellAt(row, c);
        if (cell.row() == row && cell.column() == c && cell.rowSpan() == 1) {
            cells.append(cell);
        }
    }
    return cells;
}

struct CellPad {
    QTextTableCell cell;
    qreal pad = 0;
};

// What each cell of `row` is to be padded by for its Word height
// (kRowHeight): the first cell takes what it lacks itself when the row is
// short of the height — not what the tallest cell lacks, or a short first
// cell would leave the row short — and the others nothing. The heights are
// measured without the pads already there, so they need not come off first.
QList<CellPad> rowPads(const QTextTable *table, int row)
{
    QList<CellPad> pads;
    const QList<QTextTableCell> cells = ownCells(table, row);
    for (const QTextTableCell &cell : cells) {
        pads.append({cell, 0});
    }
    const QTextTableCell anchor = rowAnchor(table, row);
    const qreal wanted = anchor.isValid() ? anchor.format().doubleProperty(kRowHeight) : 0;
    if (wanted <= 0 || cells.isEmpty()) {
        return pads;
    }
    qreal tallest = 0;
    for (const QTextTableCell &cell : cells) {
        const qreal h = contentHeight(cell);
        if (h < 0) {
            return pads; // a nested table decides for itself
        }
        tallest = qMax(tallest, h);
    }
    if (tallest < wanted - 0.5) {
        pads.first().pad = wanted - contentHeight(cells.first());
    }
    return pads;
}

// Sets the pads in one edit block (one relayout), touching only the cells
// whose format changes.
void applyPads(QTextDocument *document, const QList<CellPad> &pads)
{
    QTextCursor block(document);
    block.beginEditBlock();
    for (const CellPad &p : pads) {
        const QTextTableCellFormat now = p.cell.format().toTableCellFormat();
        QTextTableCellFormat f = now;
        setRowPad(f, p.pad);
        if (f != now) {
            QTextTableCell(p.cell).setFormat(f);
        }
    }
    block.endEditBlock();
}

void collectTables(QTextFrame *frame, QList<QTextTable *> &tables)
{
    for (QTextFrame *child : frame->childFrames()) {
        if (auto *table = qobject_cast<QTextTable *>(child)) {
            tables.append(table);
        }
        collectTables(child, tables);
    }
}

} // namespace

// AGENT-GUARD: every pad is measured before any is set. Qt lays new text
// out only when the outermost edit block ends, so measuring rows filled in
// the same edit block reads empty layouts and over-pads them (a label sheet
// spilled onto a second page that way): callers that fill rows inside an
// edit block refresh after it ends, joined to it (write_envelopes.cpp).
void refreshRowHeight(QTextTable *table, int row)
{
    layOut(table);
    applyPads(table->document(), rowPads(table, row));
}

void refreshRowHeights(QTextTable *table)
{
    layOut(table);
    QList<CellPad> pads;
    for (int r = 0; r < table->rows(); ++r) {
        pads += rowPads(table, r);
    }
    applyPads(table->document(), pads);
}

void refreshRowHeights(QTextDocument *document)
{
    QList<QTextTable *> tables;
    collectTables(document->rootFrame(), tables);
    if (tables.isEmpty()) {
        return;
    }
    static_cast<void>(document->documentLayout()->documentSize());
    QList<CellPad> pads;
    for (const QTextTable *table : std::as_const(tables)) {
        for (int r = 0; r < table->rows(); ++r) {
            pads += rowPads(table, r);
        }
    }
    applyPads(document, pads);
}

} // namespace QindaOffice::Docx
