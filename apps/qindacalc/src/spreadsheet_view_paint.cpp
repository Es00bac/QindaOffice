// SPDX-License-Identifier: GPL-3.0-or-later
// SpreadsheetView painting: the header bands, the cell grid and the
// paintEvent that drives them. Split out of spreadsheet_view.cpp to keep
// every file under the line budget (AGENTS.md).
//
// AGENT-GUARD: the grid is painted inside p.translate(kHeaderWidth,
// kHeaderHeight), so cellRect() is ALREADY correct in that block. Adding the
// header offset again there draws a full header down and to the right of the
// cell it belongs to — use cursorViewportRect() only OUTSIDE the transform.
//
// AGENT-GUARD: every position here comes from visibleRowBands() /
// visibleColumnBands(), the same lists hit-testing walks. Deriving a
// position from the scroll value instead breaks the moment a pane is frozen.

#include "spreadsheet_view.h"

#include "spreadsheet_document.h"

#include "number_format.h"

#include <QPainter>
#include <QPaintEvent>
#include <QScrollBar>

namespace CalcEng = QindaOffice::Calc;
namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;

namespace {

// Bit positions of CellStyle::borders.
enum BorderBit { kLeft = 1, kRight = 2, kTop = 4, kBottom = 8 };

} // namespace

void SpreadsheetView::paintHeaderBands(QPainter &p, const QRect &clip)
{
    Q_UNUSED(clip);
    const QColor headerBg = palette().button().color();
    const QColor headerFg = palette().buttonText().color();
    const QColor line = palette().dark().color();
    const CellRange selection = selectionRange();

    const QRect corner(0, 0, kHeaderWidth, kHeaderHeight);
    p.fillRect(corner, headerBg);
    p.setPen(line);
    // The corner box selects the whole sheet; draw it as the little triangle
    // every spreadsheet uses rather than the bullet that used to sit here.
    const QPolygon marker({QPoint(kHeaderWidth - 6, 5), QPoint(kHeaderWidth - 6, kHeaderHeight - 5),
                           QPoint(6, kHeaderHeight - 5)});
    p.setBrush(headerFg);
    p.drawPolygon(marker);
    p.setBrush(Qt::NoBrush);

    p.save();
    p.translate(kHeaderWidth, 0);
    for (const Band &band : visibleColumnBands()) {
        const QRectF r(band.pos, 0, band.size, kHeaderHeight);
        const bool selected =
            band.index >= selection.topLeft.col && band.index <= selection.bottomRight.col;
        p.fillRect(r, selected ? palette().highlight().color().lighter(170) : headerBg);
        p.setPen(headerFg);
        p.drawText(r.adjusted(0, 0, -1, -1), Qt::AlignCenter, colName(band.index));
        p.setPen(line);
        p.drawLine(r.bottomLeft(), r.bottomRight());
        p.drawLine(r.topRight(), r.bottomRight());
    }
    p.restore();

    p.save();
    p.translate(0, kHeaderHeight);
    for (const Band &band : visibleRowBands()) {
        const QRectF r(0, band.pos, kHeaderWidth, band.size);
        const bool selected =
            band.index >= selection.topLeft.row && band.index <= selection.bottomRight.row;
        p.fillRect(r, selected ? palette().highlight().color().lighter(170) : headerBg);
        p.setPen(headerFg);
        p.drawText(r.adjusted(0, 0, -1, -1), Qt::AlignCenter, QString::number(band.index + 1));
        p.setPen(line);
        p.drawLine(r.bottomLeft(), r.bottomRight());
        p.drawLine(r.bottomRight(), r.topRight());
    }
    p.restore();
}

