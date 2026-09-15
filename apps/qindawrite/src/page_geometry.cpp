// SPDX-License-Identifier: GPL-3.0-or-later
#include "page_geometry.h"

namespace QindaOffice::Apps::Write {

QRectF PageGeometry::pageRect(int index, qreal viewportWidth) const
{
    const qreal w = m_pageSize.width() * m_zoom;
    const qreal h = m_pageSize.height() * m_zoom;
    const qreal x = (viewportWidth - w) / 2.0;
    const qreal y = kGap + index * (h + kGap);
    return {x, y, w, h};
}

QPointF PageGeometry::docToView(const QPointF &docPos, qreal viewportWidth) const
{
    const int page = pageForDocY(docPos.y());
    const QRectF rect = pageRect(page, viewportWidth);
    return {rect.x() + docPos.x() * m_zoom,
            rect.y() + (docPos.y() - page * m_pageSize.height()) * m_zoom};
}

QRectF PageGeometry::docToView(const QRectF &docRect, qreal viewportWidth) const
{
    // Rects may span pages; map the top-left and scale the size (callers
    // clip per page when painting).
    const QPointF topLeft = docToView(docRect.topLeft(), viewportWidth);
    return {topLeft, docRect.size() * m_zoom};
}

QPointF PageGeometry::viewToDoc(const QPointF &viewPos, qreal viewportWidth) const
{
    const int page = [&]() {
        const qreal h = m_pageSize.height() * m_zoom;
        const qreal denom = h + kGap;
        const int raw = int((viewPos.y() - kGap) / denom);
        const int pages = qMax(0, raw);
        return pages;
    }();
    const QRectF rect = pageRect(page, viewportWidth);
    const qreal localY = viewPos.y() - rect.y();
    const qreal localX = viewPos.x() - rect.x();
    // Gap snapping: points in the bottom gap of a page map to the page's
    // last document row instead of the next page's first row.
    const qreal docY = page * m_pageSize.height() + localY / m_zoom;
    const qreal docX = localX / m_zoom;
    return {docX, docY};
}

qreal PageGeometry::documentHeight(qreal contentHeight) const
{
    const int pages = pageCount(contentHeight);
    return kGap + pages * (m_pageSize.height() * m_zoom + kGap);
}

int PageGeometry::pageCount(qreal contentHeight) const
{
    return qMax(1, int(std::ceil(contentHeight / m_pageSize.height())));
}

int PageGeometry::scrollRange(qreal contentHeight, qreal viewportHeight) const
{
    return int(qMax<qreal>(0, documentHeight(contentHeight) - viewportHeight));
}

} // namespace QindaOffice::Apps::Write
