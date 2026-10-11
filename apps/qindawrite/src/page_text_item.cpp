// SPDX-License-Identifier: GPL-3.0-or-later
#include "page_text_item.h"

#include "docx_comments.h"
#include "docx_list_labels.h"
#include "docx_paragraph_format.h"
#include "docx_text.h"

#include "page_picture_tool.h"
#include "page_story_editor.h"
#include "page_text_control.h"
#include "pdf_exporter.h"
#include "write_document.h"

#include <QAbstractTextDocumentLayout>
#include <QCursor>
#include <QFocusEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTextDocument>

namespace QindaOffice::Apps::Write {

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
    m_story = std::make_unique<StoryEditor>(document, control);
    m_pictures = std::make_unique<PictureTool>(control);
    connect(m_story.get(), &StoryEditor::openChanged, this, [this] {
        emit storyChanged();
        updateInputMethod(Qt::ImQueryInput);
        update();
    });
    connect(m_story.get(), &StoryEditor::settled, this, [this] { update(); });
    connect(control, &PageTextControl::updateRequest, this, [this] { update(); });
    connect(control, &PageTextControl::visibilityRequest, this, [this](const QRectF &rect) {
        const QRectF view = m_story->isOpen() ? storyToView(rect) : docToView(rect);
        emit ensureVisibleRequested(view.adjusted(-8, -40, 8, 40));
    });
    connect(control, &PageTextControl::microFocusChanged, this,
            [this] { updateInputMethod(Qt::ImQueryInput); });
    connect(control, &PageTextControl::doubleClicked, this, &PageTextItem::doubleClicked);
    connect(document->textDocument()->documentLayout(),
            &QAbstractTextDocumentLayout::documentSizeChanged, this,
            [this](const QSizeF &) { updatePageMetrics(); });
    connect(document, &WriteDocument::pageSetupChanged, this, [this] { updatePageMetrics(); });
    connect(document, &WriteDocument::contentChanged, this, [this] { updatePageMetrics(); });
    connect(document, &WriteDocument::commentsChanged, this, [this] { update(); });
    connect(document, &WriteDocument::activeCommentChanged, this, [this] { update(); });
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
    // Preview Results shows a merged copy (M77) on uniform pages; the body
    // shows every page at its own section's size (M88), its endnotes
    // running on past its last page (write_endnotes.h).
    int pages = 1;
    if (m_preview != nullptr) {
        m_geometry.setPageSize(m_preview->pageSize());
        m_geometry.setPages({});
        pages = m_geometry.pageCount(m_preview->documentLayout()->documentSize().height());
    } else {
        m_geometry.setPageSize(m_document->pageSize());
        const QList<PageFrame> &frames = m_document->pageFrames();
        QList<PageGeometry::Page> shown;
        bool uniform = true;
        for (qsizetype i = 0; i < frames.size(); ++i) {
            shown.append({frames.at(i).size, frames.at(i).origin});
            uniform = uniform && frames.at(i).size == m_document->pageSize()
                && frames.at(i).origin == QPointF(0, i * m_document->pageSize().height());
        }
        m_geometry.setPages(uniform ? QList<PageGeometry::Page>() : shown);
        pages = qMax(1, int(frames.size()));
    }
    setImplicitWidth(m_geometry.stackWidth(pages));
    setImplicitHeight(m_geometry.stackHeight(pages));
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
    if (m_control == nullptr) {
        return {};
    }
    return m_story != nullptr && m_story->isOpen() ? storyToView(m_control->cursorRect())
                                                   : docToView(m_control->cursorRect());
}

QRectF PageTextItem::storyToView(const QRectF &storyRect) const
{
    const QPointF page = m_geometry.pageRect(m_story->page(), width()).topLeft();
    return storyRect.translated(m_story->storyToPage(QPointF(0, 0)) + page);
}

int PageTextItem::pageAt(const QPointF &viewPos, QPointF *pagePos) const
{
    for (int i = 0; i < m_pageCount; ++i) {
        const QRectF page = m_geometry.pageRect(i, width());
        if (viewPos.y() >= page.top() && viewPos.y() <= page.bottom()) {
            if (pagePos != nullptr) {
                *pagePos = viewPos - page.topLeft();
            }
            return i;
        }
    }
    return -1;
}

