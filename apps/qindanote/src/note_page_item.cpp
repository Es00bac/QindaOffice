// SPDX-License-Identifier: GPL-3.0-or-later
// NotePageItem's page metrics, tool state, painting and history glue.
// Stylus gestures live in note_page_item_ink.cpp, blocks and the editor
// contract in note_page_item_blocks.cpp, the commands in
// note_page_history.cpp.

#include "note_page_item.h"

#include "ink_persistence.h"
#include "note_page_history.h"
#include "note_page_render.h"

#include <QPainter>
#include <QUndoStack>

#include <cmath>

namespace QindaOffice::Apps::Notes {

using namespace QindaOffice::Ink;

NotePageItem::NotePageItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setAcceptTouchEvents(false); // pens only: palm baseline
    // Fill spec: the item paints its whole rect (paper), so the scene
    // graph can skip blending the canvas behind it.
    setOpaquePainting(true);
    setFillColor(Qt::white);
    m_history = new QUndoStack(this);
    m_history->setUndoLimit(100);
    connect(m_history, &QUndoStack::canUndoChanged, this, [this] { notifyHistory(); });
    connect(m_history, &QUndoStack::canRedoChanged, this, [this] { notifyHistory(); });
    m_builder.positionSmoothing = m_smoothing;
    updateCursor();
    updatePageMetrics();
}

NotePageItem::~NotePageItem() = default;

void NotePageItem::notifyHistory()
{
    emit historyChanged();
}

bool NotePageItem::canUndo() const
{
    return m_page != nullptr && m_history->canUndo();
}

bool NotePageItem::canRedo() const
{
    return m_page != nullptr && m_history->canRedo();
}

int NotePageItem::historyIndex() const { return m_history->index(); }

void NotePageItem::applyHistoryContent(const QByteArray &blocksJson, const QString &title)
{
    if (!m_page) return;
    const bool renamed = m_page->title != title;
    m_page->title = title;
    m_page->envelope.title = title;
    applyHistoryBlocks(blocksJson);
    if (renamed) emit pageTitleChanged();
}

bool NotePageItem::replaceContent(const QJsonArray &blocks, const QString &title,
                                  const QString &undoText)
{
    if (!m_page || m_editingBlock >= 0 || m_inking || m_erasing) return false;
    const QByteArray before = m_page->envelope.blocksJson;
    const QByteArray after = withBlocksArray(before, blocks);
    const QString oldTitle = m_page->title;
    if (before == after && oldTitle == title) return false;
    applyHistoryContent(after, title);
    m_history->push(new PageContentCommand(undoText, before, after, oldTitle, title, this));
    return true;
}

void NotePageItem::updatePageMetrics()
{
    // The item is exactly as big as the (grown) page in document units;
    // the Tk.Viewport around it zooms and scrolls.
    setImplicitSize(m_pageWidth, qMax(m_minPageHeight, growHeight()));
}

QRectF NotePageItem::pageRect() const
{
    return {0, 0, m_pageWidth, qMax(m_minPageHeight, growHeight())};
}

qreal NotePageItem::growHeight() const
{
    if (m_page == nullptr) {
        return m_minPageHeight;
    }
    qreal contentBottom = m_page->envelope.ink.bounds().bottom() + 80;
    for (const PageBlock &block : m_blocks) {
        contentBottom = qMax(contentBottom, block.rect.bottom() + 40);
    }
    const int pages = qBound(1, int(std::ceil(contentBottom / m_minPageHeight)), int(kMaxPages));
    return pages * m_minPageHeight;
}

// ---- tools ----------------------------------------------------------------

void NotePageItem::setTool(NoteTool tool)
{
    if (m_tool == tool) {
        return;
    }
    if (m_editingBlock >= 0 && tool != NoteTool::Text) {
        commitEdit();
    }
    m_tool = tool;
    updateCursor();
    emit presentationChanged();
}

QString NotePageItem::toolName() const
{
    switch (m_tool) {
    case NoteTool::Pen:
        return QStringLiteral("pen");
    case NoteTool::Highlighter:
        return QStringLiteral("highlighter");
    case NoteTool::Eraser:
        return QStringLiteral("eraser");
    case NoteTool::Text:
        return QStringLiteral("text");
    }
    return QStringLiteral("pen");
}

void NotePageItem::setToolName(const QString &name)
{
    if (name == QLatin1String("highlighter")) {
        setTool(NoteTool::Highlighter);
    } else if (name == QLatin1String("eraser")) {
        setTool(NoteTool::Eraser);
    } else if (name == QLatin1String("text")) {
        setTool(NoteTool::Text);
    } else {
        setTool(NoteTool::Pen);
    }
}

void NotePageItem::setPenWidth(qreal width)
{
    m_penWidth = qBound(1.0, width, 24.0);
    emit presentationChanged();
}

void NotePageItem::setPenColor(const QColor &color)
{
    if (color.isValid()) {
        m_penColor = color;
        emit presentationChanged();
    }
}

void NotePageItem::setHighlighterColor(const QColor &color)
{
    if (color.isValid()) {
        m_highlighterColor = color;
        emit presentationChanged();
    }
}

void NotePageItem::setHighlighterWidth(qreal width)
{
    m_highlighterWidth = qBound(2.0, width, 32.0);
    emit presentationChanged();
}

