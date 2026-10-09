// SPDX-License-Identifier: GPL-3.0-or-later
// SlideCanvasItem input handling: pointer gestures (select, Shift-toggle,
// move, corner-grip resize, marquee), keys, and the in-place text edit a
// double-click starts. Split out of slide_canvas_item.cpp for the line
// budget (AGENTS.md).
//
// AGENT-GUARD: the canvas owns all model mutation and reports old/new
// payloads the host turns into undo commands (D-007); nothing here may
// edit a shape without emitting the matching signal.

#include "slide_canvas_item.h"

#include "text_bridge.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QTextDocument>

namespace QindaOffice::Apps::Show {

void SlideCanvasItem::mousePressEvent(QMouseEvent *event)
{
    forceActiveFocus(Qt::MouseFocusReason);
    const QPointF pos = event->position();
    if (event->button() == Qt::RightButton) {
        // A right press over an unselected shape selects it first, so the
        // context menu acts on what is under the pointer.
        const QString hit = shapeAt(pos);
        if (!hit.isEmpty() && !m_selected.contains(hit)) {
            m_selected = {hit};
            notifySelection();
        }
        emit contextMenuRequested(pos.x(), pos.y());
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton || m_slide == nullptr) {
        event->ignore();
        return;
    }
    if (!m_textShapeId.isEmpty()) {
        // Clicks outside the editor commit the edit.
        if (!editorRect().contains(pos)) {
            commitTextEdit(false);
        } else {
            event->ignore();
            return;
        }
    }
    m_pressPos = pos;
    m_moved = false;
    m_gestureStart.clear();
    // M67: inside the selected table a press selects, edits or resizes
    // cells (its frame edge still moves it; no grip, its corner is the last
    // column's boundary).
    if (tablePress(pos, event->modifiers())) {
        event->accept();
        return;
    }
    // Corner-grip resize on a single selected shape.
    if (m_selected.size() == 1) {
        const ShowModel::Shape *shape = m_slide->findShape(selectedShapeId());
        if (shape != nullptr) {
            const QRectF grip(shape->rect.normalized().bottomRight() - QPointF(kGripRadius, kGripRadius),
                              QSizeF(kGripRadius * 2, kGripRadius * 2));
            if (grip.contains(pos)) {
                m_gesture = Gesture::Resize;
                m_draggingShapeId = shape->id;
                m_gestureStart.insert(shape->id, shape->rect);
                event->accept();
                return;
            }
        }
    }
    const QString hit = shapeAt(pos);
    if (hit.isEmpty()) {
        if ((event->modifiers() & Qt::ShiftModifier) == 0) {
            clearSelection();
        }
        m_gesture = Gesture::Marquee;
        m_marquee = QRectF(pos, pos);
        event->accept();
        return;
    }
    if (event->modifiers() & Qt::ShiftModifier) {
        setSelected(hit, !m_selected.contains(hit));
    } else if (!m_selected.contains(hit)) {
        m_selected = {hit};
        notifySelection();
    }
    // Grouped shapes select as one, and the pointer's own shape is the one
    // that snaps; note both before the drag starts.
    if (const ShowModel::Shape *shape = m_slide->findShape(hit); shape != nullptr && !shape->groupId.isEmpty()) {
        for (const QString &id : m_slide->shapeIdsInGroup(shape->groupId)) {
            m_selected.insert(id);
        }
        notifySelection();
    }
    m_draggingShapeId = hit;
    m_gesture = Gesture::Move;
    for (const QString &id : m_selected) {
        if (const ShowModel::Shape *shape = m_slide->findShape(id)) {
            m_gestureStart.insert(id, shape->rect);
        }
    }
    event->accept();
}

void SlideCanvasItem::mouseMoveEvent(QMouseEvent *event)
{
    if (m_gesture == Gesture::None || m_slide == nullptr) {
        event->ignore();
        return;
    }
    const QPointF pos = event->position();
    const QPointF delta = pos - m_pressPos;
    if (tableMove(pos)) {
        event->accept();
        return;
    }
    if (m_gesture == Gesture::Marquee) {
        m_marquee = QRectF(m_pressPos, pos).normalized();
        update();
        event->accept();
        return;
    }
    if (!m_moved && delta.manhattanLength() < 2) {
        return;
    }
    if (!m_moved) {
        m_moved = true;
        if (m_gesture == Gesture::Move && m_gestureStart.size() > 1) {
            // A multi-shape drag is ONE action: the host snapshots here
            // and pushes a single command on release.
            m_compound = true;
            emit compoundEditStarted();
        }
    }
    if (m_gesture == Gesture::Resize) {
        QRectF rect = m_gestureStart.value(m_draggingShapeId);
        rect.setBottomRight(rect.bottomRight() + delta);
        if (rect.width() >= 20 && rect.height() >= 14) {
            if (ShowModel::Shape *shape = m_slide->findShape(m_draggingShapeId)) {
                shape->rect = snapRect(shape->id, rect.normalized());
            }
        }
        update();
        event->accept();
        return;
    }
    // Move: snap only the shape the pointer has hold of; the rest of the
    // selection follow it exactly, so a group is never pulled apart.
    const QRectF primaryStart = m_gestureStart.value(m_draggingShapeId);
    const QRectF snapped = snapRect(m_draggingShapeId, primaryStart.translated(delta));
    const QPointF applied = snapped.topLeft() - primaryStart.topLeft();
    for (auto it = m_gestureStart.constBegin(); it != m_gestureStart.constEnd(); ++it) {
        if (ShowModel::Shape *shape = m_slide->findShape(it.key())) {
            shape->rect = it.value().translated(applied);
        }
    }
    update();
    event->accept();
}

void SlideCanvasItem::finishGesture()
{
    if (m_slide == nullptr) {
        m_gesture = Gesture::None;
        return;
    }
    if (m_gesture == Gesture::Marquee) {
        if (m_marquee.width() > 2 || m_marquee.height() > 2) {
            for (const ShowModel::Shape &shape : m_slide->shapes) {
                if (m_marquee.intersects(shape.rect.normalized())) {
                    m_selected.insert(shape.id);
                }
            }
            notifySelection();
        }
    } else if (m_moved) {
        for (auto it = m_gestureStart.constBegin(); it != m_gestureStart.constEnd(); ++it) {
            const ShowModel::Shape *shape = m_slide->findShape(it.key());
            if (shape != nullptr && shape->rect != it.value()) {
                emit shapeGeometryEdited(it.key(), it.value(), shape->rect);
            }
        }
        emit slideEdited();
    }
    // M67: a click (no drag) into a table puts the caret in the cell under
    // it, as PowerPoint does; a click on its frame edge only selects it.
    const QString clickedTable = m_gesture == Gesture::Move && !m_moved && m_selected.size() == 1 ? tableShapeId() : QString();
    const QPointF clickedAt = m_pressPos;
    m_gesture = Gesture::None;
    m_gestureStart.clear();
    m_draggingShapeId.clear();
    m_marquee = QRectF();
    clearGuides();
    if (m_compound) {
        m_compound = false;
        emit compoundEditFinished();
    }
    update();
    if (!clickedTable.isEmpty()) {
        tablePress(clickedAt, Qt::NoModifier); // the cell under the click…
        tableRelease();                         // …edited, as a click into the selected table is
    }
}

void SlideCanvasItem::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || m_gesture == Gesture::None) {
        event->ignore();
        return;
    }
    if (!tableRelease()) {
        finishGesture();
    }
    event->accept();
}

