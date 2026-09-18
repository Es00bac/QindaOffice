// SPDX-License-Identifier: GPL-3.0-or-later
// SlideCanvasItem multi-selection geometry and z-order: the operations
// that act on several shapes at once (align/distribute feed
// setShapeRects) and the two z-order extremes. Split out of
// slide_canvas_item.cpp for the line budget (AGENTS.md).

#include "slide_canvas_item.h"

namespace QindaOffice::Apps::Show {

void SlideCanvasItem::reorderTo(const QStringList &order)
{
    // Reorder the MODEL; ids bind selection to shapes, so a pure order
    // change needs nothing else.
    QList<ShowModel::Shape> reordered;
    reordered.reserve(m_slide->shapes.size());
    for (const QString &id : order) {
        if (ShowModel::Shape *shape = m_slide->findShape(id)) {
            reordered.append(*shape);
        }
    }
    m_slide->shapes = reordered;
    update();
}

void SlideCanvasItem::setShapeRects(const QHash<QString, QRectF> &rects)
{
    if (m_slide == nullptr || rects.isEmpty()) {
        return;
    }
    bool changed = false;
    for (ShowModel::Shape &shape : m_slide->shapes) {
        const auto it = rects.constFind(shape.id);
        if (it == rects.constEnd() || it.value() == shape.rect) {
            continue;
        }
        shape.rect = it.value();
        changed = true;
    }
    if (!changed) {
        return;
    }
    update();
    emit slideEdited();
}

void SlideCanvasItem::bringSelectedToFront()
{
    const QStringList selected = selectedShapeIds();
    if (m_slide == nullptr || selected.isEmpty()) {
        return;
    }
    const QStringList oldOrder = shapeOrder();
    QStringList newOrder;
    for (const QString &id : oldOrder) {
        if (!selected.contains(id)) {
            newOrder.append(id);
        }
    }
    newOrder += selected;
    if (newOrder == oldOrder) {
        return;
    }
    reorderTo(newOrder);
    emit shapesReordered(oldOrder, newOrder);
    emit slideEdited();
}

void SlideCanvasItem::sendSelectedToBack()
{
    const QStringList selected = selectedShapeIds();
    if (m_slide == nullptr || selected.isEmpty()) {
        return;
    }
    const QStringList oldOrder = shapeOrder();
    QStringList newOrder = selected;
    for (const QString &id : oldOrder) {
        if (!selected.contains(id)) {
            newOrder.append(id);
        }
    }
    if (newOrder == oldOrder) {
        return;
    }
    reorderTo(newOrder);
    emit shapesReordered(oldOrder, newOrder);
    emit slideEdited();
}

} // namespace QindaOffice::Apps::Show
