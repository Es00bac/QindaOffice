// SPDX-License-Identifier: GPL-3.0-or-later
// SlideCanvas input handling: pointer and key events, and the in-place text
// edit they start and commit. Split out of slide_canvas.cpp to keep every
// file under the line budget (AGENTS.md).
//
// AGENT-GUARD: the canvas owns all model mutation and reports old/new
// payloads the window turns into undo commands (decisions D-007); nothing
// here may edit a shape without emitting the matching signal.

#include "slide_canvas.h"

#include "text_bridge.h"

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsTextItem>
#include <QKeyEvent>
#include <QMouseEvent>

namespace QindaOffice::Apps::Show {

void SlideCanvas::mousePressEvent(QMouseEvent *event)
{
    if (m_textItem != nullptr) {
        // Clicks outside the editor frame commit the edit.
        const QPointF scenePos = mapToScene(event->position().toPoint());
        if (!m_textItem->boundingRect().translated(m_textItem->pos()).contains(scenePos)) {
            commitTextEdit(false);
        }
        QGraphicsView::mousePressEvent(event);
        return;
    }
    if (event->button() == Qt::LeftButton) {
        // Corner-grip resize on the selected item.
        ShapeItem *item = selectedItem();
        if (item != nullptr) {
            const QPointF scenePos = mapToScene(event->position().toPoint());
            const QRectF grip(item->modelShape()->rect.normalized().bottomRight()
                                  - QPointF(kGripRadius, kGripRadius),
                              QSizeF(kGripRadius * 2, kGripRadius * 2));
            if (grip.contains(scenePos)) {
                m_resizing = item;
                m_resizeOrigRect = item->modelShape()->rect;
                m_resizeStart = scenePos;
                event->accept();
                return;
            }
        }
    }
    QGraphicsView::mousePressEvent(event);
}

void SlideCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (m_resizing != nullptr) {
        const QPointF delta = mapToScene(event->position().toPoint()) - m_resizeStart;
        QRectF rect = m_resizeOrigRect;
        rect.setBottomRight(rect.bottomRight() + delta);
        if (rect.width() >= 20 && rect.height() >= 14) {
            m_resizing->modelShape()->rect = rect.normalized();
            m_resizing->update();
        }
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void SlideCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_resizing != nullptr && event->button() == Qt::LeftButton) {
        emit shapeGeometryEdited(m_resizing->shapeId(), m_resizeOrigRect,
                                 m_resizing->modelShape()->rect);
        emit slideEdited();
        m_resizing = nullptr;
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void SlideCanvas::mouseDoubleClickEvent(QMouseEvent *event)
{
    ShapeItem *item = selectedItem();
    if (item == nullptr) {
        const QList<QGraphicsItem *> atPoint =
            m_scene->items(mapToScene(event->position().toPoint()));
        for (QGraphicsItem *candidate : atPoint) {
            if (auto *shapeItem = qgraphicsitem_cast<ShapeItem *>(candidate)) {
                item = shapeItem;
                break;
            }
        }
    }
    if (item != nullptr && m_slide != nullptr) {
        beginTextEdit(item);
        event->accept();
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void SlideCanvas::keyPressEvent(QKeyEvent *event)
{
    if (m_textItem != nullptr) {
        QGraphicsView::keyPressEvent(event);
        return;
    }
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
        && selectedItem() != nullptr) {
        deleteSelected();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Cancel)) {
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void SlideCanvas::beginTextEdit(ShapeItem *item)
{
    commitTextEdit(true);
    ShowModel::Shape *shape = item->modelShape();
    m_textShapeId = shape->id;
    m_textOriginal = shape->paragraphs;
    auto *document = documentFromParagraphs(shape->paragraphs, this);
    m_textItem = m_scene->addText(QString());
    m_textItem->setTextInteractionFlags(Qt::TextEditorInteraction);
    m_textItem->setTextWidth(qMax<qreal>(shape->rect.width() - 12, 40));
    m_textItem->setDocument(document);
    m_textItem->setPos(shape->rect.normalized().topLeft() + QPointF(6, 4));
    m_textItem->setZValue(1000.0);
    m_scene->clearSelection();
    m_textItem->setSelected(true);
}

void SlideCanvas::commitTextEdit(bool cancelled)
{
    if (m_textItem == nullptr) {
        return;
    }
    const QString shapeId = m_textShapeId;
    const QList<ShowModel::TextParagraph> original = m_textOriginal;
    const QList<ShowModel::TextParagraph> committed = paragraphsFromDocument(*m_textItem->document());
    m_scene->removeItem(m_textItem);
    delete m_textItem;
    m_textItem = nullptr;
    m_textShapeId.clear();
    m_textOriginal.clear();
    if (m_slide == nullptr || cancelled) {
        return;
    }
    const int index = shapeIndex(shapeId);
    if (index < 0) {
        return;
    }
    ShowModel::Shape *shape = &m_slide->shapes[index];
    if (committed != shape->paragraphs) {
        shape->paragraphs = committed;
        emit shapeTextEdited(shapeId, original, committed);
        emit slideEdited();
    }
    if (ShapeItem *item = itemFor(shapeId)) {
        item->update();
    }
}

} // namespace QindaOffice::Apps::Show
