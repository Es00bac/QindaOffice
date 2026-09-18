// SPDX-License-Identifier: GPL-3.0-or-later
// SpreadsheetItem painting: the header bands, the cell grid and the
// paint() that drives them. Moved from the Widgets view nearly verbatim:
// palette roles became the chrome colour properties, the document
// colours are fixed (D-004: paper stays paper).
//
// AGENT-GUARD: the grid is painted inside p.translate(kHeaderWidth,
// kHeaderHeight), so cellRect() is ALREADY correct in that block. Adding the
// header offset again there draws a full header down and to the right of the
// cell it belongs to — use cursorItemRect() only OUTSIDE the transform.

#include "spreadsheet_item.h"

#include "spreadsheet_document.h"

#include "number_format.h"
#include "sheet.h"

#include <QPainter>

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;

namespace {

enum BorderBit { kLeft = 1, kRight = 2, kTop = 4, kBottom = 8 };

// Document colours: the sheet is paper.
const QColor kPaper(Qt::white);
const QColor kInk(0x20, 0x20, 0x28);
const QColor kGridLine(0xd8, 0xd8, 0xdc);
const QColor kCommentMarker(0xc0, 0x39, 0x2b);

} // namespace

void SpreadsheetItem::paintHeaderBands(QPainter &p)
{
    const CellRange selection = selectionRange();
    const QColor selectedHeader = m_selectionColor.lighter(170);

    const QRectF corner(0, 0, kHeaderWidth, kHeaderHeight);
    p.fillRect(corner, m_headerColor);
    p.setPen(m_headerLineColor);
    // The corner box selects the whole sheet: the little triangle every
    // spreadsheet uses.
    const QPolygonF marker({QPointF(kHeaderWidth - 6, 5), QPointF(kHeaderWidth - 6, kHeaderHeight - 5),
                            QPointF(6, kHeaderHeight - 5)});
    p.setBrush(m_headerTextColor);
    p.drawPolygon(marker);
    p.setBrush(Qt::NoBrush);

    p.save();
    p.translate(kHeaderWidth, 0);
    for (const Band &band : visibleColumnBands()) {
        const QRectF r(band.pos, 0, band.size, kHeaderHeight);
        const bool selected =
            band.index >= selection.topLeft.col && band.index <= selection.bottomRight.col;
        p.fillRect(r, selected ? selectedHeader : m_headerColor);
        p.setPen(m_headerTextColor);
        p.drawText(r.adjusted(0, 0, -1, -1), Qt::AlignCenter, colName(band.index));
        p.setPen(m_headerLineColor);
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
        p.fillRect(r, selected ? selectedHeader : m_headerColor);
        p.setPen(m_headerTextColor);
        p.drawText(r.adjusted(0, 0, -1, -1), Qt::AlignCenter, QString::number(band.index + 1));
        p.setPen(m_headerLineColor);
        p.drawLine(r.bottomLeft(), r.bottomRight());
        p.drawLine(r.bottomRight(), r.topRight());
    }
    p.restore();
}

