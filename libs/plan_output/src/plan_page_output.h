// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The page-painting contract of QindaPlan's printed output (D-051; the
// pattern is books_io's D-046): a PagePainter paints ALL pages of one output
// onto any paged device — QPdfWriter for Export PDF, QPrinter for paper,
// the print preview's temporary PDF — so screen preview, file and paper
// cannot drift.
//
// AGENT-CONTRACT (with office_quick's PrintRenderCallback,
// libs/office_quick/src/print_service.h): the painter is already active on
// the device, positioned on page 1; a PagePainter calls device->newPage()
// between pages and never ends the painter. The app bridges with
//   PrintRenderCallback cb = asPrintCallback<QPrinter>(painter);
// plan_output itself does not link office_quick.
//
// AGENT-NOTE (device independence): every output here is first LAID OUT
// into a PageSet — a page count plus "paint page i onto a painter whose
// unit is the point" — for a given content area. pagedPainter() drives a
// PageSet over a paged device; rasterPages() paints the same pages into
// QImages (the tests' grabs, any in-app thumbnail). One layout, so a PNG
// grab and the PDF show the same page.

#include <QColor>
#include <QFont>
#include <QImage>
#include <QList>
#include <QLocale>
#include <QPageLayout>
#include <QPageSize>
#include <QRectF>
#include <QString>

#include <functional>

class QPagedPaintDevice;
class QPainter;

namespace QindaOffice::Plan {

using PagePainter = std::function<void(QPagedPaintDevice *device, QPainter *painter)>;

template<class Printer>
[[nodiscard]] std::function<void(Printer *, QPainter *)> asPrintCallback(PagePainter painter)
{
    return [painter = std::move(painter)](Printer *printer, QPainter *qp) { painter(printer, qp); };
}

// One laid-out output: `paint(painter, i)` draws page i (0-based) in
// POINTS, with the content area's top-left where the layout put it.
struct PageSet {
    int count = 0;
    std::function<void(QPainter *painter, int page)> paint;
};
// Lays an output out for a content area (points, page-local).
using PageLayout = std::function<PageSet(const QRectF &area)>;

// Minimum distance from the paper edge, in points.
inline constexpr qreal kPageMargin = 36;

// The content area of a page of `pagePoints` (margins applied).
[[nodiscard]] QRectF contentArea(const QSizeF &pagePoints);
[[nodiscard]] PagePainter pagedPainter(PageLayout layout);
// Pages painted into images at `dpi` (white paper); at most `maxPages`
// when positive.
[[nodiscard]] QList<QImage> rasterPages(const PageLayout &layout, const QSizeF &pagePoints, qreal dpi = 110,
                                        int maxPages = -1);

// US Letter where letter paper is the norm, A4 elsewhere.
[[nodiscard]] QPageSize defaultPageSize(const QLocale &locale);
// Renders `painter` to a PDF at `path` (overwrites). Zero device margins:
// the painters keep their own margins, identical on paper.
bool writePdf(const QString &path, const PagePainter &painter, const QPageSize &pageSize,
              QPageLayout::Orientation orientation = QPageLayout::Landscape, QString *error = nullptr);

// AGENT-NOTE (D-004): printed pages are paper — white page, black ink, ONE
// accent colour plus a critical red and ink greys. These literals are
// document content, not chrome, which is why they may exist at all; keep
// every one of them here.
struct PlanPaperStyle {
    QColor paper = QColor(0xFF, 0xFF, 0xFF);
    QColor ink = QColor(0x14, 0x16, 0x1A);
    QColor muted = QColor(0x5F, 0x66, 0x70);
    QColor rule = QColor(0xD9, 0xDD, 0xE2);
    QColor hairline = QColor(0xEC, 0xEE, 0xF1);
    QColor zebra = QColor(0xF7, 0xF8, 0xFA);
    QColor shade = QColor(0xF0, 0xF2, 0xF5);   // non-working days
    QColor accent = QColor(0x1F, 0x5F, 0xBF);
    QColor critical = QColor(0xC6, 0x28, 0x28);
    QColor warning = QColor(0xB2, 0x6A, 0x00);
    QColor good = QColor(0x2E, 0x7D, 0x32);
    QColor baseline = QColor(0xA7, 0xAD, 0xB5);

    [[nodiscard]] QColor tone(const QString &name) const;          // "danger"/"warning"/"good" → colour
    [[nodiscard]] QColor tint(const QColor &c, qreal amount) const; // mix toward paper
};

// The one type family (Noto Sans / DejaVu / Liberation), sized in points
// under the page transform (pixel size = point, see pagedPainter()).
[[nodiscard]] QFont paperFont(int px, QFont::Weight weight = QFont::Normal);
[[nodiscard]] qreal textWidth(const QFont &font, const QString &text);
[[nodiscard]] QString elideText(const QFont &font, const QString &text, qreal width);
// Draws `text` with its top-left at (x, top), or right-aligned to x.
void drawTextAt(QPainter *painter, qreal x, qreal top, const QString &text, const QFont &font, const QColor &color,
                bool alignRight = false);

} // namespace QindaOffice::Plan
