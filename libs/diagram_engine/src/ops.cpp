// SPDX-License-Identifier: GPL-3.0-or-later
// Ops: creation, removal, selection helpers, geometry and z-order.
// (Grouping, clipboard, style, data, find and pages: ops_edit.cpp.)
#include "ops.h"

#include "geometry.h"
#include "routing.h"
#include "stencils.h"
#include "themes.h"

#include <QLineF>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace QindaOffice::Diagram::Ops {

namespace {

QRectF boundsOf(const Page &page, const QStringList &ids)
{
    QRectF bounds;
    for (const Shape &shape : page.shapes) {
        if (ids.contains(shape.id)) {
            bounds = bounds.isNull() ? shapeBounds(shape) : bounds | shapeBounds(shape);
        }
    }
    return bounds;
}

QStringList shapeIds(const Page &page, const QStringList &ids)
{
    QStringList out;
    for (const Shape &shape : page.shapes) {
        if (ids.contains(shape.id) && !shape.locked) {
            out.append(shape.id);
        }
    }
    return out;
}

// Freezes every glued end on `shapeId` at its current position.
void unglueFrom(Page &page, const QString &shapeId)
{
    for (Connector &connector : page.connectors) {
        const Route route = routeConnector(page, connector);
        if (connector.from.shapeId == shapeId) {
            connector.from = Endpoint{QString(), -1, route.start()};
        }
        if (connector.to.shapeId == shapeId) {
            connector.to = Endpoint{QString(), -1, route.end()};
        }
    }
}

} // namespace

QString addShape(Drawing &drawing, Page &page, const QString &type, QPointF centre, const std::optional<QString> &text)
{
    const Stencil *stencil = Diagram::stencil(type);
    if (stencil == nullptr) {
        return {};
    }
    const Theme &palette = theme(drawing.themeId);
    Shape shape;
    shape.id = drawing.newId(QStringLiteral("s"));
    shape.type = type;
    shape.rect = QRectF(centre - QPointF(stencil->size.width() / 2, stencil->size.height() / 2), stencil->size);
    shape.text = text.value_or(stencil->defaultText);
    shape.style = shapeStyleFor(palette, stencil);
    shape.textStyle = textStyleFor(palette, stencil);
    shape.aspectLocked = stencil->aspectLocked;
    if (stencil->container) {
        // Containers go under everything so what is inside stays visible.
        int at = 0;
        while (at < page.shapes.size() && isContainer(page.shapes.at(at))) {
            ++at;
        }
        page.shapes.insert(at, shape);
    } else {
        page.shapes.append(shape);
    }
    return shape.id;
}

QString addConnector(Drawing &drawing, Page &page, const Endpoint &from, const Endpoint &to, Routing routing)
{
    if ((from.glued() && page.shape(from.shapeId) == nullptr) || (to.glued() && page.shape(to.shapeId) == nullptr)) {
        return {};
    }
    if (from.glued() && from.shapeId == to.shapeId && from.port == to.port) {
        return {};
    }
    Connector connector;
    connector.id = drawing.newId(QStringLiteral("c"));
    connector.from = from;
    connector.to = to;
    connector.routing = routing;
    styleConnector(theme(drawing.themeId), connector);
    page.connectors.append(connector);
    return connector.id;
}

int removeItems(Page &page, const QStringList &ids)
{
    int removed = 0;
    for (const QString &id : ids) {
        const int s = page.shapeIndex(id);
        if (s >= 0 && !page.shapes.at(s).locked) {
            unglueFrom(page, id);
            page.shapes.removeAt(s);
            ++removed;
        }
    }
    for (int i = int(page.connectors.size()) - 1; i >= 0; --i) {
        if (ids.contains(page.connectors.at(i).id)) {
            page.connectors.removeAt(i);
            ++removed;
        }
    }
    return removed;
}

QStringList expandGroups(const Page &page, const QStringList &ids)
{
    QSet<QString> groups;
    for (const Shape &shape : page.shapes) {
        if (ids.contains(shape.id) && !shape.groupId.isEmpty()) {
            groups.insert(shape.groupId);
        }
    }
    for (const Connector &connector : page.connectors) {
        if (ids.contains(connector.id) && !connector.groupId.isEmpty()) {
            groups.insert(connector.groupId);
        }
    }
    QStringList out = ids;
    for (const Shape &shape : page.shapes) {
        if (groups.contains(shape.groupId) && !out.contains(shape.id)) {
            out.append(shape.id);
        }
    }
    for (const Connector &connector : page.connectors) {
        if (groups.contains(connector.groupId) && !out.contains(connector.id)) {
            out.append(connector.id);
        }
    }
    return out;
}

