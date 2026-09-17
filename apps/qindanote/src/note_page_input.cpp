// SPDX-License-Identifier: GPL-3.0-or-later
// NotePageView's input handling: tablet and mouse inking, the LIVE eraser,
// tool cursors, stylus eraser-end and barrel-button temporary tools,
// middle-drag pan, Ctrl+wheel zoom about the cursor, and the Esc key.
// Split out of note_page_view.cpp to keep both inside the line budget.
//
// Pointer rules (plan section 3.3), in order:
//  1. Touch is ignored (the palm-rejection baseline — WA_AcceptTouchEvents
//     false in the ctor, non-pen pointer types consumed here).
//  2. The stylus eraser end and a held barrel MiddleButton borrow the
//     Eraser for one gesture; both revert on release.
//  3. Middle-drag pans the hosting scroll area; Ctrl+wheel zooms about the
//     cursor; Shift+wheel scrolls horizontally; plain wheel propagates.
//  4. Ink tools ink (over blocks too once M23 paints blocks; text/checklist
//     blocks are widgets until then).
//  5. Mouse inking stays with fixed pressure 0.6 — laptops without a pen
//     must work, and the tests drive that path.

#include "note_page_view.h"

#include "note_page_history.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabletEvent>
#include <QWheelEvent>

#include <cmath>

namespace QindaOffice::Apps::Notes {

using namespace QindaOffice::Ink;

namespace {

// Pen width grows up to 1.5x base at full pressure; the repaint margin
// must cover the widest ink a segment can produce.
qreal repaintMargin(const Stroke &stroke)
{
    return stroke.width * 1.5 + 2.0;
}

} // namespace

void NotePageView::updateCursor()
{
    switch (m_tool) {
    case NoteTool::Pen:
    case NoteTool::Highlighter:
        setCursor(Qt::CrossCursor);
        break;
    case NoteTool::Text:
        setCursor(Qt::IBeamCursor);
        break;
    case NoteTool::Eraser:
        setCursor(eraserCursor());
        break;
    }
}

QCursor NotePageView::eraserCursor() const
{
    // A ring the size of the eraser disc (radius x zoom): the user should
    // see exactly what one swipe will eat. White halo + black core stays
    // readable over dark ink on the white page.
    const int diameter = qBound(8, int(m_eraserRadius * 2.0 * m_zoom), 128);
    QPixmap pixmap(diameter, diameter);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(Qt::white, 3));
    painter.drawEllipse(QRectF(1.5, 1.5, diameter - 3.0, diameter - 3.0));
    painter.setPen(QPen(Qt::black, 1));
    painter.drawEllipse(QRectF(1.5, 1.5, diameter - 3.0, diameter - 3.0));
    painter.end();
    return QCursor(pixmap, diameter / 2, diameter / 2);
}

void NotePageView::beginTemporaryTool(NoteTool tool)
{
    if (m_temporaryTool) {
        return; // eraser end AND barrel button: one restore is enough
    }
    m_temporaryTool = true;
    m_toolBeforeTemporary = m_tool;
    setTool(tool);
}

void NotePageView::restoreTemporaryTool()
{
    if (!m_temporaryTool) {
        return;
    }
    m_temporaryTool = false;
    setTool(m_toolBeforeTemporary);
}

void NotePageView::beginEraseGesture(const QPointF &pagePos)
{
    // AGENT-CONTRACT: ONE undo step per gesture. The snapshot is taken at
    // press; the InkCommand is pushed at release ONLY when the drag
    // actually erased something — an empty swipe must not grow the history
    // (the old commit-time eraser pushed a snapshot per gesture regardless).
    m_erasing = true;
    m_eraseChanged = false;
    m_eraseSnapshot = m_page->envelope.ink;
    eraseAtPagePos(pagePos);
}

void NotePageView::eraseAtPagePos(const QPointF &pagePos)
{
    const QRectF disc(pagePos - QPointF(m_eraserRadius, m_eraserRadius),
                      QSizeF(m_eraserRadius * 2.0, m_eraserRadius * 2.0));
    const QSet<int> candidates = m_grid.query(disc);
    if (candidates.isEmpty()) {
        return;
    }
    // Repaint exactly what this sample could have changed: the eraser disc
    // plus the candidates' bounds (a removed stroke can reach past the disc).
    QRectF touched = disc;
    for (const int index : candidates) {
        touched |= m_page->envelope.ink.strokes().at(index).bounds();
    }
    if (eraseAt(&m_page->envelope.ink, candidates, pagePos, m_eraserRadius, m_eraserMode)) {
        m_eraseChanged = true;
        rebuildGrid(); // indices changed; the next sample queries fresh ones
        update(toWidgetRect(touched));
        emit pageEdited();
    }
}

