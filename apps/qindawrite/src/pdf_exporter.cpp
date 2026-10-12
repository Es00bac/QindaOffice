// SPDX-License-Identifier: GPL-3.0-or-later
#include "pdf_exporter.h"

#include "docx_float.h"
#include "docx_list_labels.h"
#include "docx_paragraph_format.h"
#include "docx_text.h"
#include "page_geometry.h"
#include "page_setup_io.h"
#include "write_endnotes.h"

#include <QAbstractTextDocumentLayout>
#include <QFileInfo>
#include <QFont>
#include <QFontMetricsF>
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
    // AGENT-GUARD: the PDF page must match the PAGE's size, which is in
    // 96-dpi px. QPageSize takes points (72 per inch), so px × 0.75 —
    // passing px as points made every A4 export 1.33 × too large (11 × 15.6
    // in), and the earlier hardcoded A4 distorted Letter documents. Each
    // page has its own size (M88: a landscape section in a portrait
    // document): set before newPage(), as QPdfWriter asks.
    QPdfWriter writer(path);
    const QSizeF first = decoration.frame(0, document->pageSize()).size;
    writer.setPageSize(QPageSize(first * 0.75, QPageSize::Point));
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
    renderDocumentPages(
        &painter, paper, document,
        [&writer](const QSizeF &next) {
            writer.setPageSize(QPageSize(next * 0.75, QPageSize::Point));
            writer.newPage();
            return QSizeF(writer.pageLayout().paintRectPixels(96).size());
        },
        decoration);
    painter.end();
    return true;
}

namespace {

// One header or footer, page fields filled in, laid out across the text
// column. `top` is where it starts, or (top < 0) `bottom` is where it ends.
void drawStory(QPainter *painter, const QTextDocument *story, const QRectF &pageRect, const Docx::PageLayout &l,
               qreal top, qreal bottom, const Docx::PageFieldValues &fields)
{
    if (story == nullptr || story->isEmpty()) {
        return;
    }
    const std::unique_ptr<QTextDocument> shown = Docx::instantiateHeaderFooter(*story, fields);
    const qreal width = qMax<qreal>(48, pageRect.width() - l.margins.left() - l.margins.right() - l.gutter);
    shown->setDocumentMargin(0);
    shown->setTextWidth(width);
    const qreal height = shown->size().height();
    const qreal y = top >= 0 ? pageRect.y() + top : pageRect.y() + bottom - height;
    painter->save();
    painter->translate(pageRect.x() + l.margins.left() + l.gutter, y);
    QAbstractTextDocumentLayout::PaintContext context;
    // Paper is white with black ink on every theme (D-004).
    context.palette.setColor(QPalette::Text, Qt::black);
    context.clip = QRectF(0, 0, width, height);
    shown->documentLayout()->draw(painter, context);
    Docx::drawTabLeaders(painter, *shown, context.clip);
    Docx::drawListMarkers(painter, *shown, context.clip);
    painter->restore();
}

} // namespace

