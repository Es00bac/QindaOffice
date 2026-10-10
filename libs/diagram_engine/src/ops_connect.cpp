// SPDX-License-Identifier: GPL-3.0-or-later
// Ops (M87, D-095): Visio's AutoConnect (ops.h) — the blue arrows around a
// shape: connect to the nearest shape lying in that direction, or add a
// shape there and connect to it, one step either way.
#include "ops.h"

#include "geometry.h"
#include "layers.h"
#include "stencils.h"

#include <QLineF>

#include <algorithm>
#include <cmath>

namespace QindaOffice::Diagram::Ops {

namespace {

// Unit vector of side `side` (0 top, 1 right, 2 bottom, 3 left).
QPointF directionOf(int side)
{
    static const QPointF dirs[] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    return dirs[std::clamp(side, 0, 3)];
}

// The connection point facing `side` when the shape has the four side
// midpoints first (every stencil does; stencils.cpp sides()), else the
// outline (dynamic glue).
int portFacing(const Shape &shape, int side)
{
    const QVector<QPointF> ports = portsOf(shape);
    static const QPointF wanted[] = {{0.5, 0}, {1, 0.5}, {0.5, 1}, {0, 0.5}};
    const QPointF target = wanted[std::clamp(side, 0, 3)];
    for (int i = 0; i < ports.size(); ++i) {
        if (QLineF(ports.at(i), target).length() < 1e-6) {
            return std::abs(shape.rotation) < 1e-6 ? i : -1;
        }
    }
    return -1;
}

bool blocking(const Shape &s)
{
    return !isContainer(s) && s.type != QLatin1String("text");
}

} // namespace

QString autoConnect(Drawing &drawing, Page &page, const QString &fromId, int side, const QString &stencilId,
                    double gap, QString *connectorId)
{
    const Shape *source = page.shape(fromId);
    if (source == nullptr || side < 0 || side > 3 || !std::isfinite(gap)) {
        return {};
    }
    const QPointF dir = directionOf(side);
    const bool horizontal = side == 1 || side == 3;
    const QRectF from = shapeBounds(*source);
    // 1. The nearest shape in line: its box overlaps the source's band
    // across the direction, and it lies ahead within reach.
    const double reach = gap * 4 + (horizontal ? from.width() : from.height()) * 2;
    QString target;
    double best = reach;
    for (const Shape &s : std::as_const(page.shapes)) {
        if (s.id == fromId || !blocking(s) || !layersVisible(page, s.layers)) {
            continue;
        }
        const QRectF b = shapeBounds(s);
        const bool inBand = horizontal ? (b.bottom() > from.top() && b.top() < from.bottom())
                                       : (b.right() > from.left() && b.left() < from.right());
        const double ahead = horizontal ? (side == 1 ? b.left() - from.right() : from.left() - b.right())
                                        : (side == 2 ? b.top() - from.bottom() : from.top() - b.bottom());
        if (inBand && ahead >= 0 && ahead < best) {
            best = ahead;
            target = s.id;
        }
    }
    if (target.isEmpty()) {
        // 2. A new shape beyond the source, stepped on past anything in the way.
        Shape shape;
        const Stencil *stencil = Diagram::stencil(stencilId);
        if (stencil != nullptr) {
            target = addShape(drawing, page, stencilId, from.center(), QString());
        } else {
            // Like the source (Visio repeats the shape it starts from), without
            // its text, data, links or group.
            shape = *source;
            shape.id = drawing.newId(QStringLiteral("s"));
            shape.text.clear();
            shape.data.clear();
            shape.links.clear();
            shape.dataLink = {};
            shape.groupId.clear();
            shape.locked = false;
            shape.extra = {};
            shape.layers = activeLayers(page);
            page.shapes.append(shape);
            target = shape.id;
        }
        Shape *added = page.shape(target);
        if (added == nullptr) {
            return {};
        }
        const QSizeF size = shapeBounds(*added).size();
        const double step = (horizontal ? (from.width() + size.width()) : (from.height() + size.height())) / 2 + gap;
        QPointF centre = from.center() + dir * step;
        for (int tries = 0; tries < 24; ++tries) {
            const QRectF place(centre - QPointF(size.width() / 2, size.height() / 2), size);
            const QRectF clearance = place.adjusted(-gap / 4, -gap / 4, gap / 4, gap / 4);
            const auto hit = std::find_if(page.shapes.cbegin(), page.shapes.cend(), [&](const Shape &s) {
                return s.id != target && s.id != fromId && blocking(s) && layersVisible(page, s.layers)
                    && shapeBounds(s).intersects(clearance);
            });
            if (hit == page.shapes.cend()) {
                break;
            }
            const QRectF b = shapeBounds(*hit);
            centre += dir * ((horizontal ? b.width() : b.height()) + gap);
        }
        added->rect.moveCenter(centre - (shapeBounds(*added).center() - added->rect.center()));
    }
    const Shape *to = page.shape(target);
    const Endpoint start{fromId, portFacing(*page.shape(fromId), side), {}};
    const Endpoint end{target, portFacing(*to, (side + 2) % 4), {}};
    const QString connector = addConnector(drawing, page, start, end, Routing::Orthogonal);
    if (connectorId != nullptr) {
        *connectorId = connector;
    }
    return connector.isEmpty() ? QString() : target;
}

} // namespace QindaOffice::Diagram::Ops
