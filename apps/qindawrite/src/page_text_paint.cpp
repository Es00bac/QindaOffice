// SPDX-License-Identifier: GPL-3.0-or-later
// PageTextItem's painting (page_text_item.h): the paper, the body (or the
// open story's washed-out body, or a merge preview), Word's paragraph
// borders, tab leaders and list numbers, floating pictures over and under
// the text, merge field shading, the header/footer/notes decoration.

#include "page_text_item.h"

#include "docx_comments.h"
#include "docx_float.h"
#include "docx_list_labels.h"
#include "docx_merge.h"
#include "docx_paragraph_format.h"
#include "docx_text.h"

#include "page_picture_tool.h"
#include "page_story_editor.h"
#include "page_text_control.h"
#include "pdf_exporter.h"
#include "write_document.h"

#include <QAbstractTextDocumentLayout>
#include <QPainter>
#include <QTextDocument>

namespace QindaOffice::Apps::Write {

namespace {
// Paper edge: paper is paper (D-004), so this is a print-like grey, not a
// theme role.
const QColor kPaperEdge(0xc8, 0xc8, 0xcc);
// The body behind an open header or footer, washed out as Word does.
const QColor kBodyWash(0xff, 0xff, 0xff, 0x8c);
} // namespace

void PageTextItem::setPreviewDocument(QTextDocument *document)
{
    if (m_preview == document) {
        return;
    }
    if (m_preview != nullptr) {
        disconnect(m_preview->documentLayout(), nullptr, this, nullptr);
    }
    m_preview = document;
    if (document != nullptr) {
        connect(document->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this,
                [this](const QSizeF &) { updatePageMetrics(); });
    }
    updatePageMetrics();
}

void PageTextItem::setMergeHighlight(bool on)
{
    if (m_mergeHighlight != on) {
        m_mergeHighlight = on;
        update();
    }
}

void PageTextItem::paintBody(QPainter *painter, const QRectF &pageArea, bool storyOpen, bool pictures)
{
    QTextDocument *body = m_document->textDocument();
    if (pictures) {
        Docx::drawFloatingPictures(painter, *body, pageArea, true); // behind the text (M77)
    }
    if (m_mergeHighlight && !storyOpen) {
        Docx::drawMergeFieldShading(painter, *body, pageArea); // Highlight Merge Fields (M77)
    }
    if (storyOpen) {
        // The control edits a header or footer: the body as it prints, no
        // caret or selection.
        QAbstractTextDocumentLayout::PaintContext context;
        context.clip = pageArea;
        context.palette = m_control->paperPalette();
        body->documentLayout()->draw(painter, context);
        Docx::drawListMarkers(painter, *body, pageArea); // Word's numbers (M73)
    } else {
        // Comment bands under the text (display only, docx_comments.h).
        if (!m_document->comments().isEmpty()) {
            Docx::drawCommentRanges(painter, *body, m_document->commentSpans(), pageArea, m_document->activeComment());
        }
        m_control->drawContents(painter, pageArea);
    }
    if (pictures) {
        Docx::drawFloatingPictures(painter, *body, pageArea, false); // in front of the text
    }
    // Word paragraph borders and tab leaders: Qt's layout draws neither
    // (docx_text.h, docx_paragraph_format.h).
    Docx::drawParagraphBorders(painter, *body, pageArea);
    Docx::drawTabLeaders(painter, *body, pageArea);
    if (!storyOpen) {
        m_pictures->draw(painter, pageArea); // a selected picture's handles
    }
}

void PageTextItem::paintPage(QPainter *painter, int index, const QRectF &clip)
{
    const QRectF page = m_geometry.pageRect(index, width());
    if (!page.intersects(clip)) {
        return;
    }
    painter->fillRect(page, m_control->paperPalette().base().color());
    painter->setPen(kPaperEdge);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(page.adjusted(0, 0, -1, -1));

    const QSizeF size = m_document->pageSize();
    PageDecoration decoration = m_document->decoration();
    painter->save();
    painter->setClipRect(page.intersected(clip));
    painter->translate(page.topLeft());
    painter->translate(0, -index * size.height());
    const QRectF pageArea(0, index * size.height(), size.width(), size.height());
    const bool storyOpen = m_story != nullptr && m_story->isOpen();
    if (m_preview != nullptr) {
        // Preview Results (M77): the merged copy as it prints.
        QAbstractTextDocumentLayout::PaintContext context;
        context.clip = pageArea;
        context.palette = m_control->paperPalette();
        Docx::drawFloatingPictures(painter, *m_preview, pageArea, true);
        m_preview->documentLayout()->draw(painter, context);
        Docx::drawFloatingPictures(painter, *m_preview, pageArea, false);
        Docx::drawListMarkers(painter, *m_preview, pageArea);
        Docx::drawParagraphBorders(painter, *m_preview, pageArea);
        Docx::drawTabLeaders(painter, *m_preview, pageArea);
    } else {
        paintBody(painter, pageArea, storyOpen, decoration.overTextPictures);
    }
    painter->restore();
    if (storyOpen) {
        painter->fillRect(page.intersected(clip), kBodyWash);
    }

    if (m_preview != nullptr) {
        decoration.footnotes.clear(); // the body's notes, not the copy's
        decoration.endnotes = nullptr;
    }
    const bool storyHere = storyOpen && m_story->page() == index;
    if (!decoration.isEmpty()) {
        painter->save();
        painter->translate(page.topLeft());
        drawPageDecoration(painter, QRectF(QPointF(0, 0), size), decoration, index + 1, m_pageCount,
                           storyHere ? m_story->story() : nullptr);
        painter->restore();
    }
    if (storyHere) {
        m_story->drawOpenStory(painter, page);
    }
}

void PageTextItem::paint(QPainter *painter)
{
    if (m_document == nullptr || m_control == nullptr) {
        return;
    }
    painter->setRenderHint(QPainter::TextAntialiasing);
    const QRectF clip(0, 0, width(), height());
    for (int i = 0; i < m_pageCount; ++i) {
        paintPage(painter, i, clip);
    }
}

} // namespace QindaOffice::Apps::Write
