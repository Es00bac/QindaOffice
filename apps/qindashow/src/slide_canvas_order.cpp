// SPDX-License-Identifier: GPL-3.0-or-later
// SlideCanvas multi-selection and z-order: the operations that act on
// several shapes at once (align/distribute feed setShapeRects) and the
// two z-order extremes. Split out of slide_canvas.cpp to keep both files
// inside the line budget (AGENTS.md source discipline).

#include "slide_canvas.h"

#include <QGraphicsScene>

namespace QindaOffice::Apps::Show {

QStringList SlideCanvas::selectedShapeIds() const
{
    QStringList ids;
    const QStringList order = shapeOrder();
    for (const QString &id : order) {
        if (const ShapeItem *item = itemFor(id); item != nullptr && item->isSelected()) {
            ids.append(id);
        }
    }
    return ids;
}

void SlideCanvas::reorderTo(const QStringList &order)
{
    // AGENT-GUARD: reorder the MODEL and re-apply z values; never
    // rebuildItems() here. rebuildItems() clears the scene, and clearing it
    // fires QGraphicsScene::selectionChanged while the items are being
    // destroyed — the canvas's own handler then walks the half-dead item
    // list (SIGSEGV, found by test). Items are bound to shapes by id, so a
    // pure order change needs no new items at all.
    QList<ShowModel::Shape> reordered;
    reordered.reserve(m_slide->shapes.size());
    for (const QString &id : order) {
        if (ShowModel::Shape *shape = m_slide->findShape(id)) {
            reordered.append(*shape);
        }
    }
    m_slide->shapes = reordered;
    applyZOrder();
}

void SlideCanvas::setShapeRects(const QHash<QString, QRectF> &rects)
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
        // prepareGeometryChange BEFORE the model moves: the scene indexes
        // items by their bounding rect, and changing the rect behind its
        // back leaves stale entries that hit-testing then follows.
        if (ShapeItem *item = itemFor(shape.id)) {
            item->prepareGeometryChangeForRect();
        }
        shape.rect = it.value();
        changed = true;
    }
    if (!changed) {
        return;
    }
    for (const QString &id : rects.keys()) {
        if (ShapeItem *item = itemFor(id)) {
            item->update();
        }
    }
    emit slideEdited();
}

void SlideCanvas::bringSelectedToFront()
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

void SlideCanvas::sendSelectedToBack()
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
