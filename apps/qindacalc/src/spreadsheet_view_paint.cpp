// SPDX-License-Identifier: GPL-3.0-or-later
// SpreadsheetView painting: the header bands, the cell grid and the
// paintEvent that drives them. Split out of spreadsheet_view.cpp to keep
// every file under the line budget (AGENTS.md).
//
// AGENT-GUARD: the grid is painted inside p.translate(kHeaderWidth,
// kHeaderHeight), so cellRect() is ALREADY correct in that block. Adding the
// header offset again there draws a full header down and to the right of the
// cell it belongs to — use cursorViewportRect() only OUTSIDE the transform.

#include "spreadsheet_view.h"

#include "spreadsheet_document.h"

#include "number_format.h"

#include <QPainter>
#include <QPaintEvent>
#include <QScrollBar>

namespace CalcEng = QindaOffice::Calc;
namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;

void SpreadsheetView::paintHeaderBands(QPainter &p, const QRect &clip)
{
    const QRect corner(0, 0, kHeaderWidth, kHeaderHeight);
    p.fillRect(corner, palette().button().color());
    p.setPen(palette().buttonText().color());
    p.drawText(corner, Qt::AlignCenter, QStringLiteral("•"));

    // AGENT-GUARD: the column band must walk the sheet's REAL column widths —
    // the grid renders with colWidthPx(); the band previously assumed
    // a fixed 96 px, so header letters drifted out of alignment after any
    // xlsx import brought different widths in.
    p.save();
    p.translate(kHeaderWidth, 0);
    {
        const Sheet &sheet = m_document->workbook()->sheetAt(m_document->activeSheet());
        const qint32 scrollX = horizontalScrollBar()->value();
        // Find the first column whose right edge is at/after the scroll pos.
        qint32 col = 0;
        qreal x = -scrollX;
        while (col < Sheet::kMaxCols && x + colWidthPx(col) <= 0) {
            x += colWidthPx(col);
            ++col;
        }
        while (x < clip.width() && col < Sheet::kMaxCols) {
            const qreal w = colWidthPx(col);
            const QRectF r(x, 0, w, kHeaderHeight);
            if (r.right() >= 0) {
                p.fillRect(r, palette().button().color());
                p.setPen(palette().buttonText().color());
                p.drawText(r.adjusted(0, 0, -1, -1), Qt::AlignCenter, colName(col));
                p.setPen(palette().dark().color());
                p.drawLine(r.bottomLeft(), r.bottomRight());
                p.drawLine(r.topRight(), r.bottomRight());
            }
            x += w;
            ++col;
        }
    }
    p.restore();

    p.save();
    p.translate(0, kHeaderHeight);
    const Sheet &sheet = m_document->workbook()->sheetAt(m_document->activeSheet());
    qint32 row = 0;
    qreal y = -verticalScrollBar()->value();
    while (y < clip.height()) {
        const qreal h = rowHeightPx(row);
        if (y + h > 0) {
            const QRect r(0, int(y), kHeaderWidth, int(h));
            p.fillRect(r, palette().button().color());
            p.setPen(palette().buttonText().color());
            p.drawText(r.adjusted(0, 0, -1, -1), Qt::AlignCenter, QString::number(row + 1));
            p.setPen(palette().dark().color());
            p.drawLine(r.bottomLeft(), r.bottomRight());
            p.drawLine(r.bottomRight(), r.topRight());
        }
        y += h;
        ++row;
    }
    p.restore();
}

void SpreadsheetView::paintGrid(QPainter &p, const QRect &clip)
{
    const qint32 sheetIndex = m_document->activeSheet();
    const Workbook &wb = *m_document->workbook();
    const Sheet &sheet = wb.sheetAt(sheetIndex);

    p.save();
    p.translate(kHeaderWidth, kHeaderHeight);

    // Visible row band.
    qint32 firstRow = 0;
    qreal y = -verticalScrollBar()->value();
    while (firstRow < Sheet::kMaxRows && y + rowHeightPx(firstRow) < 0) {
        y += rowHeightPx(firstRow);
        ++firstRow;
    }
    // The cell font scales with the view, or zooming would only move the
    // grid lines and leave the text at its original size.
    const QFont baseFont = scaledFont();
    const QFont boldFont([this] {
        QFont f = scaledFont();
        f.setBold(true);
        return f;
    }());

    for (qint32 row = firstRow; y < clip.height() - kHeaderHeight && row < Sheet::kMaxRows;) {
        const qreal h = rowHeightPx(row);
        qint32 x = -horizontalScrollBar()->value();
        qint32 firstCol = 0;
        while (firstCol < Sheet::kMaxCols && x + colWidthPx(firstCol) < 0) {
            x += colWidthPx(firstCol);
            ++firstCol;
        }
        for (qint32 col = firstCol; x < clip.width() - kHeaderWidth
                                    && col < Sheet::kMaxCols;) {
            const qreal w = colWidthPx(col);
            const QRectF cell(x, y, w, h);
            const CellAddr addr{row, col};
            const bool inSelection =
                addr.row >= qMin(m_anchor.row, m_cursor.row)
                && addr.row <= qMax(m_anchor.row, m_cursor.row)
                && addr.col >= qMin(m_anchor.col, m_cursor.col)
                && addr.col <= qMax(m_anchor.col, m_cursor.col);
            const bool isCursor = addr == m_cursor;
            if (inSelection) {
                p.fillRect(cell, isCursor ? palette().highlight().color()
                                          : palette().highlight().color().lighter(160));
            }
            const Cell *cellData = sheet.cellAt(addr);
            // Render imported style attributes: bg fill, font family/size/
            // style, fg colour and alignment. The formatting toolbar set
            // these while the grid rendered only bold — users believed the
            // formatting applied when it was invisible.
            const CellStyle *style = cellData != nullptr
                ? wb.style(sheet.styleIdAt(addr))
                : nullptr;
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
                if (style != nullptr && (style->bold || style->italic || style->underline
                                         || style->fontSize > 0 || !style->fontFamily.isEmpty())) {
                    QFont cellFont = baseFont;
                    if (!style->fontFamily.isEmpty()) {
                        cellFont.setFamily(style->fontFamily);
                    }
                    if (style->fontSize > 0) {
                        cellFont.setPointSizeF(style->fontSize);
                    }
                    cellFont.setBold(style->bold);
                    cellFont.setItalic(style->italic);
                    cellFont.setUnderline(style->underline);
                    p.setFont(cellFont);
                } else {
                    p.setFont(style != nullptr && style->bold ? boldFont : baseFont);
                }
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
                p.drawText(cell.adjusted(4, 0, -4, 0), alignment, text);
            }
            p.setPen(palette().mid().color());
            p.drawLine(cell.bottomLeft(), cell.bottomRight());
            p.drawLine(cell.topRight(), cell.bottomRight());
            x += w;
            ++col;
        }
        y += h;
        ++row;
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