void SlideCanvasItem::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || m_slide == nullptr) {
        event->ignore();
        return;
    }
    QString target = selectedShapeId();
    const QString hit = shapeAt(event->position());
    if (!hit.isEmpty()) {
        target = hit;
    }
    if (target.isEmpty()) {
        event->ignore();
        return;
    }
    m_gesture = Gesture::None;
    const ShowModel::Shape *shape = m_slide->findShape(target);
    if (shape != nullptr && shape->kind == ShowModel::ShapeKind::Table && !shape->table.isEmpty()) {
        // A table: the caret goes into the cell under the pointer.
        const ShowModel::TableGrid grid = ShowModel::tableGrid(shape->table, shape->rect.normalized());
        int row = 0;
        while (row + 2 < grid.ys.size() && event->position().y() >= grid.ys.at(row + 1)) {
            ++row;
        }
        int column = 0;
        while (column + 2 < grid.xs.size() && event->position().x() >= grid.xs.at(column + 1)) {
            ++column;
        }
        beginCellEdit(target, row, column);
    } else {
        beginTextEdit(target);
    }
    event->accept();
}

void SlideCanvasItem::keyPressEvent(QKeyEvent *event)
{
    if (!m_textShapeId.isEmpty()) {
        event->ignore();
        return;
    }
    if (tableKey(event)) {
        event->accept();
        return;
    }
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && hasSelection()) {
        deleteSelected();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Cancel)) {
        clearSelection();
        event->accept();
        return;
    }
    event->ignore();
}