void SpreadsheetItem::paintGrid(QPainter &p)
{
    const Sheet *sheetPtr = activeSheet();
    if (sheetPtr == nullptr) {
        return;
    }
    const Workbook &wb = *m_document->workbook();
    const Sheet &sheet = *sheetPtr;
    const QVector<Band> rows = visibleRowBands();
    const QVector<Band> cols = visibleColumnBands();
    const CellRange selection = selectionRange();
    const QColor selectionFill = m_selectionColor.lighter(160);

    p.save();
    p.translate(kHeaderWidth, kHeaderHeight);
    p.setClipRect(QRectF(0, 0, viewportWidth(), viewportHeight()));

    // The cell font scales with the view, or zooming would only move the
    // grid lines and leave the text at its original size.
    const QFont baseFont = scaledFont();

    // Merged blocks are painted once, from their top-left cell, over the
    // whole span; the covered cells are skipped entirely.
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
                p.fillRect(cell, isCursor ? m_selectionColor : selectionFill);
            }
            const Cell *cellData = sheet.cellAt(addr);
            const qint32 styleId = sheet.styleIdAt(addr);
            // Styles live on EMPTY cells too (fill, borders).
            const CellStyle *style = styleId != 0 ? wb.style(styleId) : nullptr;
            // Conditional formatting layers OVER the cell's own style and
            // under the selection: a rule that hid the selection would make
            // the cursor invisible on a formatted block.
            const CondFormat conditional = sheet.condFormats().isEmpty()
                ? CondFormat{}
                : sheet.condFormatFor(addr, cellData != nullptr ? cellData->value : Value());
            if (!inSelection) {
                if (conditional.background) {
                    p.fillRect(cell, QColor(conditional.background->r, conditional.background->g,
                                            conditional.background->b));
                } else if (style != nullptr && style->bg.has_value()) {
                    p.fillRect(cell, QColor((*style->bg).r, (*style->bg).g, (*style->bg).b));
                }
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
                if (conditional.bold) {
                    cellFont.setBold(*conditional.bold);
                }
                if (conditional.italic) {
                    cellFont.setItalic(*conditional.italic);
                }
                p.setFont(cellFont);
                if (inSelection && isCursor) {
                    p.setPen(kPaper);
                } else if (conditional.foreground) {
                    p.setPen(QColor(conditional.foreground->r, conditional.foreground->g,
                                    conditional.foreground->b));
                } else if (style != nullptr && style->fg.has_value()) {
                    p.setPen(QColor((*style->fg).r, (*style->fg).g, (*style->fg).b));
                } else {
                    p.setPen(kInk);
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
                    flags = (flags & ~int(Qt::AlignVCenter)) | int(Qt::AlignTop)
                        | int(Qt::TextWordWrap);
                }
                p.drawText(cell.adjusted(4, 1, -4, -1), flags, text);
            }
            // A comment marker: the small corner triangle every spreadsheet
            // uses. The text itself is the hover tooltip.
            if (sheet.commentAt(addr) != nullptr) {
                const QPolygonF marker({cell.topRight() + QPointF(-7, 1),
                                        cell.topRight() + QPointF(-1, 1),
                                        cell.topRight() + QPointF(-1, 7)});
                p.setPen(Qt::NoPen);
                p.setBrush(kCommentMarker);
                p.drawPolygon(marker);
                p.setBrush(Qt::NoBrush);
            }
            // Filter dropdown on the header row of an active filter.
            const AutoFilter &filter = sheet.autoFilter();
            if (filter.active && filter.range.isValid()
                && addr.row == filter.range.topLeft.row
                && addr.col >= filter.range.topLeft.col
                && addr.col <= filter.range.bottomRight.col) {
                const QRectF button(cell.right() - kFilterButtonPx, cell.top() + 2,
                                    kFilterButtonPx - 2, cell.height() - 4);
                p.setPen(m_headerLineColor);
                p.setBrush(filter.allowed.contains(addr.col) ? m_selectionColor : m_headerColor);
                p.drawRect(button);
                p.setPen(m_headerTextColor);
                const QPointF centre = button.center();
                p.drawPolygon(QPolygonF({centre + QPointF(-3, -1.5), centre + QPointF(3, -1.5),
                                         centre + QPointF(0, 2.5)}));
                p.setBrush(Qt::NoBrush);
            }
            p.setPen(kGridLine);
            p.drawLine(cell.bottomLeft(), cell.bottomRight());
            p.drawLine(cell.topRight(), cell.bottomRight());
            if (style != nullptr && style->borders != 0) {
                p.setPen(QPen(kInk, 1));
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

    // Cursor outline last, in GRID coordinates (see the file comment).
    p.setPen(QPen(m_cursorColor, 2));
    p.setBrush(Qt::NoBrush);
    p.drawRect(cellRect(m_cursor));

    // The frozen split, so the user can see where the sheet stops scrolling.
    if (m_frozen.row > 0 || m_frozen.col > 0) {
        p.setPen(QPen(m_headerLineColor.darker(150), 2));
        if (m_frozen.row > 0) {
            const qreal y = frozenHeightPx();
            p.drawLine(QPointF(0, y), QPointF(viewportWidth(), y));
        }
        if (m_frozen.col > 0) {
            const qreal x = frozenWidthPx();
            p.drawLine(QPointF(x, 0), QPointF(x, viewportHeight()));
        }
    }
    p.restore();
}

void SpreadsheetItem::paint(QPainter *painter)
{
    painter->fillRect(QRectF(0, 0, width(), height()), kPaper);
    paintGrid(*painter);
    paintHeaderBands(*painter);
}

} // namespace QindaOffice::Apps::QindaCalc
