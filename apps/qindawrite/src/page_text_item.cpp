// SPDX-License-Identifier: GPL-3.0-or-later
#include "page_text_item.h"

#include "docx_comments.h"
#include "docx_text.h"

#include "page_picture_tool.h"
#include "page_story_editor.h"
#include "page_text_control.h"
#include "pdf_exporter.h"
#include "style_catalog.h"
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

namespace {
// Paper edge: paper is paper (D-004), so this is a print-like grey, not a
// theme role.
const QColor kPaperEdge(0xc8, 0xc8, 0xcc);
// The body behind an open header or footer, washed out as Word does.
const QColor kBodyWash(0xff, 0xff, 0xff, 0x8c);
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
    connect(document, &WriteDocument::pageSetupChanged, this, [this] {
        m_geometry.setPageSize(m_document->pageSize());
        updatePageMetrics();
    });
    connect(document, &WriteDocument::contentChanged, this, [this] { update(); });
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
    const bool storyOpen = m_story != nullptr && m_story->isOpen();
    if (storyOpen) {
        // The control edits a header or footer: the body as it prints, no
        // caret or selection.
        QAbstractTextDocumentLayout::PaintContext context;
        context.clip = pageArea;
        context.palette = m_control->paperPalette();
        m_document->textDocument()->documentLayout()->draw(painter, context);
    } else {
        // Comment bands under the text (display only, docx_comments.h).
        if (!m_document->comments().isEmpty()) {
            Docx::drawCommentRanges(painter, *m_document->textDocument(), m_document->commentSpans(), pageArea,
                                    m_document->activeComment());
        }
        m_control->drawContents(painter, pageArea);
    }
    // Word paragraph borders: Qt's layout draws none (docx_text.h).
    Docx::drawParagraphBorders(painter, *m_document->textDocument(), pageArea);
    if (!storyOpen) {
        m_pictures->draw(painter, pageArea); // a selected picture's handles
    }
    painter->restore();
    if (storyOpen) {
        painter->fillRect(page.intersected(clip), kBodyWash);
    }

    const PageDecoration decoration = m_document->decoration();
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
    const StoryEditor::Kind zone = index >= 0 ? m_story->zoneAt(pagePos) : StoryEditor::Kind::None;
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
    forwardMouse(event);
}

void PageTextItem::mouseMoveEvent(QMouseEvent *event)
{
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
}

void PageTextItem::mouseDoubleClickEvent(QMouseEvent *event)
{
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
    const int handle = m_pictures != nullptr && !isEditingStory() ? m_pictures->handleAt(viewToDoc(event->position())) : -1;
    setCursor(QCursor(handle >= 0 ? kShapes[handle] : Qt::IBeamCursor));
    QQuickPaintedItem::hoverMoveEvent(event);
}

void PageTextItem::keyPressEvent(QKeyEvent *event)
{
    if (m_control == nullptr) {
        event->ignore();
        return;
    }
    if (event->key() == Qt::Key_Escape && m_story != nullptr && m_story->isOpen()) {
        m_story->close();
        event->accept();
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
