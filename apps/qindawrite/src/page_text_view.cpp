// SPDX-License-Identifier: GPL-3.0-or-later
#include "page_text_view.h"

#include "style_catalog.h"

#include "pdf_exporter.h"
#include "write_document.h"

#include <QContextMenuEvent>
#include <QFocusEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QScrollBar>
#include <QTextDocument>
#include <QWheelEvent>
#include <QtWidgets/private/qwidgettextcontrol_p.h>

namespace QindaOffice::Apps::Write {

PageTextView::PageTextView(WriteDocument *document, QWidget *parent)
    : QAbstractScrollArea(parent)
    , m_document(document)
    , m_control(new QWidgetTextControl(document->textDocument(), this))
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_InputMethodEnabled, true);
    viewport()->setAutoFillBackground(false);
    m_geometry.setPageSize(document->pageSize());

    m_control->setTextInteractionFlags(Qt::TextEditorInteraction);
    m_control->setFocus(true);

    // AGENT-NOTE (ADR-0115): the qindaqt platform theme owns the window
    // chrome, but a document page is *paper*. Every word processor renders
    // paper light with dark default text even under a dark desktop theme,
    // and the print/PDF path does exactly that. Without this pin, a dark
    // theme turns the page dark and only imported blocks with explicit
    // backgrounds stay light (the "white boxes on dark" bug). The canvas
    // between pages still follows the theme.
    QPalette paper = viewport()->palette();
    paper.setColor(QPalette::Base, Qt::white);
    paper.setColor(QPalette::Window, Qt::white);
    paper.setColor(QPalette::Text, Qt::black);
    paper.setColor(QPalette::WindowText, Qt::black);
    paper.setColor(QPalette::HighlightedText, Qt::white);
    viewport()->setPalette(paper);
    m_control->setPalette(paper);

    connect(m_control, &QWidgetTextControl::updateRequest, this, [this](const QRectF &) {
        viewport()->update();
    });
    connect(m_control, &QWidgetTextControl::visibilityRequest, this, [this](const QRectF &rect) {
        // AGENT-GUARD: docToView returns CONTENT coordinates; the on-screen
        // position is contentY - scrollValue. Over-/under-scrolling here left
        // the caret hidden above the viewport forever.
        const QPointF content = m_geometry.docToView(rect.topLeft(), viewport()->width());
        const qreal contentBottom =
            content.y() + m_geometry.docToView(rect.bottomLeft(), viewport()->width()).y()
                - content.y();
        QScrollBar *bar = verticalScrollBar();
        if (content.y() < bar->value()) {
            bar->setValue(qMax(0, int(content.y()) - 40));
        } else if (contentBottom > bar->value() + viewport()->height()) {
            bar->setValue(int(contentBottom) - viewport()->height() + 40);
        }
    });
    connect(m_control, &QWidgetTextControl::microFocusChanged, this, [this] {
        updateMicroFocus();
    });
    connect(m_control, &QWidgetTextControl::documentSizeChanged, this, [this](const QSizeF &) {
        updateScrollBars();
        emit pageCountChanged(pageCount());
    });

    // WR: the control's documentSizeChanged already relays the layout's;
    // a second connection here doubled every scrollbar update.
    // Sync page geometry whenever the document (re)configures its page —
    // load() and the Page Setup dialog go through setupPaged().
    connect(document, &WriteDocument::pageSetupChanged, this, [this] {
        m_geometry.setPageSize(m_document->pageSize());
        updateScrollBars();
        viewport()->update();
    });
    updateScrollBars();
}

PageTextView::~PageTextView()
{
    // The control borrows the document; it must die before the view's
    // document pointer goes away with the window.
    delete m_control;
}

int PageTextView::pageCount() const
{
    const qreal h = m_document->textDocument()->documentLayout()->documentSize().height();
    return m_geometry.pageCount(h);
}

void PageTextView::setZoom(qreal zoom)
{
    if (qFuzzyCompare(m_geometry.zoom(), zoom)) {
        return;
    }
    m_geometry.setZoom(zoom);
    updateScrollBars();
    viewport()->update();
    emit zoomChanged(m_geometry.zoom());
}

void PageTextView::updateScrollBars()
{
    const qreal h = m_document->textDocument()->documentLayout()->documentSize().height();
    verticalScrollBar()->setRange(0, m_geometry.scrollRange(h, viewport()->height()));
    verticalScrollBar()->setSingleStep(int(40 * m_geometry.zoom()));
    horizontalScrollBar()->setRange(0, 0); // pages are centred; v1 clips wide content
    viewport()->update();
}

void PageTextView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBars();
}

void PageTextView::paintPage(QPainter &painter, int index, const QRect &clip)
{
    // AGENT-GUARD: `clip` and the returned page rect are in CONTENT
    // coordinates (scroll space). paintEvent translates the painter by the
    // negative scroll offset first; callers must pass the dirty rect
    // translated into content space (event->rect() + scroll offset).
    const QRectF page = m_geometry.pageRect(index, viewport()->width());
    if (!page.intersects(QRectF(clip))) {
        return;
    }
    // Paper: the pinned paper base (see the ctor note), theme border.
    painter.fillRect(page, viewport()->palette().base().color());
    painter.setPen(palette().dark().color());
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(page.adjusted(0, 0, -1, -1));

    const QSizeF size = m_document->pageSize();
    painter.save();
    painter.setClipRect(page.intersected(QRectF(clip)));
    painter.translate(page.topLeft());
    painter.scale(m_geometry.zoom(), m_geometry.zoom());
    painter.translate(0, -index * size.height());
    m_control->drawContents(&painter, QRectF(0, index * size.height(), size.width(), size.height()),
                             viewport());
    painter.restore();

    // Header/footer live in the margin bands (same source as print/PDF).
    const PageDecoration decoration = m_document->decoration();
    if (!decoration.header.isEmpty() || !decoration.footer.isEmpty()
        || !decoration.footnotes.isEmpty()) {
        painter.save();
        painter.translate(page.topLeft());
        painter.scale(m_geometry.zoom(), m_geometry.zoom());
        drawPageDecoration(&painter, QRectF(QPointF(0, 0), size), decoration, index + 1,
                           pageCount());
        painter.restore();
    }
}