void NotePageItem::setEraserMode(Ink::EraseMode mode)
{
    m_eraserMode = mode;
    emit presentationChanged();
}

QString NotePageItem::eraserModeName() const
{
    return m_eraserMode == Ink::EraseMode::Partial ? QStringLiteral("partial")
                                                   : QStringLiteral("stroke");
}

void NotePageItem::setEraserModeName(const QString &name)
{
    setEraserMode(name == QLatin1String("partial") ? Ink::EraseMode::Partial
                                                   : Ink::EraseMode::Stroke);
}

void NotePageItem::setEraserRadius(qreal radius)
{
    m_eraserRadius = qBound(2.0, radius, 48.0);
    if (m_tool == NoteTool::Eraser) {
        updateCursor();
    }
    emit presentationChanged();
}

void NotePageItem::setPressureEnabled(bool on)
{
    m_pressureEnabled = on;
    emit presentationChanged();
}

void NotePageItem::setStrokeSmoothing(qreal factor)
{
    m_smoothing = qBound(0.0, factor, 0.9);
    m_builder.positionSmoothing = m_smoothing;
    emit presentationChanged();
}

void NotePageItem::setViewZoom(qreal zoom)
{
    if (qFuzzyCompare(m_viewZoom, zoom)) {
        return;
    }
    m_viewZoom = qMax(0.05, zoom);
    if (m_tool == NoteTool::Eraser) {
        updateCursor(); // the ring follows the zoom
    }
    emit presentationChanged();
}

// ---- page -----------------------------------------------------------------

QString NotePageItem::background() const
{
    return m_page != nullptr ? m_page->envelope.background : QStringLiteral("blank");
}

void NotePageItem::setBackground(const QString &background)
{
    if (m_page == nullptr || m_page->envelope.background == background) {
        return;
    }
    const QString before = m_page->envelope.background;
    m_page->envelope.background = background;
    m_history->push(new BackgroundCommand(before, background, this));
    update();
    emit pageChanged();
    emit pageEdited();
}

void NotePageItem::applyHistoryBackground(const QString &background)
{
    if (m_page == nullptr) {
        return;
    }
    m_page->envelope.background = background;
    update();
    emit pageChanged();
    emit pageEdited();
}

void NotePageItem::setPage(NotePage *page)
{
    if (m_editingBlock >= 0) {
        // An open editor belongs to the previous page: write it back there.
        commitEdit();
    }
    m_page = page;
    m_inking = false;
    m_erasing = false;
    m_eraseChanged = false;
    m_temporaryTool = false;
    // The history is per page: undoing page A's ink onto page B's surface
    // is exactly the replay bug the one-history guard exists to prevent.
    m_history->clear();
    rebuildGrid();
    relayoutBlocks();
    updatePageMetrics();
    update();
    emit pageChanged();
    notifyHistory();
}

void NotePageItem::applyHistoryLayer(const Ink::StrokeLayer &layer)
{
    if (m_page == nullptr) {
        return;
    }
    m_page->envelope.ink = layer;
    rebuildGrid();
    updatePageMetrics();
    update();
    emit pageEdited();
}

void NotePageItem::undo()
{
    if (m_editingBlock >= 0) {
        commitEdit();
    }
    m_history->undo();
}

void NotePageItem::redo()
{
    m_history->redo();
}

void NotePageItem::rebuildGrid()
{
    m_grid.rebuild(m_page != nullptr ? m_page->envelope.ink.strokes() : QVector<Stroke>{});
}

// ---- paint ----------------------------------------------------------------

void NotePageItem::paint(QPainter *painter)
{
    // AGENT-NOTE (D-004): the page is PAPER, not chrome — white with dark
    // ink whatever the theme does; the Viewport paints the canvas role
    // around it. Same pin as the print renderer.
    const QRectF page = pageRect();
    painter->fillRect(page, Qt::white);
    painter->setRenderHint(QPainter::Antialiasing, true);
    if (m_page == nullptr) {
        return;
    }
    // Render order (AGENT-GUARD: identical to renderNotePage, so the
    // screen and the printer cannot drift): background → image blocks →
    // text/checklist blocks (minus the one under the editor) → highlighter
    // strokes → pen strokes → the in-progress stroke.
    painter->save();
    painter->setClipRect(page);
    paintNoteBackground(painter, m_page->envelope.background, page.size());
    paintNoteBlocks(painter, m_blocks, PageBlock::Kind::Image, m_editingBlock);
    paintNoteBlocks(painter, m_blocks, PageBlock::Kind::Text, m_editingBlock);
    paintNoteBlocks(painter, m_blocks, PageBlock::Kind::Markdown, m_editingBlock);
    paintNoteBlocks(painter, m_blocks, PageBlock::Kind::Check, m_editingBlock);
    // Only the strokes touching the dirty region: the spatial grid indexes
    // page coordinates, which are the item's coordinates.
    const QRectF dirty = painter->hasClipping() ? painter->clipBoundingRect() : page;
    const QSet<int> visible = m_grid.query(dirty.toAlignedRect());
    renderStrokes(painter, m_page->envelope.ink.strokes(), &visible);
    if (m_inking) {
        renderStroke(painter, m_builder.current());
    }
    painter->restore();
}

} // namespace QindaOffice::Apps::Notes