void NotePageView::endEraseGesture()
{
    m_erasing = false;
    if (m_eraseChanged) {
        m_history->push(new InkCommand(tr("Erase"), m_eraseSnapshot, m_page->envelope.ink, this));
    }
    m_eraseSnapshot = Ink::StrokeLayer{};
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
    const StrokeLayer before = m_page->envelope.ink;
    m_page->envelope.ink.appendStroke(stroke);
    m_history->push(new InkCommand(tr("Stroke"), before, m_page->envelope.ink, this));
    rebuildGrid();
    updateGeometry();
    update();
    emit pageEdited();
}

QScrollArea *NotePageView::scrollArea() const
{
    // The view is the scroll area's widget; walk up (viewport → area)
    // rather than knowing NoteWindow's layout.
    QWidget *parent = parentWidget();
    while (parent != nullptr) {
        if (auto *area = qobject_cast<QScrollArea *>(parent)) {
            return area;
        }
        parent = parent->parentWidget();
    }
    return nullptr;
}

bool NotePageView::event(QEvent *event)
{
    if (m_page == nullptr) {
        return QWidget::event(event);
    }
    if (event->type() == QEvent::TabletPress || event->type() == QEvent::TabletMove
        || event->type() == QEvent::TabletRelease) {
        auto *tablet = static_cast<QTabletEvent *>(event);
        // Palm rejection baseline: only pen-class input inks; touch/cursor
        // events (a resting palm registers as touch) never reach the stroke
        // layer. The stylus eraser end is a pen-class device.
        const bool eraserEnd = tablet->pointerType() == QPointingDevice::PointerType::Eraser;
        if (!eraserEnd && tablet->pointerType() != QPointingDevice::PointerType::Pen) {
            return true; // consumed and ignored
        }
        const QPointF pos = toPagePos(tablet->position());
        // Preferences note/pressure: off pins every sample to a flat 0.5.
        const qreal pressure = m_pressureEnabled ? tablet->pressure() : 0.5;
        if (event->type() == QEvent::TabletPress) {
            setFocus(Qt::OtherFocusReason);
            // The stylus eraser end, or a held barrel MiddleButton, borrows
            // the eraser until the release (plan section 3.3 rule 2).
            if (eraserEnd || tablet->buttons().testFlag(Qt::MiddleButton)) {
                beginTemporaryTool(NoteTool::Eraser);
            }
            // Strokes must start inside the page; ink in the margin is
            // invisible but persisted. The Text tool never inks (its tap is
            // M23's block editing).
            if (m_tool == NoteTool::Text || !pageRect().contains(tablet->position())) {
                return true;
            }
            if (m_tool == NoteTool::Eraser) {
                beginEraseGesture(pos);
                return true;
            }
            const bool highlight = m_tool == NoteTool::Highlighter;
            m_builder.begin({pos, pressure, tablet->xTilt(), tablet->yTilt()},
                            highlight ? StrokeTool::Highlighter : StrokeTool::Pen,
                            highlight ? m_highlighterColor : m_penColor,
                            highlight ? m_highlighterWidth : m_penWidth);
            m_inking = true;
            return true;
        }
        if (event->type() == QEvent::TabletMove) {
            if (m_erasing) {
                eraseAtPagePos(pos);
                return true;
            }
            if (m_inking) {
                // AGENT-GUARD: the in-progress stroke is rendered from
                // m_builder.current() in paintEvent and must NEVER touch the
                // committed layer during moves — an updateLast() here replaced
                // the previous real stroke, destroying it on the first move of
                // the next stroke.
                m_builder.add({pos, pressure, tablet->xTilt(), tablet->yTilt()});
                // Narrow repaint: only the fresh segment, never the page.
                const Stroke &current = m_builder.current();
                const int n = current.samples.size();
                if (n > 0) {
                    QRectF segment(current.samples.constLast().pos, QSizeF(1, 1));
                    if (n >= 2) {
                        segment |= QRectF(current.samples.at(n - 2).pos, QSizeF(1, 1));
                    }
                    const qreal margin = repaintMargin(current);
                    update(toWidgetRect(segment.adjusted(-margin, -margin, margin, margin)));
                }
                return true;
            }
            return true;
        }
        // TabletRelease
        if (m_erasing) {
            endEraseGesture();
        } else if (m_inking) {
            commitStroke();
        }
        restoreTemporaryTool();
        return true;
    }
    // Mouse fallback (no tablet attached): left-drag inks with fixed
    // pressure; middle-drag pans the hosting scroll area.
    if (event->type() == QEvent::MouseButtonPress) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::MiddleButton) {
            m_panning = true;
            m_panLast = mouse->position().toPoint();
            setCursor(Qt::ClosedHandCursor);
            return true;
        }
        if (mouse->button() != Qt::LeftButton || m_tool == NoteTool::Text
            || !pageRect().contains(mouse->position())) {
            return QWidget::event(event);
        }
        const QPointF pos = toPagePos(mouse->position());
        if (m_tool == NoteTool::Eraser) {
            beginEraseGesture(pos);
            return true;
        }
        const bool highlight = m_tool == NoteTool::Highlighter;
        m_builder.begin({pos, 0.6},
                        highlight ? StrokeTool::Highlighter : StrokeTool::Pen,
                        highlight ? m_highlighterColor : m_penColor,
                        highlight ? m_highlighterWidth : m_penWidth);
        m_inking = true;
        return true;
    }
    if (event->type() == QEvent::MouseMove) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (m_panning) {
            if (QScrollArea *area = scrollArea()) {
                const QPoint delta = m_panLast - mouse->position().toPoint();
                area->horizontalScrollBar()->setValue(area->horizontalScrollBar()->value()
                                                      + delta.x());
                area->verticalScrollBar()->setValue(area->verticalScrollBar()->value()
                                                    + delta.y());
            }
            m_panLast = mouse->position().toPoint();
            return true;
        }
        if (m_erasing) {
            eraseAtPagePos(toPagePos(mouse->position()));
            return true;
        }
        if (m_inking) {
            // Same guard as the tablet path: preview stays out of the layer.
            m_builder.add({toPagePos(mouse->position()), 0.6});
            const Stroke &current = m_builder.current();
            const int n = current.samples.size();
            if (n > 0) {
                QRectF segment(current.samples.constLast().pos, QSizeF(1, 1));
                if (n >= 2) {
                    segment |= QRectF(current.samples.at(n - 2).pos, QSizeF(1, 1));
                }
                const qreal margin = repaintMargin(current);
                update(toWidgetRect(segment.adjusted(-margin, -margin, margin, margin)));
            }
            return true;
        }
        return QWidget::event(event);
    }
    if (event->type() == QEvent::MouseButtonRelease) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (m_panning && mouse->button() == Qt::MiddleButton) {
            m_panning = false;
            updateCursor();
            return true;
        }
        if (m_erasing && mouse->button() == Qt::LeftButton) {
            endEraseGesture();
            return true;
        }
        if (m_inking && mouse->button() == Qt::LeftButton) {
            commitStroke();
            return true;
        }
        return QWidget::event(event);
    }
    return QWidget::event(event);
}

