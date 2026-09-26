// SPDX-License-Identifier: GPL-3.0-or-later
// NotePageItem's ink: stylus gestures from the Tk.StylusHandler, the LIVE
// eraser, tool cursors, the eraser-end and barrel-button temporary tools.
// Split out of note_page_item.cpp to keep both inside the line budget.
//
// Pointer rules (plan section 3.3), in order:
//  1. Touch arrives only while `touchDrawing` is on and an ink tool is
//     active (NotePage.qml binds the handler's accepted devices). A finger
//     stroke that a second finger interrupts is a pinch/pan, not ink: the
//     handler cancels it and the stroke (or the erase) is undone.
//  2. The stylus eraser end and a held barrel MiddleButton borrow the
//     Eraser for one gesture; both revert at the end.
//  3. Pan and zoom are the Viewport's (middle-drag, Ctrl+wheel, two-finger
//     pinch/pan, one-finger pan when the finger does not ink).
//  4. Ink tools ink over blocks — blocks are painted, not widgets.
//  5. The mouse inks with the handler's fixed pressure — laptops without a
//     pen must work, and the tests drive that path.

#include "note_page_item.h"

#include "note_page_history.h"

#include <QCursor>
#include <QPainter>
#include <QPixmap>
#include <QUndoStack>

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

void NotePageItem::setStylus(QindaTK::StylusHandler *handler)
{
    if (m_stylus == handler) {
        return;
    }
    if (m_stylus != nullptr) {
        disconnect(m_stylus, nullptr, this, nullptr);
    }
    m_stylus = handler;
    if (m_stylus != nullptr) {
        connect(m_stylus, &QindaTK::StylusHandler::began, this, &NotePageItem::onStylusBegan);
        connect(m_stylus, &QindaTK::StylusHandler::moved, this, &NotePageItem::onStylusMoved);
        connect(m_stylus, &QindaTK::StylusHandler::ended, this, &NotePageItem::onStylusEnded);
        connect(m_stylus, &QindaTK::StylusHandler::canceled, this,
                &NotePageItem::onStylusCanceled);
    }
    emit stylusChanged();
}

void NotePageItem::setTouchDrawing(bool on)
{
    if (m_touchDrawing == on) {
        return;
    }
    m_touchDrawing = on;
    emit presentationChanged();
}

void NotePageItem::onStylusCanceled()
{
    if (!m_touchGesture) {
        // A pen whose grab was taken keeps what it drew.
        if (m_erasing) {
            endEraseGesture();
        } else if (m_inking) {
            commitStroke();
        }
        restoreTemporaryTool();
        return;
    }
    // AGENT-NOTE: a second finger landed — the first finger's few
    // samples were the start of a pinch or pan, never meant as ink. Leave
    // no dot and no undo step behind.
    m_touchGesture = false;
    if (m_erasing) {
        m_erasing = false;
        if (m_eraseChanged && m_page != nullptr) {
            m_page->envelope.ink = m_eraseSnapshot;
            rebuildGrid();
            update();
        }
        m_eraseSnapshot = Ink::StrokeLayer{};
    } else if (m_inking) {
        const Stroke dropped = m_builder.end();
        m_inking = false;
        const qreal margin = repaintMargin(dropped);
        update(dropped.bounds().adjusted(-margin, -margin, margin, margin).toAlignedRect());
    }
    restoreTemporaryTool();
}

void NotePageItem::updateCursor()
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

QCursor NotePageItem::eraserCursor() const
{
    // A ring the size of the eraser disc (radius x zoom): the user should
    // see exactly what one swipe will eat. White halo + black core stays
    // readable over dark ink on the white page.
    const int diameter = qBound(8, int(m_eraserRadius * 2.0 * m_viewZoom), 128);
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

void NotePageItem::beginTemporaryTool(NoteTool tool)
{
    if (m_temporaryTool) {
        return; // eraser end AND barrel button: one restore is enough
    }
    m_temporaryTool = true;
    m_toolBeforeTemporary = m_tool;
    setTool(tool);
}

void NotePageItem::restoreTemporaryTool()
{
    if (!m_temporaryTool) {
        return;
    }
    m_temporaryTool = false;
    setTool(m_toolBeforeTemporary);
}

void NotePageItem::beginEraseGesture(const QPointF &pagePos)
{
    // AGENT-CONTRACT: ONE undo step per gesture. The snapshot is taken at
    // press; the InkCommand is pushed at the end ONLY when the drag
    // actually erased something — an empty swipe must not grow the history.
    m_erasing = true;
    m_eraseChanged = false;
    m_eraseSnapshot = m_page->envelope.ink;
    eraseAtPagePos(pagePos);
}

void NotePageItem::eraseAtPagePos(const QPointF &pagePos)
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
        update(touched.toAlignedRect().adjusted(-1, -1, 1, 1));
        emit pageEdited();
    }
}