int PageTextItem::caretPage() const
{
    if (m_story->isOpen()) {
        return m_story->page();
    }
    const qreal y = m_control->cursorRect().center().y();
    return qBound(0, int(y / qMax<qreal>(1, m_document->pageSize().height())), qMax(0, m_pageCount - 1));
}

// ---- headers and footers in place (page_story_editor.h) -----------------------------

bool PageTextItem::isEditingStory() const
{
    return m_story != nullptr && m_story->isOpen();
}

QString PageTextItem::storyLabel() const
{
    return m_story != nullptr ? m_story->label() : QString();
}

void PageTextItem::openStory(int kind)
{
    if (m_story == nullptr) {
        return;
    }
    const int page = m_story->isOpen() ? m_story->page() : caretPage();
    if (m_story->open(StoryEditor::Kind(kind), page)) {
        forceActiveFocus(Qt::OtherFocusReason);
        emit ensureVisibleRequested(storyToView(QRectF(QPointF(0, 0), m_story->bandRect().size())));
    }
}

void PageTextItem::editHeader()
{
    openStory(int(StoryEditor::Kind::Header));
}

void PageTextItem::editFooter()
{
    openStory(int(StoryEditor::Kind::Footer));
}

void PageTextItem::closeStory()
{
    if (m_story != nullptr) {
        m_story->close();
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

bool PageTextItem::forwardStoryMouse(QMouseEvent *event)
{
    QPointF pagePos;
    const int index = pageAt(event->position(), &pagePos);
    const bool inBand = index == m_story->page() && m_story->bandRect().adjusted(-8, -8, 8, 8).contains(pagePos);
    if (!inBand && event->type() != QEvent::MouseMove && event->type() != QEvent::MouseButtonRelease) {
        event->accept(); // Word: the body ignores single clicks while a header is open
        return false;
    }
    // Drags may leave the band: map against the story's own page.
    const QPointF onStoryPage = event->position() - m_geometry.pageRect(m_story->page(), width()).topLeft();
    QMouseEvent copy(event->type(), m_story->pageToStory(onStoryPage), event->globalPosition(), event->button(),
                     event->buttons(), event->modifiers());
    event->setAccepted(m_control->processEvent(&copy));
    return true;
}

bool PageTextItem::openStoryAt(QMouseEvent *event)
{
    QPointF pagePos;
    const int index = pageAt(event->position(), &pagePos);
    const StoryEditor::Kind zone = index >= 0 ? m_story->zoneAt(index, pagePos) : StoryEditor::Kind::None;
    if (zone == StoryEditor::Kind::None || !m_story->open(zone, index)) {
        return false;
    }
    // The caret goes where the double-click landed.
    for (const QEvent::Type type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
        QMouseEvent click(type, m_story->pageToStory(pagePos), event->globalPosition(), Qt::LeftButton,
                          type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton, event->modifiers());
        m_control->processEvent(&click);
    }
    event->accept();
    return true;
}

void PageTextItem::mousePressEvent(QMouseEvent *event)
{
    forceActiveFocus(Qt::MouseFocusReason);
    if (m_preview != nullptr) {
        event->accept(); // Preview Results is read-only (M77)
        return;
    }
    if (event->button() == Qt::LeftButton && m_story != nullptr && m_story->isOpen()) {
        forwardStoryMouse(event);
        return;
    }
    if (event->button() == Qt::LeftButton && m_pictures != nullptr) {
        // A picture's handle starts a resize; the picture itself selects it.
        const QPointF doc = viewToDoc(event->position());
        if (const int handle = m_pictures->handleAt(doc); handle >= 0 && m_pictures->beginDrag(handle, doc)) {
            event->accept();
            return;
        }
        if (const int picture = m_pictures->pictureAt(doc); picture >= 0) {
            m_pictures->select(picture);
            m_pictures->beginMove(doc); // a floating picture is dragged into place (M77)
            update();
            event->accept();
            return;
        }
    }
    if (event->button() == Qt::RightButton) {
        // A context menu never moves the insertion point.
        // While a header is open the body offers no spelling (an invalid point).
        emit contextMenuRequested(event->position().x(), event->position().y(),
                                  isEditingStory() ? QPointF(-1, -1) : viewToDoc(event->position()));
        event->accept();
        return;
    }
    m_pressPoint = event->position();
    m_pressForwarded = event->button() == Qt::LeftButton;
    forwardMouse(event);
}

void PageTextItem::mouseMoveEvent(QMouseEvent *event)
{
    if (m_preview != nullptr) {
        event->accept();
        return;
    }
    if (m_pictures != nullptr && m_pictures->isDragging()) {
        m_pictures->dragTo(viewToDoc(event->position()), event->modifiers() & Qt::ShiftModifier);
        update();
        event->accept();
        return;
    }
    if (m_story != nullptr && m_story->isOpen()) {
        forwardStoryMouse(event);
        return;
    }
    forwardMouse(event);
}

void PageTextItem::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_preview != nullptr) {
        event->accept();
        return;
    }
    if (m_pictures != nullptr && m_pictures->isDragging()) {
        m_pictures->finishDrag();
        update();
        event->accept();
        return;
    }
    if (m_story != nullptr && m_story->isOpen()) {
        forwardStoryMouse(event);
        return;
    }
    forwardMouse(event);
    const bool released = m_pressForwarded && event->button() == Qt::LeftButton;
    const bool click = released && (event->position() - m_pressPoint).manhattanLength() < 4;
    m_pressForwarded = false;
    if (click && m_control != nullptr && !m_control->textCursor().hasSelection()) {
        emit clicked(event->position().x(), event->position().y(), viewToDoc(event->position()),
                     m_control->textCursor().position());
    }
    if (released) {
        emit selectionReleased();
    }
}

