// SPDX-License-Identifier: GPL-3.0-or-later
#include "pdf_exporter.h"

#include <QFontMetricsF>

#include "page_geometry.h"
#include "page_setup_io.h"

#include <QAbstractTextDocumentLayout>
#include <QFileInfo>
#include <QFont>
#include <QPainter>
#include <QPdfWriter>

namespace QindaOffice::Apps::Write {

bool exportDocumentPdf(QTextDocument *document, const QString &path, QString *error,
                         const PageDecoration &decoration)
{
    if (document == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("no document");
        }
        return false;
    }
    // AGENT-GUARD: the PDF page must match the DOCUMENT's page size — the
    // earlier release hardcoded A4, so Letter-sized documents came out
    // distorted (the two-axis scale in renderDocumentPages stretched x and
    // y differently).
    QPdfWriter writer(path);
    const QSizeF docPage = document->pageSize();
    const QPageSize size(QSize(docPage.width(), docPage.height()), QPageSize::Point);
    writer.setPageSize(size);
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    writer.setResolution(96);
    writer.setCreator(QStringLiteral("QindaWrite"));
    writer.setTitle(QFileInfo(path).completeBaseName());

    QPainter painter(&writer);
    if (!painter.isActive()) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot open %1 for writing").arg(path);
        }
        return false;
    }
    const QSizeF paper = writer.pageLayout().paintRectPixels(96).size();
    renderDocumentPages(&painter, paper, document, [&writer] { writer.newPage(); }, decoration);
    painter.end();
    return true;
}

void drawPageDecoration(QPainter *painter, const QRectF &pageRect, const PageDecoration &decoration,
                        int pageIndex, int pageCount)
{
    const bool hasHeader = !decoration.header.isEmpty();
    const bool hasFooter = !decoration.footer.isEmpty();
    const QStringList notes = decoration.footnotes.value(pageIndex);
    if (!hasHeader && !hasFooter && notes.isEmpty()) {
        return;
    }
    // Header/footer are document content, not chrome: small dark type in
    // the margin band, horizontally centered (matching the paper pin).
    QFont font = painter->font();
    font.setPointSize(9);
    painter->save();
    painter->setFont(font);
    painter->setPen(QPen(Qt::black));
    const qreal margin = decoration.margin;
    const QRectF contentBand(pageRect.x() + margin, pageRect.y() + margin,
                             pageRect.width() - 2 * margin, pageRect.height() - 2 * margin);
    if (hasHeader) {
        const QRectF band(contentBand.x(), pageRect.y(), contentBand.width(), margin);
        painter->drawText(band, Qt::AlignHCenter | Qt::AlignVCenter,
                          formatPageDecoration(decoration.header, pageIndex, pageCount));
    }
    if (hasFooter) {
        const QRectF band(contentBand.x(), pageRect.bottom() - margin, contentBand.width(), margin);
        painter->drawText(band, Qt::AlignHCenter | Qt::AlignVCenter,
                          formatPageDecoration(decoration.footer, pageIndex, pageCount));
    }
    if (!notes.isEmpty()) {
        // Notes sit in the bottom margin band, above the footer line, with a
        // separator rule — the shape a printed footnote has. The band is
        // fixed (a growing one needs per-page re-layout, which a single
        // QTextDocument flow cannot express), so what does not fit is
        // elided rather than drawn over the footer.
        QFont noteFont = painter->font();
        noteFont.setPointSize(8);
        painter->setFont(noteFont);
        const QFontMetricsF metrics(noteFont);
        const qreal lineHeight = metrics.height();
        const qreal footerRoom = hasFooter ? lineHeight + 4 : 0;
        const qreal available = margin - footerRoom - 8;
        const int maxLines = qMax(1, int(available / lineHeight));
        qreal y = pageRect.bottom() - margin + 4;
        painter->drawLine(QPointF(contentBand.left(), y),
                          QPointF(contentBand.left() + contentBand.width() / 3.0, y));
        y += 3;
        for (int i = 0; i < notes.size() && i < maxLines; ++i) {
            const bool last = (i == maxLines - 1) && notes.size() > maxLines;
            const QString line = last
                ? QStringLiteral("…") + QString::number(notes.size() - maxLines + 1)
                      + QStringLiteral(" more")
                : notes.at(i);
            painter->drawText(QRectF(contentBand.left(), y, contentBand.width(), lineHeight),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              metrics.elidedText(line, Qt::ElideRight, contentBand.width()));
            y += lineHeight;
        }
    }
    painter->restore();
}

void renderDocumentPages(QPainter *painter, const QSizeF &targetPixels, QTextDocument *document,
                         const std::function<void()> &newPage, const PageDecoration &decoration)
{
    // Probe-verified pattern (tst_write_probe_pagination): scale once, then
    // per page translate by -i*pageH, clip, draw; newPage between pages
    // (the painter transform persists across newPage on QPdfWriter/QPrinter).
    const QSizeF page = document->pageSize();
    painter->save();
    // AGENT-GUARD: a UNIFORM scale (width-derived). Two independent axes
    // distorted every document whose aspect ratio differed from the target;
    // height overflow is a page-setup concern, never a licence to stretch.
    const qreal uniformScale =
        page.width() > 0 ? targetPixels.width() / page.width() : 1.0;
    painter->scale(uniformScale, uniformScale);
    const QAbstractTextDocumentLayout *layout = document->documentLayout();
    const int pages = qMax(1, int(std::ceil(layout->documentSize().height() / page.height())));
    for (int i = 0; i < pages; ++i) {
        if (i > 0 && newPage != nullptr) {
            newPage();
        }
        painter->save();
        painter->translate(0, -i * page.height());
        QAbstractTextDocumentLayout::PaintContext context;
        context.clip = QRectF(0, i * page.height(), page.width(), page.height());
        const_cast<QAbstractTextDocumentLayout *>(layout)->draw(painter, context);
        drawPageDecoration(painter, QRectF(0, i * page.height(), page.width(), page.height()),
                           decoration, i + 1, pages);
        painter->restore();
    }
    painter->restore();
}

} // namespace QindaOffice::Apps::Write
