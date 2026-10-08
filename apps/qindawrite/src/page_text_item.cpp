// SPDX-License-Identifier: GPL-3.0-or-later
#include "page_text_item.h"

#include "docx_text.h"

#include "page_text_control.h"
#include "pdf_exporter.h"
#include "style_catalog.h"
#include "write_document.h"

#include <QAbstractTextDocumentLayout>
#include <QCursor>
#include <QFocusEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTextDocument>

namespace QindaOffice::Apps::Write {

namespace {
// Paper edge: paper is paper (D-004), so this is a print-like grey, not a
// theme role.
const QColor kPaperEdge(0xc8, 0xc8, 0xcc);
} // namespace

PageTextItem::PageTextItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setAcceptHoverEvents(true);
    setFlag(ItemAcceptsInputMethod, true);
    setFlag(ItemIsFocusScope, false);
    setCursor(QCursor(Qt::IBeamCursor));
    setAntialiasing(true);
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
    setImplicitWidth(PageGeometry::kDefaultPageW);
    setImplicitHeight(m_geometry.documentHeight(0));
}

PageTextItem::~PageTextItem() = default;

void PageTextItem::attach(WriteDocument *document, PageTextControl *control)
{
    m_document = document;
    m_control = control;
    m_geometry.setPageSize(document->pageSize());
    connect(control, &PageTextControl::updateRequest, this, [this] { update(); });
    connect(control, &PageTextControl::visibilityRequest, this, [this](const QRectF &docRect) {
        emit ensureVisibleRequested(docToView(docRect).adjusted(-8, -40, 8, 40));
    });
    connect(control, &PageTextControl::microFocusChanged, this,
            [this] { updateInputMethod(Qt::ImQueryInput); });
    connect(control, &PageTextControl::doubleClicked, this, &PageTextItem::doubleClicked);
    connect(document->textDocument()->documentLayout(),
            &QAbstractTextDocumentLayout::documentSizeChanged, this,
            [this](const QSizeF &) { updatePageMetrics(); });
    connect(document, &WriteDocument::pageSetupChanged, this, [this] {
        m_geometry.setPageSize(m_document->pageSize());
        updatePageMetrics();
    });
    connect(document, &WriteDocument::contentChanged, this, [this] { update(); });
    updatePageMetrics();
    emit attachedChanged();
}

int PageTextItem::pageCount() const
{
    return m_pageCount;
}

void PageTextItem::updatePageMetrics()
{
    if (m_document == nullptr) {
        return;
    }
    const qreal contentHeight =
        m_document->textDocument()->documentLayout()->documentSize().height();
    const int pages = m_geometry.pageCount(contentHeight);
    setImplicitWidth(m_geometry.pageSize().width());
    setImplicitHeight(m_geometry.documentHeight(contentHeight));
    if (pages != m_pageCount) {
        m_pageCount = pages;
        emit pageCountChanged();
    }
    update();
}

void PageTextItem::setViewZoom(qreal zoom)
{
    if (qFuzzyCompare(m_viewZoom, zoom)) {
        return;
    }
    m_viewZoom = zoom;
    emit viewZoomChanged();
}

// ---- mapping ------------------------------------------------------------------------

QPointF PageTextItem::viewToDoc(const QPointF &viewPos) const
{
    return m_geometry.viewToDoc(viewPos, width());
}

QPointF PageTextItem::docToView(const QPointF &docPos) const
{
    return m_geometry.docToView(docPos, width());
}

QRectF PageTextItem::docToView(const QRectF &docRect) const
{
    return m_geometry.docToView(docRect, width());
}

QRectF PageTextItem::cursorViewRect() const
{
    return m_control != nullptr ? docToView(m_control->cursorRect()) : QRectF();
}