void NotePageItem::endEraseGesture()
{
    m_erasing = false;
    if (m_eraseChanged) {
        m_history->push(new InkCommand(tr("Erase"), m_eraseSnapshot, m_page->envelope.ink, this));
    }
    m_eraseSnapshot = Ink::StrokeLayer{};
}

void NotePageItem::repaintSegment()
{
    // Narrow repaint: only the fresh segment, never the page.
    const Stroke &current = m_builder.current();
    const int n = current.samples.size();
    if (n == 0) {
        return;
    }
    QRectF segment(current.samples.constLast().pos, QSizeF(1, 1));
    if (n >= 2) {
        segment |= QRectF(current.samples.at(n - 2).pos, QSizeF(1, 1));
    }
    const qreal margin = repaintMargin(current);
    update(segment.adjusted(-margin, -margin, margin, margin).toAlignedRect());
}

void NotePageItem::commitStroke()
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
    updatePageMetrics();
    update(stroke.bounds().toAlignedRect().adjusted(-2, -2, 2, 2));
    emit pageEdited();
}

void NotePageItem::onStylusBegan(const QindaTK::StylusSample &sample)
{
    if (m_page == nullptr) {
        return;
    }
    // The stylus eraser end, or a held barrel MiddleButton, borrows the
    // eraser until the gesture ends (plan section 3.3 rule 2).
    m_touchGesture = sample.type == QPointingDevice::PointerType::Finger;
    const bool eraserEnd = sample.type == QPointingDevice::PointerType::Eraser;
    if (eraserEnd || (Qt::MouseButtons(sample.buttons) & Qt::MiddleButton)) {
        beginTemporaryTool(NoteTool::Eraser);
    }
    if (m_tool == NoteTool::Text) {
        tapAt(sample.position);
        return;
    }
    if (m_editingBlock >= 0) {
        commitEdit(); // inking over an open editor closes it first
    }
    if (!pageRect().contains(sample.position)) {
        return;
    }
    const QPointF pos = sample.position;
    const qreal pressure = m_pressureEnabled ? sample.pressure : 0.5;
    if (m_tool == NoteTool::Eraser) {
        beginEraseGesture(pos);
        return;
    }
    const bool highlight = m_tool == NoteTool::Highlighter;
    m_builder.begin({pos, pressure, sample.tiltX, sample.tiltY},
                    highlight ? StrokeTool::Highlighter : StrokeTool::Pen,
                    highlight ? m_highlighterColor : m_penColor,
                    highlight ? m_highlighterWidth : m_penWidth);
    m_inking = true;
}

void NotePageItem::onStylusMoved(const QindaTK::StylusSample &sample)
{
    if (m_page == nullptr) {
        return;
    }
    if (m_erasing) {
        eraseAtPagePos(sample.position);
        return;
    }
    if (m_inking) {
        // AGENT-GUARD: the in-progress stroke is rendered from
        // m_builder.current() in paint() and must NEVER touch the committed
        // layer during moves.
        const qreal pressure = m_pressureEnabled ? sample.pressure : 0.5;
        m_builder.add({sample.position, pressure, sample.tiltX, sample.tiltY});
        repaintSegment();
    }
}

void NotePageItem::onStylusEnded(const QindaTK::StylusSample &sample)
{
    Q_UNUSED(sample);
    if (m_page == nullptr) {
        return;
    }
    if (m_erasing) {
        endEraseGesture();
    } else if (m_inking) {
        commitStroke();
    }
    restoreTemporaryTool();
}

} // namespace QindaOffice::Apps::Notes
