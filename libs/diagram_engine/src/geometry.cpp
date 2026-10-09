// SPDX-License-Identifier: GPL-3.0-or-later
#include "geometry.h"

#include "shape_path.h"
#include "stencils.h"

#include <QLineF>

#include <algorithm>
#include <cmath>
#include <limits>

namespace QindaOffice::Diagram {

QTransform shapeTransform(const Shape &shape)
{
    const QRectF &r = shape.rect;
    QTransform t;
    t.translate(r.center().x(), r.center().y());
    t.rotate(shape.rotation);
    t.scale(shape.flipX ? -1 : 1, shape.flipY ? -1 : 1);
    t.translate(-r.width() / 2, -r.height() / 2);
    return t;
}

QPolygonF shapeCorners(const Shape &shape)
{
    const QRectF local(0, 0, shape.rect.width(), shape.rect.height());
    const QTransform t = shapeTransform(shape);
    // Corners of the rotated box without the flip (a flip maps the box onto
    // itself), so TL stays the visual top-left before rotation.
    QTransform noFlip;
    noFlip.translate(shape.rect.center().x(), shape.rect.center().y());
    noFlip.rotate(shape.rotation);
    noFlip.translate(-shape.rect.width() / 2, -shape.rect.height() / 2);
    Q_UNUSED(t)
    return noFlip.map(QPolygonF(QVector<QPointF>{local.topLeft(), local.topRight(), local.bottomRight(),
                                                  local.bottomLeft()}));
}

QRectF shapeBounds(const Shape &shape)
{
    if (std::abs(std::remainder(shape.rotation, 360.0)) < 1e-9) {
        return shape.rect;
    }
    return shapeCorners(shape).boundingRect();
}

QPainterPath pageOutline(const Shape &shape)
{
    return shapeTransform(shape).map(outline(shape));
}

bool shapeContains(const Shape &shape, QPointF pagePoint)
{
    const QTransform inverse = shapeTransform(shape).inverted();
    const QPointF local = inverse.map(pagePoint);
    const QPainterPath path = outline(shape);
    if (path.contains(local)) {
        return true;
    }
    // Shapes without a painted outline (text, actor) pick on their box, and
    // so do custom outlines made only of lines (M65).
    const bool box = !paintsOutline(shape) || (!shape.paths.isEmpty() && !customOutlineFills(shape));
    return box && QRectF(0, 0, shape.rect.width(), shape.rect.height()).contains(local);
}

int portCount(const Shape &shape)
{
    return int(portsOf(shape).size());
}

QPointF portPoint(const Shape &shape, int port)
{
    const QVector<QPointF> ports = portsOf(shape);
    if (port < 0 || port >= ports.size()) {
        return shape.rect.center();
    }
    const QPointF n = ports.at(port);
    return shapeTransform(shape).map(QPointF(n.x() * shape.rect.width(), n.y() * shape.rect.height()));
}

QPointF portDirection(const Shape &shape, int port)
{
    const QVector<QPointF> ports = portsOf(shape);
    if (port < 0 || port >= ports.size()) {
        return {};
    }
    const QPointF n = ports.at(port) - QPointF(0.5, 0.5);
    QPointF local;
    if (std::abs(n.x()) >= std::abs(n.y()) && std::abs(n.x()) > 1e-9) {
        local = QPointF(n.x() > 0 ? 1 : -1, 0);
    } else if (std::abs(n.y()) > 1e-9) {
        local = QPointF(0, n.y() > 0 ? 1 : -1);
    }
    // Directions ignore translation; apply the linear part only.
    const QTransform t = shapeTransform(shape);
    const QPointF mapped(t.m11() * local.x() + t.m21() * local.y(), t.m12() * local.x() + t.m22() * local.y());
    const double length = std::hypot(mapped.x(), mapped.y());
    return length > 1e-9 ? mapped / length : QPointF();
}

int nearestPort(const Shape &shape, QPointF pagePoint, double maxDistance)
{
    int best = -1;
    double bestDistance = maxDistance;
    for (int i = 0; i < portCount(shape); ++i) {
        const double d = QLineF(portPoint(shape, i), pagePoint).length();
        if (d <= bestDistance) {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

QPointF outlinePoint(const Shape &shape, QPointF target)
{
    const QPointF centre = shape.rect.center();
    const QPolygonF outlinePolygon = pageOutline(shape).toFillPolygon();
    if (outlinePolygon.size() < 3 || QLineF(centre, target).length() < 1e-9) {
        return centre;
    }
    // The ray centre -> target, long enough to cross the whole outline.
    QLineF ray(centre, target);
    const double reach = std::hypot(shape.rect.width(), shape.rect.height()) * 2 + 1;
    ray.setLength(std::max(reach, ray.length()));
    QPointF best = centre;
    double bestDistance = -1;
    for (int i = 0; i < outlinePolygon.size(); ++i) {
        const QLineF edge(outlinePolygon.at(i), outlinePolygon.at((i + 1) % outlinePolygon.size()));
        QPointF hit;
        if (ray.intersects(edge, &hit) == QLineF::BoundedIntersection) {
            // The outermost crossing is the visible edge (a concave outline
            // such as a cloud may be crossed more than once).
            const double d = QLineF(centre, hit).length();
            if (d > bestDistance) {
                bestDistance = d;
                best = hit;
            }
        }
    }
    return best;
}

int facingSide(const Shape &shape, QPointF target)
{
    // Work in the shape's unrotated frame so a rotated shape still picks the
    // side that visually faces the target.
    const QTransform inverse = shapeTransform(shape).inverted();
    const QPointF local = inverse.map(target);
    const double w = std::max(1.0, shape.rect.width());
    const double h = std::max(1.0, shape.rect.height());
    const double dx = (local.x() - w / 2) / w;
    const double dy = (local.y() - h / 2) / h;
    if (std::abs(dx) > std::abs(dy)) {
        return dx > 0 ? 1 : 3;
    }
    return dy > 0 ? 2 : 0;
}

QPointF endpointPoint(const Page &page, const Endpoint &end, QPointF other)
{
    if (!end.glued()) {
        return end.point;
    }
    const Shape *shape = page.shape(end.shapeId);
    if (shape == nullptr) {
        return end.point;
    }
    if (end.port >= 0 && end.port < portCount(*shape)) {
        return portPoint(*shape, end.port);
    }
    return outlinePoint(*shape, other);
}

double distanceToPolyline(const QVector<QPointF> &polyline, QPointF point)
{
    double best = std::numeric_limits<double>::infinity();
    for (int i = 0; i + 1 < polyline.size(); ++i) {
        const QPointF a = polyline.at(i);
        const QPointF b = polyline.at(i + 1);
        const QPointF ab = b - a;
        const double len2 = QPointF::dotProduct(ab, ab);
        double t = len2 > 0 ? QPointF::dotProduct(point - a, ab) / len2 : 0;
        t = std::clamp(t, 0.0, 1.0);
        best = std::min(best, QLineF(a + ab * t, point).length());
    }
    if (polyline.size() == 1) {
        best = QLineF(polyline.first(), point).length();
    }
    return best;
}

} // namespace QindaOffice::Diagram
