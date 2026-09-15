// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PageGeometry (plan section 7.1): the single source of truth mapping
// between document space (96-dpi px, y grows forever across pages) and
// view space (zoomed pages stacked with gaps). Page i occupies
// doc-y in [i*H, (i+1)*H); in the view it is centred horizontally with a
// fixed gap above the first page. Points in the vertical gaps snap to the
// nearest page edge so drags across a gap keep selecting.

#include <QRectF>
#include <QSizeF>

namespace QindaOffice::Apps::Write {

class PageGeometry {
public:
    // A4 at 96 dpi (plan section 7.1). Letter and other sizes arrive with
    // the Page Setup dialog; the geometry is size-agnostic.
    static constexpr qreal kDefaultPageW = 794.0;
    static constexpr qreal kDefaultPageH = 1123.0;
    static constexpr qreal kGap = 24.0;

    [[nodiscard]] QSizeF pageSize() const { return m_pageSize; }
    void setPageSize(const QSizeF &size) { m_pageSize = size; }

    [[nodiscard]] qreal zoom() const { return m_zoom; }
    void setZoom(qreal zoom) { m_zoom = qBound(0.25, zoom, 4.0); }

    [[nodiscard]] qreal pageHeight() const { return m_pageSize.height(); }
    [[nodiscard]] int pageForDocY(qreal docY) const
    {
        return qMax(0, int(docY / m_pageSize.height()));
    }

    // View rect of page `index` for a viewport of `viewportWidth`.
    [[nodiscard]] QRectF pageRect(int index, qreal viewportWidth) const;
    [[nodiscard]] QRectF pageRect(int index, int viewportWidth) const
    {
        return pageRect(index, qreal(viewportWidth));
    }

    // Document point for a view point (gap points snap to the nearest page
    // edge on the vertical axis; the mapping is piecewise because of gaps).
    [[nodiscard]] QPointF viewToDoc(const QPointF &viewPos, qreal viewportWidth) const;
    // View point for a document point (never snaps).
    [[nodiscard]] QPointF docToView(const QPointF &docPos, qreal viewportWidth) const;
    [[nodiscard]] QRectF docToView(const QRectF &docRect, qreal viewportWidth) const;

    [[nodiscard]] qreal documentHeight(qreal contentHeight) const;
    [[nodiscard]] int pageCount(qreal contentHeight) const;
    [[nodiscard]] int scrollRange(qreal contentHeight, qreal viewportHeight) const;

private:
    QSizeF m_pageSize{kDefaultPageW, kDefaultPageH};
    qreal m_zoom = 1.0;
};

} // namespace QindaOffice::Apps::Write