QStringList containerMembers(const Page &page, const QString &containerId)
{
    const Shape *container = page.shape(containerId);
    if (container == nullptr || !isContainer(*container)) {
        return {};
    }
    const QRectF area = shapeBounds(*container).adjusted(-0.5, -0.5, 0.5, 0.5);
    const int containerIndex = page.shapeIndex(containerId);
    QStringList out;
    for (int i = 0; i < page.shapes.size(); ++i) {
        const Shape &shape = page.shapes.at(i);
        if (shape.id == containerId) {
            continue;
        }
        // Inside and painted above it: a lane that happens to lie under a
        // smaller container is not that container's member.
        if (area.contains(shapeBounds(shape)) && (i > containerIndex || !isContainer(shape))) {
            out.append(shape.id);
        }
    }
    return out;
}

QString shapeAt(const Page &page, QPointF point, const QStringList &ignore)
{
    for (int i = int(page.shapes.size()) - 1; i >= 0; --i) {
        const Shape &shape = page.shapes.at(i);
        if (!ignore.contains(shape.id) && shapeContains(shape, point)) {
            return shape.id;
        }
    }
    return {};
}

Endpoint glueAt(const Page &page, QPointF point, double tolerance, const QStringList &ignore)
{
    // A connection point wins when one is close, even on a shape below.
    for (int i = int(page.shapes.size()) - 1; i >= 0; --i) {
        const Shape &shape = page.shapes.at(i);
        if (ignore.contains(shape.id)) {
            continue;
        }
        const int port = nearestPort(shape, point, tolerance);
        if (port >= 0) {
            return Endpoint{shape.id, port, portPoint(shape, port)};
        }
    }
    const QString id = shapeAt(page, point, ignore);
    if (!id.isEmpty()) {
        const Shape *shape = page.shape(id);
        // Containers and lanes take glue only on their header or edge; a
        // drop inside a lane lands on the page, not on the lane.
        if (!isContainer(*shape)) {
            return Endpoint{id, -1, point};
        }
    }
    return Endpoint{QString(), -1, point};
}

bool setShapeGeometry(Page &page, const QString &id, const QRectF &rect, double rotation)
{
    Shape *shape = page.shape(id);
    if (shape == nullptr || shape->locked || !rect.isValid() || !std::isfinite(rotation)) {
        return false;
    }
    double r = std::remainder(rotation, 360.0);
    if (std::abs(r) < 1e-9) {
        r = 0;
    }
    const QRectF next(rect.topLeft(), QSizeF(std::max(4.0, rect.width()), std::max(4.0, rect.height())));
    if (shape->rect == next && qFuzzyCompare(shape->rotation + 360, r + 360)) {
        return false;
    }
    shape->rect = next;
    shape->rotation = r;
    return true;
}

bool moveItems(Page &page, const QStringList &ids, QPointF delta)
{
    if (delta.isNull() || !std::isfinite(delta.x()) || !std::isfinite(delta.y())) {
        return false;
    }
    QStringList moving = shapeIds(page, ids);
    for (const QString &id : QStringList(moving)) {
        for (const QString &member : containerMembers(page, id)) {
            if (!moving.contains(member)) {
                moving.append(member);
            }
        }
    }
    bool changed = false;
    for (Shape &shape : page.shapes) {
        if (moving.contains(shape.id) && !shape.locked) {
            shape.rect.translate(delta);
            changed = true;
        }
    }
    for (Connector &connector : page.connectors) {
        const bool listed = ids.contains(connector.id);
        const bool fromMoves = connector.from.glued() && moving.contains(connector.from.shapeId);
        const bool toMoves = connector.to.glued() && moving.contains(connector.to.shapeId);
        // A free end moves with the selection when its connector is
        // selected, or when its other end is glued to a moved shape.
        if (!connector.from.glued() && (listed || toMoves)) {
            connector.from.point += delta;
            changed = true;
        }
        if (!connector.to.glued() && (listed || fromMoves)) {
            connector.to.point += delta;
            changed = true;
        }
    }
    return changed;
}

bool align(Page &page, const QStringList &ids, Align edge)
{
    const QStringList targets = shapeIds(page, ids);
    if (targets.size() < 2) {
        return false;
    }
    const QRectF all = boundsOf(page, targets);
    bool changed = false;
    for (Shape &shape : page.shapes) {
        if (!targets.contains(shape.id)) {
            continue;
        }
        const QRectF b = shapeBounds(shape);
        QPointF delta;
        switch (edge) {
        case Align::Left: delta.setX(all.left() - b.left()); break;
        case Align::Center: delta.setX(all.center().x() - b.center().x()); break;
        case Align::Right: delta.setX(all.right() - b.right()); break;
        case Align::Top: delta.setY(all.top() - b.top()); break;
        case Align::Middle: delta.setY(all.center().y() - b.center().y()); break;
        case Align::Bottom: delta.setY(all.bottom() - b.bottom()); break;
        }
        if (std::abs(delta.x()) > 1e-9 || std::abs(delta.y()) > 1e-9) {
            shape.rect.translate(delta);
            changed = true;
        }
    }
    return changed;
}