void NotePageView::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        // Zoom about the cursor: the page point under it stays put.
        const QPointF pagePos = toPagePos(event->position());
        const qreal steps = event->angleDelta().y() / 120.0;
        setZoom(m_zoom * std::pow(1.25, steps));
        if (QScrollArea *area = scrollArea()) {
            const QPointF now = pageRect().topLeft() + pagePos * m_zoom;
            const QPointF delta = now - event->position();
            area->horizontalScrollBar()->setValue(area->horizontalScrollBar()->value()
                                                  + int(delta.x()));
            area->verticalScrollBar()->setValue(area->verticalScrollBar()->value()
                                                + int(delta.y()));
        }
        event->accept();
        return;
    }
    if (event->modifiers().testFlag(Qt::ShiftModifier)) {
        if (QScrollArea *area = scrollArea()) {
            area->horizontalScrollBar()->setValue(area->horizontalScrollBar()->value()
                                                  - event->angleDelta().y());
            event->accept();
            return;
        }
    }
    // Plain wheel scrolls: leave it to the hosting scroll area.
    QWidget::wheelEvent(event);
}

void NotePageView::keyPressEvent(QKeyEvent *event)
{
    // Esc returns to the Pen (M23 commits an open editor first, M24 clears
    // a selection first — both insert themselves ahead of this line).
    if (event->key() == Qt::Key_Escape && m_tool != NoteTool::Pen) {
        setTool(NoteTool::Pen);
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

} // namespace QindaOffice::Apps::Notes
