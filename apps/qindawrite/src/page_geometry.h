// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PageGeometry (plan section 7.1): the single source of truth mapping
// between document space (96-dpi px, y grows forever across the layout
// canvas's pages) and view space (zoomed pages stacked with gaps, each
// centred horizontally, as Word's Print Layout stacks them). Points in the
// vertical gaps snap to the nearest page edge so drags across a gap keep
// selecting.
//
// The canvas (M88, D-096): QTextDocument paginates with one page height;
// canvas page i covers doc-y [i*H, (i+1)*H). Shown page i is canvas page i.
// With one page size (one section) a page IS its canvas page. With Word
// sections of different sizes (write_sections.h) each page has its own size
// and shows the part of its canvas page that starts at `origin` (canvas =
// page point + origin), so a landscape section's page is wider and shorter
// than the canvas it was laid out on.

#include <QList>
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

    // One page as shown: its own size and where its top-left lies on the
    // canvas.
    struct Page {
        QSizeF size;
        QPointF origin;
        bool operator==(const Page &) const = default;
    };

    // The canvas page size (every page, while no pages are set).
    [[nodiscard]] QSizeF pageSize() const { return m_pageSize; }
    void setPageSize(const QSizeF &size);
    // Pages of their own sizes (sections); empty returns to uniform pages.
    void setPages(const QList<Page> &pages);
    [[nodiscard]] bool hasPages() const { return !m_pages.isEmpty(); }

    [[nodiscard]] qreal zoom() const { return m_zoom; }
    void setZoom(qreal zoom);

    // The canvas page height and the canvas page a document y falls on.
    [[nodiscard]] qreal pageHeight() const { return m_pageSize.height(); }
    [[nodiscard]] int pageForDocY(qreal docY) const
    {
        return qMax(0, int(docY / m_pageSize.height()));
    }
    // Page `index` (its size and canvas origin); past the set pages, the
    // last one's size continues canvas page by canvas page.
    [[nodiscard]] Page page(int index) const;

    // View rect of page `index` for a viewport of `viewportWidth`.
    [[nodiscard]] QRectF pageRect(int index, qreal viewportWidth) const;
    [[nodiscard]] QRectF pageRect(int index, int viewportWidth) const
    {
        return pageRect(index, qreal(viewportWidth));
    }
    // The page a view y falls on (gaps belong to the page above).
    [[nodiscard]] int pageAtViewY(qreal viewY) const;

    // Document point for a view point (gap points snap to the nearest page
    // edge on the vertical axis; the mapping is piecewise because of gaps).
    [[nodiscard]] QPointF viewToDoc(const QPointF &viewPos, qreal viewportWidth) const;
    // View point for a document point (never snaps).
    [[nodiscard]] QPointF docToView(const QPointF &docPos, qreal viewportWidth) const;
    [[nodiscard]] QRectF docToView(const QRectF &docRect, qreal viewportWidth) const;

    // Uniform pages: the stack height and page count for a content height.
    [[nodiscard]] qreal documentHeight(qreal contentHeight) const;
    [[nodiscard]] int pageCount(qreal contentHeight) const;
    [[nodiscard]] int scrollRange(qreal contentHeight, qreal viewportHeight) const;
    // The stack height of `pages` pages and the widest of them (zoomed).
    [[nodiscard]] qreal stackHeight(int pages) const;
    [[nodiscard]] qreal stackWidth(int pages) const;

private:
    [[nodiscard]] qreal pageTop(int index) const;

    QSizeF m_pageSize{kDefaultPageW, kDefaultPageH};
    QList<Page> m_pages;
    QList<qreal> m_tops; // unzoomed: Σ heights of the pages before each
    qreal m_zoom = 1.0;
};

} // namespace QindaOffice::Apps::Write
