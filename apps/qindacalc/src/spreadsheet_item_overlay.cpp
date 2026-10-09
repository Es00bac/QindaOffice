// SPDX-License-Identifier: GPL-3.0-or-later
// SpreadsheetItem's M78 overlays (D-087): View ▸ Page Break Preview — the
// print area outlined, what does not print greyed, page boundaries (manual
// breaks solid, automatic ones dashed) and "Page N" over each page, as
// Excel draws them — and Data ▸ Data Validation ▸ Circle Invalid Data's red
// rings. Painted inside paintGrid's translate, in GRID coordinates.

#include "spreadsheet_item.h"

#include <QPainter>
#include <QPainterPath>

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;

namespace {

// Excel's page-break blue and the grey over what does not print: document
// overlays, not chrome — Page Break Preview looks the same on every theme.
const QColor kBreakBlue(0x1f, 0x4e, 0xc8);
const QColor kOutsideArea(0x80, 0x80, 0x88, 70);
const QColor kPageNumber(0x90, 0x90, 0x98, 110);
const QColor kInvalidRing(0xd0, 0x10, 0x10);

} // namespace

void SpreadsheetItem::setPageBreakView(const std::optional<PageBreakView> &view)
{
    m_pageBreaks = view;
    update();
}

void SpreadsheetItem::setInvalidCells(const QList<CellAddr> &cells)
{
    m_invalidCells = cells;
    update();
}

void SpreadsheetItem::paintPageBreaks(QPainter &p)
{
    if (!m_pageBreaks.has_value() || !m_pageBreaks->area.isValid()) {
        return;
    }
    const PageBreakView &view = *m_pageBreaks;
    const QRectF viewport(0, 0, viewportWidth(), viewportHeight());
    const QRectF first = cellRect(view.area.topLeft);
    const QRectF last = cellRect(view.area.bottomRight);
    const QRectF area(first.topLeft(), last.bottomRight());
    // What does not print reads greyed (Excel's Page Break Preview).
    QPainterPath outside;
    outside.addRect(viewport);
    QPainterPath inside;
    inside.addRect(area);
    p.fillPath(outside.subtracted(inside), kOutsideArea);
    p.save();
    p.setClipRect(area.adjusted(-2, -2, 2, 2));
    // "Page N" in the middle of each page.
    QFont big = m_font;
    big.setPointSizeF(qMax(12.0, 28.0 * m_zoom));
    big.setBold(true);
    p.setFont(big);
    p.setPen(kPageNumber);
    for (const PageBreakView::Page &page : view.pages) {
        const QRectF body(cellRect(page.body.topLeft).topLeft(), cellRect(page.body.bottomRight).bottomRight());
        if (body.intersects(viewport)) {
            p.drawText(body, Qt::AlignCenter, tr("Page %1").arg(page.number));
        }
    }
    // Page boundaries: a manual break solid, an automatic one dashed.
    for (qsizetype i = 0; i < view.rowStarts.size(); ++i) {
        const qint32 row = view.rowStarts.at(i);
        if (row <= view.area.topLeft.row || row > view.area.bottomRight.row) {
            continue;
        }
        const qreal y = cellRect({row, view.area.topLeft.col}).top();
        p.setPen(QPen(kBreakBlue, 2, view.rowManual.value(i) ? Qt::SolidLine : Qt::DashLine));
        p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
    }
    for (qsizetype i = 0; i < view.colStarts.size(); ++i) {
        const qint32 col = view.colStarts.at(i);
        if (col <= view.area.topLeft.col || col > view.area.bottomRight.col) {
            continue;
        }
        const qreal x = cellRect({view.area.topLeft.row, col}).left();
        p.setPen(QPen(kBreakBlue, 2, view.colManual.value(i) ? Qt::SolidLine : Qt::DashLine));
        p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
    }
    p.restore();
    // The print area's own border.
    p.setPen(QPen(kBreakBlue, 3));
    p.setBrush(Qt::NoBrush);
    p.drawRect(area);
}

void SpreadsheetItem::paintInvalidCircles(QPainter &p)
{
    if (m_invalidCells.isEmpty()) {
        return;
    }
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(kInvalidRing, 1.6));
    p.setBrush(Qt::NoBrush);
    for (const CellAddr &addr : std::as_const(m_invalidCells)) {
        const QRectF cell = cellRect(addr);
        if (cell.width() > 0 && cell.height() > 0) {
            p.drawEllipse(cell.adjusted(-3, -2, 3, 2));
        }
    }
    p.restore();
}

} // namespace QindaOffice::Apps::QindaCalc
