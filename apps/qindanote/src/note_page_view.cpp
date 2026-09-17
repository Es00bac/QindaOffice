// SPDX-License-Identifier: GPL-3.0-or-later
// NotePageView's page metrics, tool state, painting and history glue.
// Input handling lives in note_page_input.cpp, content blocks in
// note_page_blocks.cpp, the QUndoStack commands in note_page_history.cpp.

#include "note_page_view.h"

#include "ink_persistence.h"
#include "note_page_history.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPaintEvent>
#include <QPainter>
#include <QUndoStack>

namespace QindaOffice::Apps::Notes {

using namespace QindaOffice::Ink;

NotePageView::NotePageView(QWidget *parent)
    : QWidget(parent)
{
    // AGENT-GUARD: the page is sized through updatePageMetrics(), which the
    // window's QScrollArea scrolls. Setting the page size as the widget's
    // hard minimum (which is what used to happen) made the whole QindaNote
    // window refuse to be shorter than a full A4 page plus chrome — about
    // 1100 px — so it could not fit on a laptop screen at all.
    setAttribute(Qt::WA_AcceptTouchEvents, false); // pens only: palm baseline
    // StrongFocus so Esc (and any future page keys) reach the view after a
    // pen/mouse press; tool letters are window-scoped QAction shortcuts.
    setFocusPolicy(Qt::StrongFocus);
    // The ONE page history (note_page_history.h). M22 wires ink only;
    // blocks and background join it in M23.
    m_history = new QUndoStack(this);
    m_history->setUndoLimit(100);
    connect(m_history, &QUndoStack::canUndoChanged, this,
            [this](bool can) { emit undoAvailable(can && m_page != nullptr); });
    connect(m_history, &QUndoStack::canRedoChanged, this,
            [this](bool can) { emit redoAvailable(can && m_page != nullptr); });
    m_builder.positionSmoothing = m_smoothing;
    updateCursor();
    updatePageMetrics();
}

void NotePageView::updatePageMetrics()
{
    // The widget is exactly as big as the (zoomed, grown) page plus its
    // margin; the scroll area around it supplies the scrollbars.
    const qreal height = qMax(m_minPageHeight * m_zoom, growHeight());
    const QSize wanted(int(m_pageWidth * m_zoom) + 48, int(height) + 48);
    setMinimumSize(wanted);
    resize(wanted);
    updateGeometry();
}

QRectF NotePageView::pageRect() const
{
    const qreal height = qMax(m_minPageHeight * m_zoom, growHeight());
    const qreal pageW = m_pageWidth * m_zoom;
    return {(width() - pageW) / 2.0, 24.0, pageW, height};
}

void NotePageView::setTool(NoteTool tool)
{
    if (m_tool == tool) {
        return;
    }
    m_tool = tool;
    updateCursor();
    emit presentationChanged();
}

void NotePageView::setPenWidth(qreal width)
{
    m_penWidth = qBound(1.0, width, 24.0);
    emit presentationChanged();
}

void NotePageView::setPenColor(const QColor &color)
{
    if (color.isValid()) {
        m_penColor = color;
        emit presentationChanged();
    }
}

void NotePageView::setHighlighterColor(const QColor &color)
{
    if (color.isValid()) {
        m_highlighterColor = color;
        emit presentationChanged();
    }
}

void NotePageView::setHighlighterWidth(qreal width)
{
    m_highlighterWidth = qBound(2.0, width, 32.0);
    emit presentationChanged();
}

void NotePageView::setEraserMode(Ink::EraseMode mode)
{
    m_eraserMode = mode;
    emit presentationChanged();
}

void NotePageView::setEraserRadius(qreal radius)
{
    m_eraserRadius = qBound(2.0, radius, 48.0);
    if (m_tool == NoteTool::Eraser) {
        updateCursor(); // the ring is drawn at radius x zoom
    }
    emit presentationChanged();
}

void NotePageView::setStrokeSmoothing(qreal factor)
{
    m_smoothing = qBound(0.0, factor, 0.9);
    m_builder.positionSmoothing = m_smoothing;
}

void NotePageView::setZoom(qreal zoom)
{
    const qreal next = qBound(0.5, zoom, 3.0);
    if (qFuzzyCompare(next, m_zoom)) {
        return;
    }
    m_zoom = next;
    if (m_tool == NoteTool::Eraser) {
        updateCursor(); // the ring follows the zoom
    }
    updatePageMetrics();
    syncBlocksGeometry();
    update();
    emit presentationChanged();
}

void NotePageView::setBackground(const QString &background)
{
    if (m_page == nullptr || m_page->envelope.background == background) {
        return;
    }
    m_page->envelope.background = background;
    update();
    emit pageEdited();
}

// Widget (event) coordinates -> page coordinates (strokes are stored in
// page space so zoom never rewrites the document).
QPointF NotePageView::toPagePos(const QPointF &widgetPos) const
{
    return (widgetPos - pageRect().topLeft()) / m_zoom;
}

QRect NotePageView::toWidgetRect(const QRectF &pageSpace) const
{
    const QRectF page = pageRect();
    return QRectF(page.topLeft() + pageSpace.topLeft() * m_zoom, pageSpace.size() * m_zoom)
        .toAlignedRect()
        .adjusted(-1, -1, 1, 1);
}

qreal NotePageView::growHeight() const
{
    if (m_page == nullptr) {
        return m_minPageHeight;
    }
    qreal contentBottom = m_page->envelope.ink.bounds().bottom() + 80;
    // The block layout is already in PAGE space (note_blocks.h), which is
    // the space this comparison is in. AGENT-GUARD: do NOT call pageRect()
    // here — pageRect() calls growHeight().
    for (const PageBlock &block : m_blocks) {
        contentBottom = qMax(contentBottom, block.rect.bottom() + 40);
    }
    const int pages = qBound(1, int(std::ceil(contentBottom / m_minPageHeight)), int(kMaxPages));
    // The widget height of the page: page-space height times zoom.
    return pages * m_minPageHeight * m_zoom;
}

void NotePageView::setPage(NotePage *page)
{
    m_page = page;
    // Any half-finished gesture belongs to the previous page.
    m_inking = false;
    m_erasing = false;
    m_eraseChanged = false;
    m_temporaryTool = false;
    // The history is per page: undoing page A's ink onto page B's surface
    // is exactly the replay bug the one-history guard exists to prevent.
    m_history->clear();
    if (m_page != nullptr) {
        rebuildGrid();
    }
    rebuildBlockWidgets();
    // A page that has already grown past one sheet must make the widget
    // that tall again, or the scroll area cannot reach the bottom of it.
    updatePageMetrics();
    update();
}

void NotePageView::applyHistoryLayer(const Ink::StrokeLayer &layer)
{
    if (m_page == nullptr) {
        return;
    }
    m_page->envelope.ink = layer;
    rebuildGrid();
    updateGeometry();
    update();
    emit pageEdited();
}

void NotePageView::undo()
{
    m_history->undo();
}

void NotePageView::redo()
{
    m_history->redo();
}

void NotePageView::rebuildGrid()
{
    m_grid.rebuild(m_page != nullptr ? m_page->envelope.ink.strokes() : QVector<Stroke>{});
}

void NotePageView::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().window().color());
    const QRectF page = pageRect();
    // AGENT-NOTE (D-004): the page is PAPER, not chrome. It is pinned
    // white with dark ink whatever the platform theme is doing — the same
    // pin the print renderer (note_page_render.cpp) and QindaWrite's
    // PageTextView apply — because a dark page would hide the default
    // black pen and contradict what Print produces. The theme owns the
    // canvas around the page and the border (chrome around paper).
    painter.fillRect(page, Qt::white);
    painter.setPen(palette().dark().color());
    painter.drawRect(page.adjusted(0, 0, -1, -1));

    // Guide lines in page space (they zoom with the page) in the same grey
    // the print renderer uses (0xc8c8d2; document surface, not chrome), so
    // screen and paper cannot drift apart.
    const QString background = m_page != nullptr ? m_page->envelope.background
                                                 : QStringLiteral("blank");
    painter.save();
    painter.setClipRect(page);
    painter.translate(page.topLeft());
    painter.scale(m_zoom, m_zoom);
    painter.setPen(QPen(QColor(0xc8, 0xc8, 0xd2), 0)); // cosmetic: 1 px at any zoom
    const qreal pageW = m_pageWidth;
    const qreal pageH = page.height() / m_zoom;
    if (background == QLatin1String("ruled")) {
        for (qreal y = 32; y < pageH; y += 28) {
            painter.drawLine(QPointF(24, y), QPointF(pageW - 24, y));
        }
    } else if (background == QLatin1String("grid")) {
        for (qreal y = 24; y < pageH; y += 28) {
            painter.drawLine(QPointF(24, y), QPointF(pageW - 24, y));
        }
        for (qreal x = 24; x < pageW; x += 28) {
            painter.drawLine(QPointF(x, 24), QPointF(x, pageH - 24));
        }
    } else if (background == QLatin1String("dotted")) {
        for (qreal y = 24; y < pageH; y += 22) {
            for (qreal x = 24; x < pageW; x += 22) {
                painter.drawPoint(QPointF(x, y));
            }
        }
    }
    painter.restore();

    if (m_page != nullptr) {
        // Render order (AGENT-GUARD: identical to renderNotePage, so the
        // screen and the printer cannot drift): background → image blocks →
        // highlighter strokes → pen strokes. Text/checklist blocks are
        // still child widgets until M23 paints them, so they stay above
        // the ink on screen only (defect T4, fixed there).
        painter.save();
        painter.setClipRect(page);
        paintImageBlocks(&painter, page);
        // The spatial grid indexes page coordinates; query in page space
        // and render strokes through the zoom transform. AGENT-GUARD: the
        // widget→page transform subtracts the page origin BEFORE dividing
        // by zoom (the earlier release skipped the subtraction, offsetting
        // the query rect and leaving strokes unpainted at zoom ≠ 100 %).
        const QRectF widgetClip = event->rect();
        const QRectF pageClip = QRectF((widgetClip.topLeft() - page.topLeft()) / m_zoom,
                                       widgetClip.size() / m_zoom);
        const QSet<int> visible = m_grid.query(pageClip.toAlignedRect());
        painter.translate(page.topLeft());
        painter.scale(m_zoom, m_zoom);
        renderStrokes(&painter, m_page->envelope.ink.strokes(), &visible);
        if (m_inking) {
            renderStroke(&painter, m_builder.current());
        }
        painter.restore();
    }
}

void NotePageView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    syncBlocksGeometry();
}

} // namespace QindaOffice::Apps::Notes
