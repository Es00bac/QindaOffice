// SPDX-License-Identifier: GPL-3.0-or-later
#include "pdf_exporter.h"

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
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QSizeF(210, 297), QPageSize::Millimeter)); // A4
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
    if (!hasHeader && !hasFooter) {
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
    painter->scale(targetPixels.width() / page.width(), targetPixels.height() / page.height());
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
