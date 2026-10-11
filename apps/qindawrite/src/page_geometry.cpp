// SPDX-License-Identifier: GPL-3.0-or-later
#include "page_geometry.h"

#include <algorithm>
#include <cmath>

namespace QindaOffice::Apps::Write {

void PageGeometry::setPageSize(const QSizeF &size)
{
    m_pageSize = size;
}

void PageGeometry::setPages(const QList<Page> &pages)
{
    m_pages = pages;
    m_tops.clear();
    qreal top = 0;
    for (const Page &p : pages) {
        m_tops.append(top);
        top += p.size.height();
    }
    m_tops.append(top);
}

void PageGeometry::setZoom(qreal zoom)
{
    m_zoom = qBound(0.25, zoom, 4.0);
}

PageGeometry::Page PageGeometry::page(int index) const
{
    if (m_pages.isEmpty()) {
        return {m_pageSize, QPointF(0, index * m_pageSize.height())};
    }
    if (index >= 0 && index < m_pages.size()) {
        return m_pages.at(index);
    }
    Page last = m_pages.last();
    last.origin.ry() += (index - (m_pages.size() - 1)) * m_pageSize.height();
    return last;
}

qreal PageGeometry::pageTop(int index) const
{
    if (m_pages.isEmpty()) {
        return kGap + index * (m_pageSize.height() * m_zoom + kGap);
    }
    const qsizetype known = m_pages.size();
    const qreal above = index <= known ? m_tops.at(qMax(0, index))
                                       : m_tops.at(known) + (index - known) * m_pages.last().size.height();
    return kGap + above * m_zoom + index * kGap;
}

QRectF PageGeometry::pageRect(int index, qreal viewportWidth) const
{
    const QSizeF size = page(index).size * m_zoom;
    return {(viewportWidth - size.width()) / 2.0, pageTop(index), size.width(), size.height()};
}

int PageGeometry::pageAtViewY(qreal viewY) const
{
    if (m_pages.isEmpty()) {
        return qMax(0, int((viewY - kGap) / (m_pageSize.height() * m_zoom + kGap)));
    }
    // The last page whose top is at or above the point (a gap belongs to
    // the page above it).
    int low = 0;
    int high = int(m_pages.size()) - 1;
    while (low < high) {
        const int mid = (low + high + 1) / 2;
        if (pageTop(mid) <= viewY) {
            low = mid;
        } else {
            high = mid - 1;
        }
    }
    return low;
}

QPointF PageGeometry::docToView(const QPointF &docPos, qreal viewportWidth) const
{
    const int index = pageForDocY(docPos.y());
    const QRectF rect = pageRect(index, viewportWidth);
    return rect.topLeft() + (docPos - page(index).origin) * m_zoom;
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
    // Gap snapping: points in the bottom gap of a page map past the page's
    // last document row, not onto the next page's first row.
    const int index = pageAtViewY(viewPos.y());
    const QRectF rect = pageRect(index, viewportWidth);
    return (viewPos - rect.topLeft()) / m_zoom + page(index).origin;
}

qreal PageGeometry::documentHeight(qreal contentHeight) const
{
    return stackHeight(pageCount(contentHeight));
}

int PageGeometry::pageCount(qreal contentHeight) const
{
    return qMax(1, int(std::ceil(contentHeight / m_pageSize.height())));
}

int PageGeometry::scrollRange(qreal contentHeight, qreal viewportHeight) const
{
    return int(qMax<qreal>(0, documentHeight(contentHeight) - viewportHeight));
}

qreal PageGeometry::stackHeight(int pages) const
{
    return pageTop(qMax(1, pages));
}

qreal PageGeometry::stackWidth(int pages) const
{
    qreal widest = m_pages.isEmpty() ? m_pageSize.width() : 0;
    for (int i = 0; i < pages && i < m_pages.size(); ++i) {
        widest = qMax(widest, m_pages.at(i).size.width());
    }
    return widest * m_zoom;
}

} // namespace QindaOffice::Apps::Write
