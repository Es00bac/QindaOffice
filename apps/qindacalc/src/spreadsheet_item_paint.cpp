// SPDX-License-Identifier: GPL-3.0-or-later
// SpreadsheetItem painting: the header bands, the grid pane by pane (M82:
// the frozen and scrolling parts, or a split's four panes — each clipped to
// itself, its cells painted by spreadsheet_item_cells.cpp) and the paint()
// that drives them. Moved from the Widgets view nearly verbatim: palette
// roles became the chrome colour properties, the document colours are
// fixed (D-004: paper stays paper).
//
// AGENT-GUARD: the grid is painted inside p.translate(headerWidth(),
// headerHeight()), so cellRect() is ALREADY correct in that block. Adding the
// header offset again there draws a full header down and to the right of the
// cell it belongs to — use cursorItemRect() only OUTSIDE the transform.

#include "spreadsheet_item.h"

#include "spreadsheet_document.h"

#include "sheet.h"

#include <QPainter>

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;

namespace {
// The sheet is paper (D-004).
const QColor kPaper(Qt::white);
} // namespace

void SpreadsheetItem::paintHeaderBands(QPainter &p)
{
    const CellRange selection = selectionRange();
    const QColor selectedHeader = m_selectionColor.lighter(170);
    // The headers sit inside the outline gutter (M66): left of the row
    // headers, above the column headers; both are zero wide when the sheet
    // is not grouped.
    const qreal left = rowGutterPx();
    const qreal top = colGutterPx();

    const QRectF corner(left, top, kHeaderWidth, kHeaderHeight);
    p.fillRect(corner, m_headerColor);
    p.setPen(m_headerLineColor);
    // The corner box selects the whole sheet: the little triangle every
    // spreadsheet uses.
    const QPolygonF marker({QPointF(left + kHeaderWidth - 6, top + 5),
                            QPointF(left + kHeaderWidth - 6, top + kHeaderHeight - 5),
                            QPointF(left + 6, top + kHeaderHeight - 5)});
    p.setBrush(m_headerTextColor);
    p.drawPolygon(marker);
    p.setBrush(Qt::NoBrush);

    p.save();
    p.translate(headerWidth(), top);
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
    p.translate(left, headerHeight());
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
    paintOutline(p);
}

void SpreadsheetItem::paintGrid(QPainter &p)
{
    if (activeSheet() == nullptr) {
        return;
    }
    p.save();
    p.translate(headerWidth(), headerHeight());
    p.setClipRect(QRectF(0, 0, viewportWidth(), viewportHeight()));
    const QVector<AxisPane> rowList = panes(true);
    const QVector<AxisPane> colList = panes(false);
    // Pane by pane, each clipped to itself (M82): a text overflowing its
    // cell stops at the pane's edge, and under a split cellRect() measures
    // in the pane being painted.
    for (int r = 0; r < rowList.size(); ++r) {
        for (int c = 0; c < colList.size(); ++c) {
            const AxisPane &rowPane = rowList.at(r);
            const AxisPane &colPane = colList.at(c);
            const QRectF area(colPane.start, rowPane.start, colPane.end - colPane.start, rowPane.end - rowPane.start);
            if (area.width() <= 0 || area.height() <= 0) {
                continue;
            }
            m_paintRowPane = r;
            m_paintColPane = c;
            p.save();
            p.setClipRect(area, Qt::IntersectClip);
            paintPaneCells(p, paneBands(rowPane, true), paneBands(colPane, false), colPane);
            paintPivotPlaceholders(p); // an empty PivotTable (M74)
            paintImages(p);
            paintPageBreaks(p);     // Page Break Preview (M78)
            paintInvalidCircles(p); // Circle Invalid Data (M78)
            paintSpillBorder(p);
            // Cursor outline last, in GRID coordinates (see the file comment).
            p.setPen(QPen(m_cursorColor, 2));
            p.setBrush(Qt::NoBrush);
            p.drawRect(cellRect(m_cursor));
            p.restore();
        }
    }
    m_paintRowPane = -1;
    m_paintColPane = -1;

    // The frozen split, so the user can see where the sheet stops scrolling.
    if (frozen().row > 0 || frozen().col > 0) {
        p.setPen(QPen(m_headerLineColor.darker(150), 2));
        if (frozen().row > 0) {
            const qreal y = rowList.last().start;
            p.drawLine(QPointF(0, y), QPointF(viewportWidth(), y));
        }
        if (frozen().col > 0) {
            const qreal x = colList.last().start;
            p.drawLine(QPointF(x, 0), QPointF(x, viewportHeight()));
        }
    }
    p.restore();
}

// Floating pictures (M39): drawn over the cells they cover and under the
// cursor, in GRID coordinates like everything else in paintGrid, so they
// scroll and freeze with the sheet. A spec with no size falls back to the
// image's own pixels.
void SpreadsheetItem::paintImages(QPainter &p)
{
    if (m_document == nullptr) {
        return;
    }
    const CalcEng::Workbook *book = m_document->workbook();
    const qint32 active = sheetIndex();
    for (const CalcEng::ImageSpec &spec : book->images) {
        if (spec.sheet != active || !spec.isValid()) {
            continue;
        }
        const auto key = quintptr(spec.bytes.data());
        auto it = m_imageCache.constFind(key);
        if (it == m_imageCache.constEnd()) {
            it = m_imageCache.insert(key, {spec.bytes, QImage::fromData(*spec.bytes)});
        }
        const QImage &image = it.value().image;
        if (image.isNull()) {
            continue; // a format this build cannot decode: leave the cells alone
        }
        const QRectF anchor = cellRect({spec.anchorRow, spec.anchorCol});
        const QSizeF size(spec.widthPx > 0 ? spec.widthPx : image.width(),
                          spec.heightPx > 0 ? spec.heightPx : image.height());
        p.drawImage(QRectF(anchor.topLeft(), size), image);
    }
}

void SpreadsheetItem::paint(QPainter *painter)
{
    painter->fillRect(QRectF(0, 0, width(), height()), kPaper);
    paintGrid(*painter);
    paintHeaderBands(*painter);
    paintSplitBars(*painter); // M82: across the headers too
}

} // namespace QindaOffice::Apps::QindaCalc