void PageTextItem::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (m_preview != nullptr) {
        event->accept();
        return;
    }
    if (m_story != nullptr && m_story->isOpen()) {
        if (forwardStoryMouse(event)) {
            return;
        }
        // A double-click on the body returns to it, the caret where it landed.
        m_story->close();
        for (const QEvent::Type type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
            QMouseEvent click(type, viewToDoc(event->position()), event->globalPosition(), Qt::LeftButton,
                              type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton, event->modifiers());
            m_control->processEvent(&click);
        }
        event->accept();
        return;
    }
    if (m_pictures != nullptr && event->button() == Qt::LeftButton) {
        if (const int picture = m_pictures->pictureAt(viewToDoc(event->position())); picture >= 0) {
            m_pictures->select(picture);
            emit pictureDialogRequested();
            event->accept();
            return;
        }
    }
    if (m_story != nullptr && event->button() == Qt::LeftButton && openStoryAt(event)) {
        return;
    }
    forwardMouse(event);
}

void PageTextItem::hoverMoveEvent(QHoverEvent *event)
{
    // Resize cursors over a selected picture's handles.
    static const Qt::CursorShape kShapes[8] = {Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor,
                                               Qt::SizeHorCursor,   Qt::SizeFDiagCursor, Qt::SizeVerCursor,
                                               Qt::SizeBDiagCursor, Qt::SizeHorCursor};
    const QPointF doc = viewToDoc(event->position());
    const int handle = m_pictures != nullptr && !isEditingStory() ? m_pictures->handleAt(doc) : -1;
    const bool place = handle < 0 && m_pictures != nullptr && m_pictures->selectedFloats()
        && m_pictures->pictureRect(m_pictures->selected()).contains(doc);
    setCursor(QCursor(handle >= 0 ? kShapes[handle] : place ? Qt::SizeAllCursor : Qt::IBeamCursor));
    QQuickPaintedItem::hoverMoveEvent(event);
}

void PageTextItem::keyPressEvent(QKeyEvent *event)
{
    if (m_control == nullptr || m_preview != nullptr) {
        event->ignore();
        return;
    }
    if (event->key() == Qt::Key_Escape && m_story != nullptr && m_story->isOpen()) {
        m_story->close();
        event->accept();
        return;
    }
    // AGENT-NOTE: Enter at the end of a heading starts BODY text — the
    // style's "style for following paragraph" — in the control's key filter
    // (StylesController::handleKey, M73), so a Word document's own Normal
    // follows its headings, not Write's.
    event->setAccepted(m_control->processEvent(event));
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