// ---- painting -----------------------------------------------------------------------

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
    painter->save();
    painter->setClipRect(page.intersected(clip));
    painter->translate(page.topLeft());
    painter->translate(0, -index * size.height());
    const QRectF pageArea(0, index * size.height(), size.width(), size.height());
    m_control->drawContents(painter, pageArea);
    // Word paragraph borders: Qt's layout draws none (docx_text.h).
    Docx::drawParagraphBorders(painter, *m_document->textDocument(), pageArea);
    painter->restore();

    const PageDecoration decoration = m_document->decoration();
    if (!decoration.isEmpty()) {
        painter->save();
        painter->translate(page.topLeft());
        drawPageDecoration(painter, QRectF(QPointF(0, 0), size), decoration, index + 1, m_pageCount);
        painter->restore();
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

// ---- events -------------------------------------------------------------------------

void PageTextItem::forwardMouse(QMouseEvent *event)
{
    if (m_control == nullptr) {
        event->ignore();
        return;
    }
    // AGENT-CONTRACT: the control speaks document coordinates; gap points
    // snap to the nearest page edge through viewToDoc.
    QMouseEvent copy(event->type(), viewToDoc(event->position()), event->globalPosition(),
                     event->button(), event->buttons(), event->modifiers());
    event->setAccepted(m_control->processEvent(&copy));
}

void PageTextItem::mousePressEvent(QMouseEvent *event)
{
    forceActiveFocus(Qt::MouseFocusReason);
    if (event->button() == Qt::RightButton) {
        // A context menu never moves the insertion point.
        emit contextMenuRequested(event->position().x(), event->position().y(),
                                  viewToDoc(event->position()));
        event->accept();
        return;
    }
    forwardMouse(event);
}

void PageTextItem::mouseMoveEvent(QMouseEvent *event)
{
    forwardMouse(event);
}

void PageTextItem::mouseReleaseEvent(QMouseEvent *event)
{
    forwardMouse(event);
}

void PageTextItem::mouseDoubleClickEvent(QMouseEvent *event)
{
    forwardMouse(event);
}

void PageTextItem::keyPressEvent(QKeyEvent *event)
{
    if (m_control == nullptr) {
        event->ignore();
        return;
    }
    const bool newParagraph = (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && (event->modifiers() & Qt::ShiftModifier) == Qt::NoModifier;
    const int styleBefore =
        newParagraph ? classifyBlockStyle(m_control->textCursor().block()) : StyleCatalog::Normal;
    event->setAccepted(m_control->processEvent(event));
    if (!newParagraph) {
        return;
    }
    // AGENT-NOTE: Enter at the end of a heading starts BODY text, not
    // another heading. Qt carries the block format forward, so without this
    // everything typed after a heading stayed a heading — which is also why
    // the table of contents listed body paragraphs as entries.
    const bool wasHeading = styleBefore >= StyleCatalog::Title && styleBefore <= StyleCatalog::Heading6;
    if (!wasHeading || !m_control->textCursor().block().text().isEmpty()) {
        return;
    }
    StyleCatalog catalog;
    catalog.apply(m_control->textCursor(), StyleCatalog::Normal);
}

void PageTextItem::inputMethodEvent(QInputMethodEvent *event)
{
    if (m_control == nullptr) {
        event->ignore();
        return;
    }
    m_control->processEvent(event);
    event->accept();
}

QVariant PageTextItem::inputMethodQuery(Qt::InputMethodQuery query) const
{
    if (m_control == nullptr) {
        return QQuickPaintedItem::inputMethodQuery(query);
    }
    const QVariant value = m_control->inputMethodQuery(query);
    if (query == Qt::ImCursorRectangle) {
        return docToView(value.toRectF());
    }
    return value;
}

void PageTextItem::focusInEvent(QFocusEvent *event)
{
    QQuickPaintedItem::focusInEvent(event);
    if (m_control != nullptr) {
        m_control->setFocus(true);
    }
}

void PageTextItem::focusOutEvent(QFocusEvent *event)
{
    QQuickPaintedItem::focusOutEvent(event);
    if (m_control != nullptr) {
        m_control->setFocus(false);
    }
}

} // namespace QindaOffice::Apps::Write
