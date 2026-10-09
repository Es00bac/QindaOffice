// SPDX-License-Identifier: GPL-3.0-or-later
#include "form_renderer.h"

#include "company_file.h"
#include "paint_util.h"

#include <QImage>
#include <QPagedPaintDevice>
#include <QPainter>

namespace QindaOffice::Books {

using namespace Paint;

namespace {

// AGENT-NOTE: layout is measured with the same functions that draw (a
// null painter measures), so pagination can never disagree with paint.
struct Fonts {
    QFont company = font(15, QFont::Bold);
    QFont small = font(9);
    QFont title = font(24, QFont::Bold);
    QFont label = font(8, QFont::DemiBold);
    QFont body = font(10);
    QFont bodyBold = font(10, QFont::DemiBold);
    QFont partyName = font(11, QFont::DemiBold);
    QFont highlight = font(20, QFont::Bold);
    QFont emphasis = font(11, QFont::Bold);
    Fonts()
    {
        title.setLetterSpacing(QFont::PercentageSpacing, 104);
        label.setLetterSpacing(QFont::PercentageSpacing, 110);
    }
};

struct Page {
    int first = 0;
    int last = -1;
    bool trailing = false;
};

struct Layout {
    QRectF content;
    qreal firstTableTop = 0;
    qreal contTableTop = 0;
    qreal bottom = 0;
    qreal headerH = 0;
    QList<qreal> colX;
    QList<qreal> colW;
    QList<qreal> rowH;
    qreal trailingH = 0;
    QList<Page> pages;
};

constexpr qreal kCellPad = 8;
constexpr qreal kTotalsWidth = 250;

qreal drawFirstHeader(QPainter *p, const PrintableDocument &d, const QRectF &c, const Fonts &f)
{
    qreal left = c.top();
    const QImage logo = d.logo.isEmpty() ? QImage() : QImage::fromData(d.logo);
    if (!logo.isNull()) {
        QSizeF size = logo.size();
        size.scale(150, 46, Qt::KeepAspectRatio);
        if (p != nullptr) {
            p->drawImage(QRectF(QPointF(c.left(), left), size), logo);
        }
        left += size.height() + 10;
    }
    if (p != nullptr) {
        drawText(p, c.left(), left, d.companyName, f.company);
    }
    left += lineHeight(f.company) + 3;
    QStringList lines = d.companyLines;
    if (!d.taxId.isEmpty()) {
        lines << QStringLiteral("Tax ID ") + d.taxId;
    }
    for (const QString &line : std::as_const(lines)) {
        if (p != nullptr) {
            drawText(p, c.left(), left, elide(f.small, line, c.width() * 0.55), f.small, kMuted);
        }
        left += lineHeight(f.small) + 1.5;
    }
    // Right: the title in the accent colour, then the facts.
    qreal right = c.top() - 3;
    if (p != nullptr) {
        drawText(p, c.right(), right, d.title, f.title, d.accent, true);
    }
    right += lineHeight(f.title) + 8;
    qreal valueW = 96;
    for (const auto &m : d.meta) {
        valueW = qMax(valueW, textWidth(f.body, m.second));
    }
    for (const auto &m : d.meta) {
        if (p != nullptr) {
            drawText(p, c.right() - valueW - 14, right + 1, m.first, f.small, kMuted, true);
            drawText(p, c.right(), right, m.second, f.body, kInk, true);
        }
        right += lineHeight(f.body) + 3;
    }
    const qreal bottom = qMax(left, right) + 14;
    if (p != nullptr) {
        p->setPen(QPen(kRule, 0.6));
        p->drawLine(QPointF(c.left(), bottom), QPointF(c.right(), bottom));
    }
    // Party block and the highlighted amount.
    qreal party = bottom + 18;
    if (p != nullptr) {
        drawText(p, c.left(), party, d.partyLabel.toUpper(), f.label, kMuted);
    }
    party += lineHeight(f.label) + 5;
    if (!d.partyName.isEmpty()) {
        if (p != nullptr) {
            drawText(p, c.left(), party, d.partyName, f.partyName);
        }
        party += lineHeight(f.partyName) + 2;
    }
    for (const QString &line : d.partyLines) {
        if (p != nullptr) {
            drawText(p, c.left(), party, elide(f.body, line, c.width() * 0.5), f.body, kInk);
        }
        party += lineHeight(f.body) + 1.5;
    }
    // M68: a purchase order's ship-to address, under the vendor.
    if (!d.shipToLines.isEmpty()) {
        party += 10;
        if (p != nullptr) {
            drawText(p, c.left(), party, d.shipToLabel.toUpper(), f.label, kMuted);
        }
        party += lineHeight(f.label) + 5;
        for (const QString &line : d.shipToLines) {
            if (p != nullptr) {
                drawText(p, c.left(), party, elide(f.body, line, c.width() * 0.5), f.body, kInk);
            }
            party += lineHeight(f.body) + 1.5;
        }
    }
    qreal box = bottom + 18;
    if (!d.highlightLabel.isEmpty()) {
        const qreal h = 12 + lineHeight(f.small) + 4 + lineHeight(f.highlight) + 12;
        const QRectF rect(c.right() - 220, box, 220, h);
        if (p != nullptr) {
            QColor fill = d.accent;
            fill.setAlpha(24);
            p->setPen(Qt::NoPen);
            p->setBrush(fill);
            p->drawRoundedRect(rect, 5, 5);
            p->setBrush(Qt::NoBrush);
            QColor bar = d.accent;
            p->fillRect(QRectF(rect.left(), rect.top(), 3, rect.height()), bar);
            drawText(p, rect.left() + 16, rect.top() + 12, d.highlightLabel, f.small, kMuted);
            drawText(p, rect.right() - 14, rect.top() + 12 + lineHeight(f.small) + 4, d.highlightAmount, f.highlight,
                     kInk, true);
        }
        box += h;
    }
    return qMax(party, box) + 24;
}

qreal drawContinuationHeader(QPainter *p, const PrintableDocument &d, const QRectF &c, const Fonts &f)
{
    if (p != nullptr) {
        drawText(p, c.left(), c.top(), d.companyName, f.bodyBold);
        const QString number = d.meta.isEmpty() ? QString() : d.meta.first().second;
        drawText(p, c.right(), c.top() + 1, d.title + QLatin1Char(' ') + number + QStringLiteral(" · continued"),
                 f.small, kMuted, true);
        p->setPen(QPen(kRule, 0.6));
        p->drawLine(QPointF(c.left(), c.top() + 20), QPointF(c.right(), c.top() + 20));
    }
    return c.top() + 34;
}

qreal drawTableHeader(QPainter *p, const PrintableDocument &d, const Layout &l, qreal top, const Fonts &f)
{
    const qreal h = lineHeight(f.label) + 12;
    if (p != nullptr) {
        for (int i = 0; i < d.columns.size(); ++i) {
            const PrintableColumn &col = d.columns[i];
            const qreal x = col.alignRight ? l.colX[i] + l.colW[i] - (i == d.columns.size() - 1 ? 0 : kCellPad)
                                           : l.colX[i] + (i == 0 ? 0 : kCellPad);
            drawText(p, x, top + 5, col.title.toUpper(), f.label, kMuted, col.alignRight);
        }
        p->setPen(QPen(d.accent, 1.2));
        p->drawLine(QPointF(l.content.left(), top + h), QPointF(l.content.right(), top + h));
    }
    return h;
}

qreal rowHeight(const PrintableDocument &d, const Layout &l, int row, const Fonts &f)
{
    qreal h = lineHeight(f.body);
    const QStringList &cells = d.rows[row];
    for (int i = 0; i < d.columns.size() && i < cells.size(); ++i) {
        if (!d.columns[i].alignRight) {
            h = qMax(h, wrappedHeight(f.body, cells[i], l.colW[i] - kCellPad * 2));
        }
    }
    return h + 14;
}

void drawRow(QPainter *p, const PrintableDocument &d, const Layout &l, int row, qreal top, const Fonts &f)
{
    const QStringList &cells = d.rows[row];
    for (int i = 0; i < d.columns.size() && i < cells.size(); ++i) {
        const bool last = i == d.columns.size() - 1;
        if (d.columns[i].alignRight) {
            drawText(p, l.colX[i] + l.colW[i] - (last ? 0 : kCellPad), top + 7, cells[i], f.body, kInk, true);
        } else {
            const qreal x = l.colX[i] + (i == 0 ? 0 : kCellPad);
            p->setFont(f.body);
            p->setPen(kInk);
            p->drawText(QRectF(x, top + 7, l.colW[i] - kCellPad * 2, 1e5), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop,
                        cells[i]);
        }
    }
    p->setPen(QPen(kRule, 0.5));
    p->drawLine(QPointF(l.content.left(), top + l.rowH[row]), QPointF(l.content.right(), top + l.rowH[row]));
}

qreal drawTrailing(QPainter *p, const PrintableDocument &d, const QRectF &c, qreal top, const Fonts &f)
{
    // Totals on the right.
    qreal y = top;
    const qreal tl = c.right() - kTotalsWidth;
    for (int i = 0; i < d.totals.size(); ++i) {
        const PrintableTotal &t = d.totals[i];
        if (t.emphasis) {
            y += 4;
            if (p != nullptr) {
                const bool last = i == d.totals.size() - 1;
                p->setPen(QPen(last ? d.accent : kInk, last ? 1.2 : 0.7));
                p->drawLine(QPointF(tl, y), QPointF(c.right(), y));
            }
            y += 6;
        }
        const QFont &fontFor = t.emphasis ? f.emphasis : f.body;
        if (p != nullptr) {
            drawText(p, tl, y, t.label, t.emphasis ? f.emphasis : f.small, t.emphasis ? kInk : kMuted);
            drawText(p, c.right(), y, t.amount, fontFor, kInk, true);
        }
        y += lineHeight(fontFor) + 5;
    }
    // Notes and payment instructions on the left.
    qreal n = top;
    const qreal nw = c.width() - kTotalsWidth - 36;
    auto note = [&](const QString &heading, const QString &text) {
        if (text.trimmed().isEmpty()) {
            return;
        }
        if (p != nullptr) {
            drawText(p, c.left(), n, heading, f.label, kMuted);
        }
        n += lineHeight(f.label) + 4;
        const qreal h = wrappedHeight(f.small, text, nw);
        if (p != nullptr) {
            p->setFont(f.small);
            p->setPen(kInk);
            p->drawText(QRectF(c.left(), n, nw, h + 2), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, text);
        }
        n += h + 14;
    };
    note(QStringLiteral("NOTES"), d.message);
    // "Pay online: <url>" sits under the instructions (docs/books.md, M48).
    const QString payOnline = d.payOnlineUrl.isEmpty() ? QString() : QStringLiteral("Pay online: ") + d.payOnlineUrl;
    note(QStringLiteral("PAYMENT INSTRUCTIONS"),
         d.paymentInstructions.trimmed().isEmpty() ? payOnline
         : payOnline.isEmpty()                    ? d.paymentInstructions
                                                  : d.paymentInstructions + QLatin1Char('\n') + payOnline);
    qreal bottom = qMax(y, n);
    if (!d.strip.isEmpty()) {
        bottom += 16;
        const qreal cellW = c.width() / d.strip.size();
        const qreal h = 10 + lineHeight(f.label) + 4 + lineHeight(f.bodyBold) + 10;
        if (p != nullptr) {
            p->setPen(QPen(kRule, 0.6));
            p->drawLine(QPointF(c.left(), bottom), QPointF(c.right(), bottom));
            p->drawLine(QPointF(c.left(), bottom + h), QPointF(c.right(), bottom + h));
            for (int i = 0; i < d.strip.size(); ++i) {
                const qreal x = c.left() + cellW * i;
                drawText(p, x + (i == 0 ? 0 : 10), bottom + 10, d.strip[i].first.toUpper(), f.label, kMuted);
                drawText(p, x + (i == 0 ? 0 : 10), bottom + 14 + lineHeight(f.label), d.strip[i].second, f.bodyBold);
            }
        }
        bottom += h;
    }
    return bottom - top;
}

void drawFooter(QPainter *p, const PrintableDocument &d, const QRectF &c, int page, int pages, const Fonts &f)
{
    const qreal y = c.bottom() - lineHeight(f.small);
    const QString pageText = QStringLiteral("Page %1 of %2").arg(page).arg(pages);
    drawText(p, c.left(), y, elide(f.small, d.footer, c.width() - textWidth(f.small, pageText) - 24), f.small, kMuted);
    drawText(p, c.right(), y, pageText, f.small, kMuted, true);
}

Layout layoutFor(const PrintableDocument &d, const QRectF &content, const Fonts &f)
{
    Layout l;
    l.content = content;
    l.firstTableTop = drawFirstHeader(nullptr, d, content, f);
    l.contTableTop = drawContinuationHeader(nullptr, d, content, f);
    l.bottom = content.bottom() - lineHeight(f.small) - 16;
    int weights = 0;
    for (const PrintableColumn &c : d.columns) {
        weights += qMax(1, c.weight);
    }
    qreal x = content.left();
    for (const PrintableColumn &c : d.columns) {
        const qreal w = content.width() * qMax(1, c.weight) / qMax(1, weights);
        l.colX << x;
        l.colW << w;
        x += w;
    }
    l.headerH = drawTableHeader(nullptr, d, l, 0, f);
    for (int r = 0; r < d.rows.size(); ++r) {
        l.rowH << rowHeight(d, l, r, f);
    }
    l.trailingH = drawTrailing(nullptr, d, content, 0, f);
    // Rows fill pages; the trailing block must fit whole after the last row.
    qreal y = l.firstTableTop + l.headerH;
    int start = 0;
    for (int r = 0; r < d.rows.size(); ++r) {
        if (y + l.rowH[r] > l.bottom && r > start) {
            l.pages << Page{start, r - 1, false};
            start = r;
            y = l.contTableTop + l.headerH;
        }
        y += l.rowH[r];
    }
    if (y + 18 + l.trailingH > l.bottom && !d.rows.isEmpty()) {
        l.pages << Page{start, int(d.rows.size()) - 1, false};
        l.pages << Page{int(d.rows.size()), int(d.rows.size()) - 1, true};
    } else {
        l.pages << Page{start, int(d.rows.size()) - 1, true};
    }
    return l;
}

QRectF contentFor(const QPageSize &pageSize)
{
    const QRectF full = pageSize.rect(QPageSize::Point);
    return full.adjusted(kMinMargin, kMinMargin, -kMinMargin, -kMinMargin);
}

} // namespace

void FormRenderer::paint(QPagedPaintDevice *device, QPainter *painter, const PrintableDocument &d)
{
    const Fonts f;
    const QRectF content = beginPage(device, painter);
    const Layout l = layoutFor(d, content, f);
    painter->setRenderHint(QPainter::Antialiasing);
    for (int i = 0; i < l.pages.size(); ++i) {
        if (i > 0) {
            device->newPage();
            beginPage(device, painter);
        }
        const Page &page = l.pages[i];
        qreal y = i == 0 ? drawFirstHeader(painter, d, content, f) : drawContinuationHeader(painter, d, content, f);
        if (page.last >= page.first) {
            y += drawTableHeader(painter, d, l, y, f);
            for (int r = page.first; r <= page.last; ++r) {
                drawRow(painter, d, l, r, y, f);
                y += l.rowH[r];
            }
        } else if (i == 0 && d.rows.isEmpty()) {
            y += drawTableHeader(painter, d, l, y, f);
        }
        if (page.trailing) {
            drawTrailing(painter, d, content, y + 18, f);
        }
        drawFooter(painter, d, content, i + 1, int(l.pages.size()), f);
    }
}

PagePainter FormRenderer::painter(const PrintableDocument &document)
{
    return [document](QPagedPaintDevice *device, QPainter *painter) { paint(device, painter, document); };
}

int FormRenderer::pageCount(const PrintableDocument &document, const QPageSize &pageSize)
{
    return int(layoutFor(document, contentFor(pageSize), Fonts()).pages.size());
}

bool FormRenderer::writeDocumentPdf(CompanyFile &file, qint64 docId, const QString &path, QString *error)
{
    PrintableDocument doc;
    if (!buildPrintableDocument(file, docId, &doc, error)) {
        return false;
    }
    return writePdf(path, painter(doc), defaultPageSize(doc.locale), QPageLayout::Portrait, error);
}

bool FormRenderer::writeStatementPdf(CompanyFile &file, qint64 customerId, const QDate &from, const QDate &to,
                                     const QString &path, QString *error)
{
    PrintableDocument doc;
    if (!buildPrintableStatement(file, customerId, from, to, &doc, error)) {
        return false;
    }
    return writePdf(path, painter(doc), defaultPageSize(doc.locale), QPageLayout::Portrait, error);
}

} // namespace QindaOffice::Books