void SpreadsheetView::paintGrid(QPainter &p, const QRect &clip)
{
    const qint32 sheetIndex = m_document->activeSheet();
    const Workbook &wb = *m_document->workbook();
    const Sheet &sheet = wb.sheetAt(sheetIndex);
    const QVector<Band> rows = visibleRowBands();
    const QVector<Band> cols = visibleColumnBands();
    const CellRange selection = selectionRange();

    p.save();
    p.translate(kHeaderWidth, kHeaderHeight);

    // The cell font scales with the view, or zooming would only move the
    // grid lines and leave the text at its original size.
    const QFont baseFont = scaledFont();

    // Merged blocks are painted once, from their top-left cell, over the
    // whole span; the covered cells are skipped entirely so a merge reads as
    // one cell instead of a grid of fragments.
    const auto bandFor = [](const QVector<Band> &bands, qint32 index) -> const Band * {
        for (const Band &band : bands) {
            if (band.index == index) {
                return &band;
            }
        }
        return nullptr;
    };

    for (const Band &rowBand : rows) {
        for (const Band &colBand : cols) {
            const CellAddr addr{rowBand.index, colBand.index};
            QRectF cell(colBand.pos, rowBand.pos, colBand.size, rowBand.size);
            const CellRange *merge = sheet.mergeCovering(addr);
            if (merge != nullptr) {
                if (merge->topLeft != addr) {
                    continue; // painted by the anchor cell
                }
                const Band *lastRow = bandFor(rows, merge->bottomRight.row);
                const Band *lastCol = bandFor(cols, merge->bottomRight.col);
                if (lastRow != nullptr) {
                    cell.setBottom(lastRow->pos + lastRow->size);
                }
                if (lastCol != nullptr) {
                    cell.setRight(lastCol->pos + lastCol->size);
                }
            }
            const bool inSelection = selection.contains(addr);
            const bool isCursor = addr == m_cursor;
            if (inSelection) {
                p.fillRect(cell, isCursor ? palette().highlight().color()
                                          : palette().highlight().color().lighter(160));
            }
            const Cell *cellData = sheet.cellAt(addr);
            const qint32 styleId = sheet.styleIdAt(addr);
            // Styles live on EMPTY cells too (fill, borders), so read the
            // style whether or not the cell has content.
            const CellStyle *style = styleId != 0 ? wb.style(styleId) : nullptr;
            if (style != nullptr && style->bg.has_value() && !inSelection) {
                p.fillRect(cell, QColor((*style->bg).r, (*style->bg).g, (*style->bg).b));
            }
            if (cellData != nullptr && !isBlank(cellData->value)) {
                const QString text =
                    NumberFormat(style != nullptr && !style->numberFormat.isEmpty()
                                     ? style->numberFormat
                                     : QStringLiteral("General"))
                        .format(cellData->value)
                        .text;
                QFont cellFont = baseFont;
                if (style != nullptr) {
                    if (!style->fontFamily.isEmpty()) {
                        cellFont.setFamily(style->fontFamily);
                    }
                    if (style->fontSize > 0) {
                        cellFont.setPointSizeF(style->fontSize * m_zoom);
                    }
                    cellFont.setBold(style->bold);
                    cellFont.setItalic(style->italic);
                    cellFont.setUnderline(style->underline);
                }
                p.setFont(cellFont);
                if (inSelection && isCursor) {
                    p.setPen(palette().highlightedText().color());
                } else if (style != nullptr && style->fg.has_value()) {
                    p.setPen(QColor((*style->fg).r, (*style->fg).g, (*style->fg).b));
                } else {
                    p.setPen(palette().text().color());
                }
                // Explicit alignment wins; otherwise numbers right, text left.
                Qt::Alignment alignment = Qt::AlignVCenter;
                if (style != nullptr && style->alignment != 0) {
                    alignment |= style->alignment == 1 ? Qt::AlignHCenter : Qt::AlignRight;
                } else {
                    alignment |= isNumber(cellData->value) || isBool(cellData->value)
                        ? Qt::AlignRight
                        : Qt::AlignLeft;
                }
                int flags = int(alignment);
                if (style != nullptr && style->wrap) {
                    // Wrapped text reads from the top of the cell, the way a
                    // paragraph does; centring it vertically hides the tail.
                    flags = (flags & ~int(Qt::AlignVCenter)) | int(Qt::AlignTop)
                        | int(Qt::TextWordWrap);
                }
                p.drawText(cell.adjusted(4, 1, -4, -1), flags, text);
            }
            p.setPen(palette().mid().color());
            p.drawLine(cell.bottomLeft(), cell.bottomRight());
            p.drawLine(cell.topRight(), cell.bottomRight());
            if (style != nullptr && style->borders != 0) {
                p.setPen(QPen(palette().text().color(), 1));
                if (style->borders & kLeft) {
                    p.drawLine(cell.topLeft(), cell.bottomLeft());
                }
                if (style->borders & kRight) {
                    p.drawLine(cell.topRight(), cell.bottomRight());
                }
                if (style->borders & kTop) {
                    p.drawLine(cell.topLeft(), cell.topRight());
                }
                if (style->borders & kBottom) {
                    p.drawLine(cell.bottomLeft(), cell.bottomRight());
                }
            }
        }
    }

    // Cursor outline last. AGENT-GUARD: GRID coordinates — the painter is
    // still inside p.translate(kHeaderWidth, kHeaderHeight) from the top of
    // this block, so translating the rect here too applied the header offset
    // TWICE and drew the outline a full header down and to the right of the
    // cell it belongs to (the selection fill, painted in the loop above, sat
    // in the right place, which is what made it look so odd).
    p.setPen(QPen(palette().highlight().color().darker(140), 2));
    p.setBrush(Qt::NoBrush);
    p.drawRect(cellRect(m_cursor));

    // The frozen split, so the user can see where the sheet stops scrolling.
    if (m_frozen.row > 0 || m_frozen.col > 0) {
        p.setPen(QPen(palette().shadow().color(), 2));
        if (m_frozen.row > 0) {
            const int y = int(frozenHeightPx());
            p.drawLine(0, y, clip.width(), y);
        }
        if (m_frozen.col > 0) {
            const int x = int(frozenWidthPx());
            p.drawLine(x, 0, x, clip.height());
        }
    }
    p.restore();
}

void SpreadsheetView::paintEvent(QPaintEvent *event)
{
    QPainter p(viewport());
    p.fillRect(viewport()->rect(), palette().base().color());
    paintGrid(p, event->rect());
    paintHeaderBands(p, event->rect());
}

} // namespace QindaOffice::Apps::QindaCalc