void PageTextView::paintEvent(QPaintEvent *event)
{
    QPainter painter(viewport());
    // Fill in widget space, then enter content space for the pages: the
    // scrollbars carry real ranges, and without this translate the canvas
    // never moved — everything below the first viewport was unreachable.
    painter.fillRect(viewport()->rect(), palette().window().color());
    painter.translate(-QPointF(horizontalScrollBar()->value(), verticalScrollBar()->value()));
    const int pages = pageCount();
    const QRect contentClip = event->rect().translated(horizontalScrollBar()->value(),
                                                       verticalScrollBar()->value());
    for (int i = 0; i < pages; ++i) {
        paintPage(painter, i, contentClip);
    }
}

QPointF PageTextView::offsetFor(const QPointF &viewPos) const
{
    // AGENT-GUARD: widget → document must pass through the scroll offset
    // (content = widget + scroll). Missing this made every click below the
    // first viewport land a viewport-height too early in the document.
    const QPointF content = viewPos + QPointF(horizontalScrollBar()->value(),
                                              verticalScrollBar()->value());
    const QPointF doc = m_geometry.viewToDoc(content, viewport()->width());
    return doc - viewPos;
}

void PageTextView::sendMouse(QMouseEvent *event)
{
    // AGENT-CONTRACT (plan section 7.1): forward the event in view
    // coordinates plus the piecewise offset for its position; gap points
    // snap to the nearest page edge through viewToDoc.
    QMouseEvent copy(event->type(), event->position(), event->globalPosition(),
                     event->button(), event->buttons(), event->modifiers());
    m_control->processEvent(&copy, offsetFor(event->position()), viewport());
    event->accept();
}

void PageTextView::mousePressEvent(QMouseEvent *event)
{
    sendMouse(event);
}

void PageTextView::mouseMoveEvent(QMouseEvent *event)
{
    sendMouse(event);
}

void PageTextView::mouseReleaseEvent(QMouseEvent *event)
{
    sendMouse(event);
}

void PageTextView::mouseDoubleClickEvent(QMouseEvent *event)
{
    sendMouse(event);
    // After the control has placed the cursor, so listeners can read the
    // position it settled on.
    emit doubleClicked(m_control->textCursor().position());
}

void PageTextView::keyPressEvent(QKeyEvent *event)
{
    const bool newParagraph = (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && (event->modifiers() & Qt::ShiftModifier) == Qt::NoModifier;
    const int styleBefore =
        newParagraph ? classifyBlockStyle(m_control->textCursor().block()) : StyleCatalog::Normal;
    m_control->processEvent(event);
    event->accept();
    if (!newParagraph) {
        return;
    }
    // AGENT-NOTE: Enter at the end of a heading starts BODY text, not
    // another heading. Qt carries the block format forward, so without this
    // everything typed after a heading stayed a heading — which is also why
    // the table of contents listed body paragraphs as entries.
    const bool wasHeading = styleBefore >= StyleCatalog::Title
        && styleBefore <= StyleCatalog::Heading6;
    if (!wasHeading || !m_control->textCursor().block().text().isEmpty()) {
        return;
    }
    StyleCatalog catalog;
    catalog.apply(m_control->textCursor(), StyleCatalog::Normal);
}

void PageTextView::keyReleaseEvent(QKeyEvent *event)
{
    m_control->processEvent(event);
    event->accept();
}

void PageTextView::inputMethodEvent(QInputMethodEvent *event)
{
    m_control->processEvent(event);
    event->accept();
}

QVariant PageTextView::inputMethodQuery(Qt::InputMethodQuery property) const
{
    const QVariant value = m_control->inputMethodQuery(property, QVariant());
    if (property == Qt::ImCursorRectangle) {
        const QRectF contentRect = m_geometry.docToView(value.toRectF(), viewport()->width());
        return QVariant(contentRect.translated(-horizontalScrollBar()->value(),
                                               -verticalScrollBar()->value()));
    }
    return value;
}

void PageTextView::focusInEvent(QFocusEvent *event)
{
    QAbstractScrollArea::focusInEvent(event);
    m_control->setFocus(true, event->reason());
    viewport()->update();
}

void PageTextView::focusOutEvent(QFocusEvent *event)
{
    QAbstractScrollArea::focusOutEvent(event);
    m_control->setFocus(false, event->reason());
    viewport()->update();
}

void PageTextView::contextMenuEvent(QContextMenuEvent *event)
{
    // The control opens its menu at the cursor; position it near the click
    // by moving the cursor there first is deliberately avoided (a context
    // menu must not move the insertion point).
    const QPointF docPos = m_geometry.viewToDoc(event->pos(), viewport()->width());
    QMenu *menu = m_control->createStandardContextMenu(QPointF(), nullptr);
    emit contextMenuBuilt(menu, docPos);
    menu->exec(event->globalPos());
    delete menu;
}

void PageTextView::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        // Transform-only zoom: no relayout, repaint only (plan 7.1).
        const qreal factor = event->angleDelta().y() > 0 ? 1.1 : 1.0 / 1.1;
        setZoom(m_geometry.zoom() * factor);
        event->accept();
        return;
    }
    QAbstractScrollArea::wheelEvent(event);
}

} // namespace QindaOffice::Apps::Write