// ---- in-place text editing ---------------------------------------------------------

QRectF SlideCanvasItem::editorRect() const
{
    const ShowModel::Shape *shape = m_slide != nullptr ? m_slide->findShape(m_textShapeId) : nullptr;
    if (shape == nullptr) {
        return {};
    }
    if (m_textRow >= 0) {
        return cellEditorRect(*shape); // a table cell, inside its margins
    }
    // The shape's own text insets (6/4 pt for QindaShow's, the file's for
    // an Office shape).
    const ShowModel::TextBodyStyle &body = shape->textBody;
    return shape->rect.normalized().adjusted(body.insetLeft, body.insetTop, -body.insetRight, -body.insetBottom);
}

void SlideCanvasItem::beginTextEdit(const QString &shapeId)
{
    commitTextEdit(false);
    ShowModel::Shape *shape = m_slide != nullptr ? m_slide->findShape(shapeId) : nullptr;
    // Pictures, lines, groups, tables, charts and preserved Office content
    // have no text of their own to edit in place.
    if (shape == nullptr || !shape->holdsText()) {
        return;
    }
    m_textShapeId = shape->id;
    m_textOriginal = shape->paragraphs;
    m_textDocument = documentFromParagraphs(shape->paragraphs, this);
    m_textDocument->setTextWidth(qMax<qreal>(editorRect().width(), 40));
    m_selected = {shape->id};
    notifySelection();
    emit editingChanged();
}

void SlideCanvasItem::commitTextEdit(bool cancelled)
{
    if (m_textDocument == nullptr) {
        return;
    }
    if (m_textRow >= 0) {
        commitCellEdit(cancelled);
        return;
    }
    const QString shapeId = m_textShapeId;
    const QList<ShowModel::TextParagraph> original = m_textOriginal;
    const QList<ShowModel::TextParagraph> committed = paragraphsFromDocument(*m_textDocument);
    // The QML TextEdit may still reference the document this event-loop
    // pass; let it go once the window has moved on.
    m_textDocument->deleteLater();
    m_textDocument = nullptr;
    m_textShapeId.clear();
    m_textOriginal.clear();
    emit editingChanged();
    ShowModel::Shape *shape = m_slide != nullptr && !cancelled ? m_slide->findShape(shapeId) : nullptr;
    if (shape != nullptr && committed != shape->paragraphs) {
        shape->paragraphs = committed;
        emit shapeTextEdited(shapeId, original, committed);
        emit slideEdited();
    }
    update();
}

} // namespace QindaOffice::Apps::Show
