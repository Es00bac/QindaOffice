// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_page_view.h"

#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QTabletEvent>
#include <QTextEdit>
#include <QVBoxLayout>

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

void NotePageView::setPenWidth(qreal width)
{
    m_penWidth = qBound(1.0, width, 24.0);
}

void NotePageView::setPenColor(const QColor &color)
{
    if (color.isValid()) {
        m_penColor = color;
    }
}

void NotePageView::setZoom(qreal zoom)
{
    const qreal next = qBound(0.5, zoom, 3.0);
    if (qFuzzyCompare(next, m_zoom)) {
        return;
    }
    m_zoom = next;
    updatePageMetrics();
    syncBlocksGeometry();
    update();
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
    m_undoStack.clear();
    m_redoStack.clear();
    notifyHistoryAvailability();
    if (m_page != nullptr) {
        rebuildGrid();
    }
    rebuildBlockWidgets();
    // A page that has already grown past one sheet must make the widget
    // that tall again, or the scroll area cannot reach the bottom of it.
    updatePageMetrics();
    update();
}

void NotePageView::pushUndoSnapshot()
{
    m_undoStack.append(m_page->envelope.ink);
    if (m_undoStack.size() > kMaxHistory) {
        m_undoStack.removeFirst();
    }
    // A new edit ends the old redo branch (standard editor semantics).
    m_redoStack.clear();
    notifyHistoryAvailability();
}

void NotePageView::notifyHistoryAvailability()
{
    emit undoAvailable(m_page != nullptr && !m_undoStack.isEmpty());
    emit redoAvailable(m_page != nullptr && !m_redoStack.isEmpty());
}

void NotePageView::undo()
{
    if (m_page == nullptr || m_undoStack.isEmpty()) {
        return;
    }
    // The state being left behind is what redo must come back to.
    m_redoStack.append(m_page->envelope.ink);
    if (m_redoStack.size() > kMaxHistory) {
        m_redoStack.removeFirst();
    }
    m_page->envelope.ink = m_undoStack.takeLast();
    notifyHistoryAvailability();
    rebuildGrid();
    update();
    emit pageEdited();
}

void NotePageView::redo()
{
    if (m_page == nullptr || m_redoStack.isEmpty()) {
        return;
    }
    m_undoStack.append(m_page->envelope.ink);
    if (m_undoStack.size() > kMaxHistory) {
        m_undoStack.removeFirst();
    }
    m_page->envelope.ink = m_redoStack.takeLast();
    notifyHistoryAvailability();
    rebuildGrid();
    update();
    emit pageEdited();
}

void NotePageView::rebuildGrid()
{
    m_grid.rebuild(m_page != nullptr ? m_page->envelope.ink.strokes() : QVector<Stroke>{});
}




void NotePageView::commitStroke()
{
    Stroke stroke = m_builder.end();
    m_inking = false;
    if (stroke.samples.isEmpty()) {
        update();
        return;
    }
    if (stroke.samples.size() == 1) {
        // A pen tap (dot) makes no movement; synthesize a tiny second sample
        // so the stroke survives the renderer's and persistence's two-sample
        // minimum instead of silently vanishing.
        const StrokeSample only = stroke.samples.first();
        const qreal jitter = qMax(0.5, stroke.width / 6.0);
        stroke.samples.append({only.pos + QPointF(jitter, jitter), only.pressure, only.tiltX,
                               only.tiltY});
    }
    pushUndoSnapshot();
    if (m_tool == NoteTool::Eraser) {
        // Eraser strokes are not stored; they remove what they touched.
        for (const StrokeSample &sample : stroke.samples) {
            while (m_page->envelope.ink.removeAt(sample.pos, 6.0)) {
            }
        }
    } else {
        m_page->envelope.ink.appendStroke(stroke);
    }
    rebuildGrid();
    updateGeometry();
    update();
    emit pageEdited();
}

