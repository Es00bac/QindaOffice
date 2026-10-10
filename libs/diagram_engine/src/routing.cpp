// SPDX-License-Identifier: GPL-3.0-or-later
#include "routing.h"
#include "routing_p.h"

#include "geometry.h"
#include "stencils.h"

#include <QLineF>

#include <algorithm>
#include <cmath>
#include <limits>

namespace QindaOffice::Diagram {

namespace {

struct End {
    QPointF point;
    QPointF direction; // outward, unit; null when free
    QString shapeId;
};

QPointF unit(QPointF v)
{
    const double length = std::hypot(v.x(), v.y());
    return length > 1e-9 ? v / length : QPointF();
}

// Page point of the side midpoint `side` (0 top, 1 right, 2 bottom, 3 left).
QPointF sidePoint(const Shape &shape, int side)
{
    const double w = shape.rect.width();
    const double h = shape.rect.height();
    static const QPointF normalized[] = {{0.5, 0}, {1, 0.5}, {0.5, 1}, {0, 0.5}};
    const QPointF n = normalized[side];
    // Unflipped frame: the side a user sees stays that side.
    QTransform t;
    t.translate(shape.rect.center().x(), shape.rect.center().y());
    t.rotate(shape.rotation);
    t.translate(-w / 2, -h / 2);
    return t.map(QPointF(n.x() * w, n.y() * h));
}

QPointF sideDirection(const Shape &shape, int side)
{
    static const QPointF local[] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    QTransform t;
    t.rotate(shape.rotation);
    return unit(t.map(local[side]));
}

// The point a far end aims at: its shape's centre, or its free point.
QPointF aimOf(const Page &page, const Endpoint &end)
{
    if (end.glued()) {
        if (const Shape *shape = page.shape(end.shapeId)) {
            if (end.port >= 0 && end.port < portCount(*shape)) {
                return portPoint(*shape, end.port);
            }
            return shape->rect.center();
        }
    }
    return end.point;
}

End resolve(const Page &page, const Endpoint &end, QPointF aim, Routing routing)
{
    End out;
    out.point = end.point;
    const Shape *shape = end.glued() ? page.shape(end.shapeId) : nullptr;
    if (shape == nullptr) {
        return out;
    }
    out.shapeId = shape->id;
    if (end.port >= 0 && end.port < portCount(*shape)) {
        out.point = portPoint(*shape, end.port);
        out.direction = portDirection(*shape, end.port);
        if (out.direction.isNull()) {
            out.direction = unit(aim - out.point);
        }
        return out;
    }
    if (routing == Routing::Orthogonal) {
        const int side = facingSide(*shape, aim);
        out.point = sidePoint(*shape, side);
        out.direction = sideDirection(*shape, side);
        return out;
    }
    out.point = outlinePoint(*shape, aim);
    out.direction = unit(out.point - shape->rect.center());
    return out;
}

using RoutingDetail::segmentHitsRect;

bool obstacle(const Shape &shape)
{
    return !isContainer(shape) && shape.type != QLatin1String("text");
}

void simplify(QVector<QPointF> &points)
{
    QVector<QPointF> out;
    for (const QPointF &p : std::as_const(points)) {
        if (!out.isEmpty() && QLineF(out.last(), p).length() < 1e-6) {
            continue;
        }
        if (out.size() >= 2) {
            const QPointF a = out.at(out.size() - 2);
            const QPointF b = out.last();
            const double cross = (b.x() - a.x()) * (p.y() - b.y()) - (b.y() - a.y()) * (p.x() - b.x());
            const double dot = (b.x() - a.x()) * (p.x() - b.x()) + (b.y() - a.y()) * (p.y() - b.y());
            if (std::abs(cross) < 1e-6 && dot >= 0) {
                out.last() = p;
                continue;
            }
        }
        out.append(p);
    }
    points = out;
}

double cost(const QVector<QPointF> &path, const QVector<QRectF> &obstacles)
{
    double length = 0;
    int hits = 0;
    for (int i = 0; i + 1 < path.size(); ++i) {
        length += QLineF(path.at(i), path.at(i + 1)).length();
        // The stubs leave and enter the end shapes; everything between them
        // must stay clear of every shape, the end shapes included.
        if (i == 0 || i + 2 == path.size()) {
            continue;
        }
        for (const QRectF &r : obstacles) {
            if (segmentHitsRect(path.at(i), path.at(i + 1), r)) {
                ++hits;
            }
        }
    }
    return length + double(path.size()) * 12 + hits * 5000.0;
}

QVector<QPointF> orthogonal(const Page &page, const End &a, const End &b)
{
    QPointF da = a.direction;
    QPointF db = b.direction;
    const QPointF delta = b.point - a.point;
    // A free end leaves along the dominant axis towards the other end.
    const auto freeDirection = [](QPointF towards) {
        return std::abs(towards.x()) >= std::abs(towards.y()) ? QPointF(towards.x() >= 0 ? 1 : -1, 0)
                                                              : QPointF(0, towards.y() >= 0 ? 1 : -1);
    };
    if (da.isNull()) {
        da = freeDirection(delta);
    }
    if (db.isNull()) {
        db = freeDirection(-delta);
    }
    // Snap near-axis directions (rotated shapes route from their true
    // normal but bend on the page's axes).
    da = freeDirection(da);
    db = freeDirection(db);
    const QPointF a1 = a.point + da * kRouteStub;
    const QPointF b1 = b.point + db * kRouteStub;

    QVector<QRectF> obstacles;
    double top = std::min(a1.y(), b1.y());
    double bottom = std::max(a1.y(), b1.y());
    double left = std::min(a1.x(), b1.x());
    double right = std::max(a1.x(), b1.x());
    for (const Shape &shape : page.shapes) {
        if (!obstacle(shape)) {
            continue;
        }
        const QRectF bounds = shapeBounds(shape);
        obstacles.append(bounds.adjusted(1, 1, -1, -1));
        if (shape.id == a.shapeId || shape.id == b.shapeId
            || bounds.intersects(QRectF(QPointF(left, top), QPointF(right, bottom)).normalized())) {
            top = std::min(top, bounds.top() - kRouteStub);
            bottom = std::max(bottom, bounds.bottom() + kRouteStub);
            left = std::min(left, bounds.left() - kRouteStub);
            right = std::max(right, bounds.right() + kRouteStub);
        }
    }
    const double mx = (a1.x() + b1.x()) / 2;
    const double my = (a1.y() + b1.y()) / 2;
    const QVector<QVector<QPointF>> middles = {
        {QPointF(b1.x(), a1.y())},
        {QPointF(a1.x(), b1.y())},
        {QPointF(mx, a1.y()), QPointF(mx, b1.y())},
        {QPointF(a1.x(), my), QPointF(b1.x(), my)},
        {QPointF(a1.x(), top), QPointF(b1.x(), top)},
        {QPointF(a1.x(), bottom), QPointF(b1.x(), bottom)},
        {QPointF(left, a1.y()), QPointF(left, b1.y())},
        {QPointF(right, a1.y()), QPointF(right, b1.y())},
    };
    QVector<QPointF> best;
    double bestCost = std::numeric_limits<double>::infinity();
    for (const QVector<QPointF> &middle : middles) {
        QVector<QPointF> path{a.point, a1};
        path += middle;
        path << b1 << b.point;
        // Keep it Manhattan: a candidate whose corners do not line up is
        // skipped (the Z/U shapes above always do).
        bool manhattan = true;
        for (int i = 1; i + 2 < path.size() && manhattan; ++i) {
            const QPointF p = path.at(i);
            const QPointF q = path.at(i + 1);
            manhattan = std::abs(p.x() - q.x()) < 1e-6 || std::abs(p.y() - q.y()) < 1e-6;
        }
        if (!manhattan) {
            continue;
        }
        // Never double back into the end's own stub.
        const QPointF firstTurn = path.at(2) - a1;
        const QPointF lastTurn = b1 - path.at(path.size() - 3);
        if (QPointF::dotProduct(firstTurn, da) < -1e-6 || QPointF::dotProduct(lastTurn, -db) < -1e-6) {
            continue;
        }
        simplify(path);
        const double c = cost(path, obstacles);
        if (c < bestCost) {
            bestCost = c;
            best = path;
        }
    }
    // M87: every quick candidate crosses a shape — search the grid round
    // them for a clear way (cost() counts a crossing as 5000).
    if (bestCost >= 5000) {
        const QVector<QPointF> found = RoutingDetail::searchOrthogonal(a1, da, b1, db, obstacles, kRouteStub / 2, 12);
        if (!found.isEmpty()) {
            QVector<QPointF> path{a.point};
            path += found;
            path.append(b.point);
            simplify(path);
            const double c = cost(path, obstacles);
            if (c < bestCost) {
                bestCost = c;
                best = path;
            }
        }
    }
    if (best.isEmpty()) {
        best = {a.point, a1, QPointF(a1.x(), b1.y()), b1, b.point};
        simplify(best);
    }
    return best;
}

QPointF pointAlong(const QVector<QPointF> &polyline, double fraction)
{
    double total = 0;
    for (int i = 0; i + 1 < polyline.size(); ++i) {
        total += QLineF(polyline.at(i), polyline.at(i + 1)).length();
    }
    double remaining = total * std::clamp(fraction, 0.0, 1.0);
    for (int i = 0; i + 1 < polyline.size(); ++i) {
        const QLineF segment(polyline.at(i), polyline.at(i + 1));
        if (remaining <= segment.length() || i + 2 == polyline.size()) {
            return segment.length() > 0 ? segment.pointAt(std::min(1.0, remaining / segment.length())) : segment.p1();
        }
        remaining -= segment.length();
    }
    return polyline.isEmpty() ? QPointF() : polyline.first();
}

} // namespace

Route routeConnector(const Page &page, const Connector &connector)
{
    Route route;
    const QPointF aimA = aimOf(page, connector.to);
    const QPointF aimB = aimOf(page, connector.from);
    const End a = resolve(page, connector.from, aimA, connector.routing);
    const End b = resolve(page, connector.to, aimB, connector.routing);
    switch (connector.routing) {
    case Routing::Straight:
        route.points = {a.point, b.point};
        break;
    case Routing::Curved: {
        const double span = QLineF(a.point, b.point).length();
        const double reach = std::max(30.0, span * 0.4);
        const QPointF da = a.direction.isNull() ? unit(b.point - a.point) : a.direction;
        const QPointF db = b.direction.isNull() ? unit(a.point - b.point) : b.direction;
        route.points = {a.point, a.point + da * reach, b.point + db * reach, b.point};
        route.curved = true;
        break;
    }
    case Routing::Orthogonal:
        route.points = orthogonal(page, a, b);
        break;
    }
    const QVector<QPointF> flat = routePolyline(route);
    route.labelPoint = pointAlong(flat, connector.labelPosition);
    if (flat.size() >= 2) {
        route.startDirection = unit(flat.at(0) - flat.at(1));
        route.endDirection = unit(flat.last() - flat.at(flat.size() - 2));
    }
    return route;
}

QVector<QPointF> routePolyline(const Route &route)
{
    if (!route.curved || route.points.size() != 4) {
        return route.points;
    }
    QPainterPath path(route.points.at(0));
    path.cubicTo(route.points.at(1), route.points.at(2), route.points.at(3));
    QVector<QPointF> out;
    constexpr int steps = 32;
    for (int i = 0; i <= steps; ++i) {
        out.append(path.pointAtPercent(double(i) / steps));
    }
    return out;
}

QHash<QString, Route> routePage(const Page &page)
{
    QHash<QString, Route> routes;
    for (const Connector &connector : page.connectors) {
        Route route = routeConnector(page, connector);
        route.jumpStyle = page.jumpStyle;
        routes.insert(connector.id, route);
    }
    if (!page.lineJumps) {
        return routes;
    }
    // Which of two crossing segments hops (M87: Visio's "add line jumps
    // to"): the later connector's, or the horizontal or vertical one —
    // which, for a pair that is not one of each, falls back to the later.
    const auto horizontal = [](const QLineF &l) { return std::abs(l.dy()) < 1e-6 && std::abs(l.dx()) > 1e-6; };
    const auto vertical = [](const QLineF &l) { return std::abs(l.dx()) < 1e-6 && std::abs(l.dy()) > 1e-6; };
    const auto hops = [&](const QLineF &mine, int i, const QLineF &theirs, int j) {
        if (page.jumpOn == JumpOn::Horizontal && horizontal(mine) != horizontal(theirs)) {
            return horizontal(mine);
        }
        if (page.jumpOn == JumpOn::Vertical && vertical(mine) != vertical(theirs)) {
            return vertical(mine);
        }
        return j < i;
    };
    QHash<QString, QVector<Jump>> found;
    for (int i = 0; i < page.connectors.size(); ++i) {
        const Route &route = routes[page.connectors.at(i).id];
        if (route.curved) {
            continue;
        }
        for (int s = 0; s + 1 < route.points.size(); ++s) {
            const QLineF segment(route.points.at(s), route.points.at(s + 1));
            QVector<double> distances;
            for (int j = 0; j < page.connectors.size(); ++j) {
                const Route &other = routes.value(page.connectors.at(j).id);
                if (j == i || other.curved) {
                    continue;
                }
                for (int t = 0; t + 1 < other.points.size(); ++t) {
                    QPointF hit;
                    const QLineF line(other.points.at(t), other.points.at(t + 1));
                    if (segment.intersects(line, &hit) != QLineF::BoundedIntersection || !hops(segment, i, line, j)) {
                        continue;
                    }
                    const double d = QLineF(segment.p1(), hit).length();
                    // Not at a corner or an end: only true crossings hop.
                    if (d > kJumpRadius * 1.5 && d < segment.length() - kJumpRadius * 1.5
                        && QLineF(line.p1(), hit).length() > 1 && QLineF(line.p2(), hit).length() > 1) {
                        distances.append(d);
                    }
                }
            }
            std::sort(distances.begin(), distances.end());
            double last = -1e9;
            for (double d : std::as_const(distances)) {
                if (d - last > kJumpRadius * 2.5) {
                    found[page.connectors.at(i).id].append({s, d});
                    last = d;
                }
            }
        }
    }
    for (auto it = found.cbegin(); it != found.cend(); ++it) {
        routes[it.key()].jumps = it.value();
    }
    return routes;
}

QPainterPath routePath(const Route &route, double trimStart, double trimEnd)
{
    QPainterPath path;
    if (route.points.size() < 2) {
        return path;
    }
    QVector<QPointF> points = route.points;
    // Pull the ends in along their end segments (never past the next point).
    const auto trim = [](QPointF end, QPointF next, double amount) {
        QLineF line(end, next);
        if (amount <= 0 || line.length() < 1e-9) {
            return end;
        }
        return line.pointAt(std::min(amount / line.length(), 0.9));
    };
    points.first() = trim(points.first(), points.at(1), trimStart);
    points.last() = trim(points.last(), points.at(points.size() - 2), trimEnd);
    path.moveTo(points.first());
    if (route.curved && points.size() == 4) {
        path.cubicTo(points.at(1), points.at(2), points.at(3));
        return path;
    }
    for (int s = 0; s + 1 < points.size(); ++s) {
        const QLineF segment(route.points.at(s), route.points.at(s + 1));
        const QPointF along = segment.length() > 0 ? (segment.p2() - segment.p1()) / segment.length() : QPointF();
        // Hops bulge to the segment's left-hand normal (up on a rightward line).
        const QPointF normal(along.y(), -along.x());
        for (const Jump &jump : route.jumps) {
            if (jump.segment != s) {
                continue;
            }
            const QPointF c = segment.p1() + along * jump.distance;
            const QPointF in = c - along * kJumpRadius;
            const QPointF out = c + along * kJumpRadius;
            path.lineTo(in);
            switch (route.jumpStyle) {
            case JumpStyle::Arc: {
                const QPointF lift = normal * (kJumpRadius * 1.33);
                path.cubicTo(in + lift, out + lift, out);
                break;
            }
            case JumpStyle::Gap:
                path.moveTo(out); // M87: the line breaks where it crosses
                break;
            case JumpStyle::Square: {
                const QPointF lift = normal * kJumpRadius;
                path.lineTo(in + lift);
                path.lineTo(out + lift);
                path.lineTo(out);
                break;
            }
            }
        }
        path.lineTo(points.at(s + 1));
    }
    return path;
}

} // namespace QindaOffice::Diagram