bool distribute(Page &page, const QStringList &ids, Qt::Orientation orientation)
{
    const QStringList targets = shapeIds(page, ids);
    if (targets.size() < 3) {
        return false;
    }
    const bool horizontal = orientation == Qt::Horizontal;
    QVector<int> order;
    for (int i = 0; i < page.shapes.size(); ++i) {
        if (targets.contains(page.shapes.at(i).id)) {
            order.append(i);
        }
    }
    const auto start = [&](int i) {
        const QRectF b = shapeBounds(page.shapes.at(i));
        return horizontal ? b.left() : b.top();
    };
    const auto extent = [&](int i) {
        const QRectF b = shapeBounds(page.shapes.at(i));
        return horizontal ? b.width() : b.height();
    };
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return start(a) < start(b); });
    // Equal gaps between neighbours; the outermost shapes stay.
    double used = 0;
    for (int i : order) {
        used += extent(i);
    }
    const double first = start(order.first());
    const double last = start(order.last()) + extent(order.last());
    const double gap = (last - first - used) / double(order.size() - 1);
    double cursor = first;
    bool changed = false;
    for (int i : order) {
        const double shift = cursor - start(i);
        if (std::abs(shift) > 1e-9) {
            page.shapes[i].rect.translate(horizontal ? QPointF(shift, 0) : QPointF(0, shift));
            changed = true;
        }
        cursor += extent(i) + gap;
    }
    return changed;
}

bool rotate(Page &page, const QStringList &ids, double degrees)
{
    const QStringList targets = shapeIds(page, ids);
    if (targets.isEmpty() || !std::isfinite(degrees)) {
        return false;
    }
    const QPointF pivot = boundsOf(page, targets).center();
    QTransform turn;
    turn.translate(pivot.x(), pivot.y());
    turn.rotate(degrees);
    turn.translate(-pivot.x(), -pivot.y());
    for (Shape &shape : page.shapes) {
        if (targets.contains(shape.id)) {
            const QPointF centre = turn.map(shape.rect.center());
            shape.rect.moveCenter(centre);
            double r = std::remainder(shape.rotation + degrees, 360.0);
            shape.rotation = std::abs(r) < 1e-9 ? 0 : r;
        }
    }
    return true;
}

bool flip(Page &page, const QStringList &ids, Qt::Orientation orientation)
{
    const QStringList targets = shapeIds(page, ids);
    if (targets.isEmpty()) {
        return false;
    }
    const QPointF pivot = boundsOf(page, targets).center();
    const bool horizontal = orientation == Qt::Horizontal;
    for (Shape &shape : page.shapes) {
        if (!targets.contains(shape.id)) {
            continue;
        }
        QPointF centre = shape.rect.center();
        if (horizontal) {
            centre.setX(pivot.x() * 2 - centre.x());
            shape.flipX = !shape.flipX;
        } else {
            centre.setY(pivot.y() * 2 - centre.y());
            shape.flipY = !shape.flipY;
        }
        shape.rotation = shape.rotation == 0 ? 0 : std::remainder(-shape.rotation, 360.0);
        shape.rect.moveCenter(centre);
    }
    return true;
}

bool setAspectLocked(Page &page, const QStringList &ids, bool locked)
{
    bool changed = false;
    for (Shape &shape : page.shapes) {
        if (ids.contains(shape.id) && shape.aspectLocked != locked) {
            shape.aspectLocked = locked;
            changed = true;
        }
    }
    return changed;
}

bool reorder(Page &page, const QStringList &ids, Order order)
{
    const auto move = [&](auto &items) {
        using Item = typename std::decay_t<decltype(items)>::value_type;
        QVector<Item> selected;
        QVector<Item> rest;
        for (const Item &item : items) {
            (ids.contains(item.id) ? selected : rest).append(item);
        }
        if (selected.isEmpty()) {
            return false;
        }
        const QVector<Item> before = items;
        if (order == Order::Front) {
            items = rest + selected;
        } else if (order == Order::Back) {
            items = selected + rest;
        } else {
            // One step: swap each selected item with its unselected
            // neighbour in the direction of travel.
            const bool forward = order == Order::Forward;
            const int n = int(items.size());
            for (int k = 0; k < n; ++k) {
                const int i = forward ? n - 1 - k : k;
                const int j = forward ? i + 1 : i - 1;
                if (j < 0 || j >= n) {
                    continue;
                }
                if (ids.contains(items.at(i).id) && !ids.contains(items.at(j).id)) {
                    items.swapItemsAt(i, j);
                }
            }
        }
        return !(items == before);
    };
    const bool shapes = move(page.shapes);
    const bool connectors = move(page.connectors);
    return shapes || connectors;
}

bool setEndpoint(Page &page, const QString &connectorId, bool start, const Endpoint &end)
{
    Connector *connector = page.connector(connectorId);
    if (connector == nullptr || (end.glued() && page.shape(end.shapeId) == nullptr)) {
        return false;
    }
    Endpoint &target = start ? connector->from : connector->to;
    if (target == end) {
        return false;
    }
    target = end;
    return true;
}

} // namespace QindaOffice::Diagram::Ops
