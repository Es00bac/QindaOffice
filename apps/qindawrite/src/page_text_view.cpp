// SPDX-License-Identifier: GPL-3.0-or-later
#include "page_text_view.h"

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
        const QPointF top = m_geometry.docToView(rect.topLeft(), viewport()->width());
        QScrollBar *bar = verticalScrollBar();
        if (top.y() < 0) {
            bar->setValue(bar->value() + int(top.y()) - 40);
        } else if (top.y() > viewport()->height()) {
            bar->setValue(bar->value() + int(top.y() - viewport()->height()) + 40);
        }
    });
    connect(m_control, &QWidgetTextControl::microFocusChanged, this, [this] {
        updateMicroFocus();
    });
    connect(m_control, &QWidgetTextControl::documentSizeChanged, this, [this](const QSizeF &) {
        updateScrollBars();
        emit pageCountChanged(pageCount());
    });

    const auto *layout = document->textDocument()->documentLayout();
    connect(layout, &QAbstractTextDocumentLayout::documentSizeChanged, this,
            [this](const QSizeF &) {
                updateScrollBars();
                emit pageCountChanged(pageCount());
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
}

void PageTextView::paintEvent(QPaintEvent *event)
{
    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), palette().window().color());
    const int pages = pageCount();
    const qreal content =
        m_document->textDocument()->documentLayout()->documentSize().height();
    for (int i = 0; i < pages; ++i) {
        paintPage(painter, i, event->rect());
    }
    Q_UNUSED(content);
}

QPointF PageTextView::offsetFor(const QPointF &viewPos) const
{
    const QPointF doc = m_geometry.viewToDoc(viewPos, viewport()->width());
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
}

void PageTextView::keyPressEvent(QKeyEvent *event)
{
    m_control->processEvent(event);
    event->accept();
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
        return QVariant(m_geometry.docToView(value.toRectF(), viewport()->width()));
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