void drawPageDecoration(QPainter *painter, const QRectF &pageRect, const PageDecoration &decoration,
                        int pageIndex, int pageCount, const QTextDocument *omit)
{
    if (decoration.isEmpty()) {
        return;
    }
    // The page's own frame (M88): its section's margins, the header and
    // footer it shows, its number.
    const PageFrame frame = decoration.frame(pageIndex - 1, pageRect.size());
    const Docx::PageLayout &l = frame.layout;
    const Docx::PageFieldValues fields{frame.number, frame.numberFormat, pageCount, frame.sectionPages};
    if (frame.header != omit) {
        drawStory(painter, frame.header, pageRect, l, l.headerDistance, 0, fields);
    }
    if (frame.footer != omit) {
        drawStory(painter, frame.footer, pageRect, l, -1, pageRect.height() - l.footerDistance, fields);
    }
    if (decoration.endnotes != nullptr) {
        drawEndnotes(painter, *decoration.endnotes, pageIndex - 1, pageRect); // M77
    }
    const QStringList notes = decoration.footnotes.value(pageIndex);
    if (notes.isEmpty()) {
        return;
    }
    // Notes sit in the bottom margin band, above the footer, with a
    // separator rule — the shape a printed footnote has. The band is fixed
    // (a growing one needs per-page re-layout, which a single QTextDocument
    // flow cannot express), so what does not fit is elided rather than
    // drawn over the footer.
    painter->save();
    QFont noteFont = painter->font();
    noteFont.setPointSize(8);
    painter->setFont(noteFont);
    painter->setPen(QPen(Qt::black));
    const QFontMetricsF metrics(noteFont);
    const qreal lineHeight = metrics.height();
    const qreal band = l.margins.bottom();
    const qreal left = pageRect.x() + l.margins.left() + l.gutter;
    const qreal width = pageRect.width() - l.margins.left() - l.margins.right() - l.gutter;
    const qreal footerRoom = frame.footer != nullptr ? lineHeight + 4 + l.footerDistance : 0;
    const qreal available = band - footerRoom - 8;
    const int maxLines = qMax(1, int(available / lineHeight));
    qreal y = pageRect.bottom() - band + 4;
    painter->drawLine(QPointF(left, y), QPointF(left + width / 3.0, y));
    y += 3;
    for (int i = 0; i < notes.size() && i < maxLines; ++i) {
        const bool last = (i == maxLines - 1) && notes.size() > maxLines;
        const QString line = last ? QStringLiteral("…") + QString::number(notes.size() - maxLines + 1)
                + QStringLiteral(" more")
                                  : notes.at(i);
        painter->drawText(QRectF(left, y, width, lineHeight), Qt::AlignLeft | Qt::AlignVCenter,
                          metrics.elidedText(line, Qt::ElideRight, width));
        y += lineHeight;
    }
    painter->restore();
}

void renderDocumentPages(QPainter *painter, const QSizeF &targetPixels, QTextDocument *document,
                         const std::function<QSizeF(const QSizeF &)> &newPage, const PageDecoration &decoration,
                         int fromPage, int toPage)
{
    // Probe-verified pattern (tst_write_probe_pagination): per page scale,
    // move the page's canvas region to the origin, clip, draw; newPage
    // between pages. Each page is its own frame (M88): a section's page
    // shows its own part of the canvas page it was laid out on.
    const QSizeF canvas = document->pageSize();
    const QAbstractTextDocumentLayout *layout = document->documentLayout();
    const int pages = decoration.pageCount(qMax(1, int(std::ceil(layout->documentSize().height() / canvas.height()))));
    const int first = qMax(0, fromPage - 1);
    const int last = toPage > 0 ? qMin(toPage, pages) : pages;
    QSizeF target = targetPixels;
    for (int i = first; i < last; ++i) {
        const PageFrame frame = decoration.frame(i, canvas);
        if (i > first && newPage != nullptr) {
            const QSizeF next = newPage(frame.size);
            if (next.isValid() && !next.isEmpty()) {
                target = next;
            }
        }
        painter->save();
        // AGENT-GUARD: a UNIFORM scale (width-derived). Two independent axes
        // distorted every document whose aspect ratio differed from the
        // target; height overflow is a page-setup concern, never a licence
        // to stretch.
        const qreal scale = frame.size.width() > 0 ? target.width() / frame.size.width() : 1.0;
        painter->scale(scale, scale);
        painter->save();
        if (frame.textBottom > 0) {
            painter->setClipRect(QRectF(0, 0, frame.size.width(), frame.textBottom));
        }
        painter->translate(-frame.origin);
        QAbstractTextDocumentLayout::PaintContext context;
        context.clip = QRectF(frame.origin, frame.size);
        // Pictures behind and in front of the text (M77, docx_float.h).
        if (decoration.overTextPictures) {
            Docx::drawFloatingPictures(painter, *document, context.clip, true);
        }
        const_cast<QAbstractTextDocumentLayout *>(layout)->draw(painter, context);
        if (decoration.overTextPictures) {
            Docx::drawFloatingPictures(painter, *document, context.clip, false);
        }
        Docx::drawParagraphBorders(painter, *document, context.clip);
        Docx::drawTabLeaders(painter, *document, context.clip);
        Docx::drawListMarkers(painter, *document, context.clip);
        painter->restore();
        drawPageDecoration(painter, QRectF(QPointF(0, 0), frame.size), decoration, i + 1, pages);
        painter->restore();
    }
}

} // namespace QindaOffice::Apps::Write