bool NotePageView::event(QEvent *event)
{
    if (m_page == nullptr) {
        return QWidget::event(event);
    }
    if (event->type() == QEvent::TabletPress || event->type() == QEvent::TabletMove
        || event->type() == QEvent::TabletRelease) {
        auto *tablet = static_cast<QTabletEvent *>(event);
        // Palm rejection baseline: only pen input inks; touch/cursor events
        // (a resting palm registers as touch) never reach the stroke layer.
        // The stylus eraser end is a pen-class device: flip to the eraser
        // tool instead of consuming it as palm.
        if (tablet->pointerType() == QPointingDevice::PointerType::Eraser) {
            if (event->type() == QEvent::TabletPress) {
                m_toolBeforeEraser = m_tool;
                m_tool = NoteTool::Eraser;
            }
        } else if (tablet->pointerType() != QPointingDevice::PointerType::Pen) {
            return true; // consumed and ignored
        }
        const QPointF pos = toPagePos(tablet->position());
        const StrokeSample sample{pos, tablet->pressure(), tablet->xTilt(), tablet->yTilt()};
        if (event->type() == QEvent::TabletPress) {
            // AGENT-GUARD: the Text tool never inks — symmetric with the
            // mouse fallback below (the tablet branch missed this and drew
            // pen strokes while Text was selected). Strokes must also start
            // inside the page; ink in the margin is invisible but persisted.
            if (m_tool == NoteTool::Text || !pageRect().contains(tablet->position())) {
                if (tablet->pointerType() == QPointingDevice::PointerType::Eraser) {
                    m_tool = m_toolBeforeEraser;
                }
                return true;
            }
            const bool highlight = m_tool == NoteTool::Highlighter;
            m_builder.begin(sample,
                            m_tool == NoteTool::Highlighter ? StrokeTool::Highlighter
                                                            : StrokeTool::Pen,
                            highlight ? QColor(255, 210, 0) : m_penColor,
                            highlight ? 6.0 : m_penWidth);
            m_inking = true;
        } else if (event->type() == QEvent::TabletMove && m_inking) {
            // AGENT-GUARD: the in-progress stroke is rendered from
            // m_builder.current() in paintEvent and must NEVER touch the
            // committed layer during moves — an updateLast() here replaced
            // the previous real stroke, destroying it on the first move of
            // the next stroke.
            m_builder.add(sample);
            update();
        } else if (event->type() == QEvent::TabletRelease && m_inking) {
            commitStroke();
        }
        if (tablet->pointerType() == QPointingDevice::PointerType::Eraser
            && event->type() == QEvent::TabletRelease) {
            m_tool = m_toolBeforeEraser;
        }
        return true;
    }
    // Mouse fallback (no tablet attached): left-drag inks with fixed pressure.
    if (event->type() == QEvent::MouseButtonPress
        && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton
        && pageRect().contains(static_cast<QMouseEvent *>(event)->position())
        && m_tool != NoteTool::Text) {
        const bool highlight = m_tool == NoteTool::Highlighter;
        m_builder.begin({toPagePos(static_cast<QMouseEvent *>(event)->position()), 0.6},
                        highlight ? StrokeTool::Highlighter : StrokeTool::Pen,
                        highlight ? QColor(255, 210, 0) : m_penColor,
                        highlight ? 6.0 : m_penWidth);
        m_inking = true;
        return true;
    }
    if (event->type() == QEvent::MouseMove && m_inking) {
        // Same guard as the tablet path: preview stays out of the layer.
        m_builder.add({toPagePos(static_cast<QMouseEvent *>(event)->position()), 0.6});
        update();
        return true;
    }
    if (event->type() == QEvent::MouseButtonRelease && m_inking) {
        commitStroke();
        return true;
    }
    return QWidget::event(event);
}

void NotePageView::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().window().color());
    const QRectF page = pageRect();
    painter.fillRect(page, palette().base().color());
    painter.setPen(palette().dark().color());
    painter.drawRect(page.adjusted(0, 0, -1, -1));

    // Backgrounds (plan 6.4).
    const QString background = m_page != nullptr ? m_page->envelope.background
                                                 : QStringLiteral("blank");
    painter.setPen(QPen(palette().mid().color(), 1));
    if (background == QLatin1String("ruled")) {
        for (qreal y = page.top() + 32; y < page.bottom(); y += 28) {
            painter.drawLine(QPointF(page.left() + 24, y), QPointF(page.right() - 24, y));
        }
    } else if (background == QLatin1String("grid")) {
        // A real grid: horizontal AND vertical lines through each node
        // (the earlier release drew only horizontals + points).
        for (qreal y = page.top() + 24; y < page.bottom(); y += 28) {
            painter.drawLine(QPointF(page.left() + 24, y), QPointF(page.right() - 24, y));
        }
        for (qreal x = page.left() + 24; x < page.right(); x += 28) {
            painter.drawLine(QPointF(x, page.top() + 24), QPointF(x, page.bottom() - 24));
        }
    } else if (background == QLatin1String("dotted")) {
        for (qreal y = page.top() + 24; y < page.bottom(); y += 22) {
            for (qreal x = page.left() + 24; x < page.right(); x += 22) {
                painter.drawPoint(QPointF(x, y));
            }
        }
    }

    if (m_page != nullptr) {
        painter.setClipRect(page);
        // The spatial grid indexes page coordinates; query in page space
        // and render strokes through the zoom transform. AGENT-GUARD: the
        // widget→page transform subtracts the page origin BEFORE dividing
        // by zoom (the earlier release skipped the subtraction, offsetting
        // the query rect and leaving strokes unpainted at zoom ≠ 100 %).
        const QRectF widgetClip = event->rect();
        const QRectF pageClip = QRectF((widgetClip.topLeft() - page.topLeft()) / m_zoom,
                                       widgetClip.size() / m_zoom);
        const QVector<int> visible = m_grid.query(pageClip.toAlignedRect()).values();
        painter.save();
        painter.translate(page.topLeft());
        painter.scale(m_zoom, m_zoom);
        for (const int index : visible) {
            renderStroke(&painter, m_page->envelope.ink.strokes().at(index));
        }
        if (m_inking) {
            renderStroke(&painter, m_builder.current());
        }
        painter.restore();
        painter.setClipping(false);
        // Images sit UNDER the ink: annotating a photograph is the point.
        paintImageBlocks(&painter, page);
    }
}

void NotePageView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    syncBlocksGeometry();
}


} // namespace QindaOffice::Apps::Notes
