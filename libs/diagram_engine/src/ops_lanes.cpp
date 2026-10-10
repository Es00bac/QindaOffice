// SPDX-License-Identifier: GPL-3.0-or-later
// Ops (M87, D-095): cross-functional swimlanes — POOLS, lanes of one
// orientation and one length stacked edge to edge, which stay stacked and
// equally long whatever is inserted, deleted, resized or dragged among
// them (Visio's Cross-Functional Flowchart), and phases across them
// (ops.h; the shared geometry is ops_pool_p.h).
//
// AGENT-NOTE: a pool is found, not stored — lanes that touch and share
// their span form one (poolOf). That keeps M50 drawings, Visio's own
// swimlanes (structure "lane") and hand-placed lanes working alike, and
// nothing can go stale. Membership stays geometric (D-050).
#include "ops.h"
#include "ops_pool_p.h"

#include "geometry.h"
#include "stencils.h"

#include <algorithm>
#include <cmath>

namespace QindaOffice::Diagram::Ops {

using namespace Pool;

namespace {

// Stacks `pool` (ids in order) edge to edge from `start`, every lane
// `along`/`length` long, moving each lane's members (`members`) with it.
bool restack(Page &page, const QStringList &pool, Qt::Orientation o, double start, double along, double length,
             const QHash<QString, QSet<QString>> &members)
{
    bool changed = false;
    double cursor = start;
    for (const QString &id : pool) {
        Shape *lane = page.shape(id);
        if (lane == nullptr) {
            continue;
        }
        const double thickness = acrossLength(lane->rect, o);
        const double shift = cursor - acrossStart(lane->rect, o);
        const QRectF next = makeRect(o, along, length, cursor, thickness);
        if (next != lane->rect) {
            lane->rect = next;
            changed = true;
        }
        if (std::abs(shift) > 1e-9) {
            translate(page, members.value(id), acrossDelta(o, shift));
            changed = true;
        }
        cursor += thickness;
    }
    return changed;
}

} // namespace

namespace Pool {

bool isPhase(const Shape &shape)
{
    return shape.type == QLatin1String("phase") || shape.structure == QLatin1String("phase");
}

void translate(Page &page, const QSet<QString> &ids, QPointF delta)
{
    if (delta.isNull() || ids.isEmpty()) {
        return;
    }
    for (Shape &s : page.shapes) {
        if (ids.contains(s.id)) {
            s.rect.translate(delta);
        }
    }
    for (Connector &c : page.connectors) {
        const bool fromMoves = c.from.glued() && ids.contains(c.from.shapeId);
        const bool toMoves = c.to.glued() && ids.contains(c.to.shapeId);
        if (!c.from.glued() && toMoves) {
            c.from.point += delta;
        }
        if (!c.to.glued() && fromMoves) {
            c.to.point += delta;
        }
    }
}

QSet<QString> membersDeep(const Page &page, const QString &id)
{
    QSet<QString> out;
    QStringList queue{id};
    while (!queue.isEmpty()) {
        const QString next = queue.takeFirst();
        for (const QString &m : containerMembers(page, next)) {
            if (!out.contains(m) && m != id) {
                out.insert(m);
                queue.append(m);
            }
        }
    }
    return out;
}

QMarginsF headerOf(const Shape &c)
{
    const QRectF h = headerRect(c);
    if (h.isEmpty()) {
        return {};
    }
    if (c.type == QLatin1String("laneHorizontal")) {
        return {h.width(), 0, 0, 0};
    }
    return {0, h.height(), 0, 0};
}

QRectF holding(const Shape &c, const QRectF &content)
{
    const double m = containerMargin(c);
    const QMarginsF header = headerOf(c);
    return content.adjusted(-m - header.left(), -m - header.top(), m + header.right(), m + header.bottom());
}

QRectF poolBounds(const Page &page, const QStringList &pool)
{
    QRectF bounds;
    for (const QString &id : pool) {
        if (const Shape *s = page.shape(id)) {
            bounds = bounds.isNull() ? s->rect : bounds | s->rect;
        }
    }
    return bounds;
}

bool stretchPhases(Page &page, Qt::Orientation o, const QRectF &before, const QRectF &after)
{
    if (before.isNull() || after.isNull() || before == after) {
        return false;
    }
    bool changed = false;
    for (Shape &s : page.shapes) {
        if (!isPhase(s) || std::abs(s.rotation) > 1e-6) {
            continue;
        }
        if (near(acrossStart(s.rect, o), acrossStart(before, o))
            && near(acrossStart(s.rect, o) + acrossLength(s.rect, o), acrossStart(before, o) + acrossLength(before, o))) {
            const QRectF next = makeRect(o, alongStart(s.rect, o), alongLength(s.rect, o), acrossStart(after, o),
                                         acrossLength(after, o));
            changed = changed || next != s.rect;
            s.rect = next;
        }
    }
    return changed;
}

bool syncPool(Page &page, const Page &before, const QStringList &pool, const QString &edited)
{
    const Shape *lane = page.shape(edited);
    const Shape *was = before.shape(edited);
    if (lane == nullptr || was == nullptr || pool.isEmpty() || before.shape(pool.first()) == nullptr) {
        return false;
    }
    const Qt::Orientation o = laneOrientation(*lane);
    // Members as they were: the other lanes' are still there; the edited
    // lane's moved with it (Ops::moveItems) or stayed (a resize).
    QHash<QString, QSet<QString>> members;
    for (const QString &id : pool) {
        members.insert(id, membersDeep(before, id));
    }
    const bool resized = !near(acrossLength(lane->rect, o), acrossLength(was->rect, o))
                      || !near(alongLength(lane->rect, o), alongLength(was->rect, o));
    QStringList order = pool;
    if (!resized && order.size() > 1) {
        // Dragged along the stack: its centre picks its slot.
        order.removeAll(edited);
        const double centre = acrossStart(lane->rect, o) + acrossLength(lane->rect, o) / 2;
        qsizetype slot = 0;
        while (slot < order.size()) {
            const Shape *other = page.shape(order.at(slot));
            if (other == nullptr || centre < acrossStart(other->rect, o) + acrossLength(other->rect, o) / 2) {
                break;
            }
            ++slot;
        }
        order.insert(slot, edited);
    }
    double start = acrossStart(before.shape(pool.first())->rect, o);
    if (edited == pool.first() && resized) {
        start = acrossStart(lane->rect, o);
    }
    const double along = resized ? alongStart(lane->rect, o) : alongStart(was->rect, o);
    const double length = resized ? alongLength(lane->rect, o) : alongLength(was->rect, o);
    const QRectF oldBounds = poolBounds(before, pool);
    bool changed = restack(page, order, o, start, along, length, members);
    changed = stretchPhases(page, o, oldBounds, poolBounds(page, order)) || changed;
    return changed;
}

} // namespace Pool

bool isLane(const Shape &shape)
{
    return shape.type == QLatin1String("laneHorizontal") || shape.type == QLatin1String("laneVertical")
        || shape.structure == QLatin1String("lane");
}

Qt::Orientation laneOrientation(const Shape &lane)
{
    if (lane.type == QLatin1String("laneHorizontal")) {
        return Qt::Horizontal;
    }
    if (lane.type == QLatin1String("laneVertical")) {
        return Qt::Vertical;
    }
    return lane.rect.width() >= lane.rect.height() ? Qt::Horizontal : Qt::Vertical;
}

QStringList poolOf(const Page &page, const QString &laneId)
{
    const Shape *lane = page.shape(laneId);
    if (lane == nullptr || !isLane(*lane)) {
        return {};
    }
    const Qt::Orientation o = laneOrientation(*lane);
    // Lanes that can join: same orientation, unrotated, same span along.
    QList<const Shape *> candidates;
    for (const Shape &s : page.shapes) {
        if (isLane(s) && laneOrientation(s) == o && std::abs(s.rotation) < 1e-6
            && near(alongStart(s.rect, o), alongStart(lane->rect, o))
            && near(alongLength(s.rect, o), alongLength(lane->rect, o))) {
            candidates.append(&s);
        }
    }
    QStringList pool{laneId};
    const auto extend = [&](bool forward) {
        for (bool grown = true; grown;) {
            grown = false;
            const QRectF edge = page.shape(forward ? pool.last() : pool.first())->rect;
            for (const Shape *c : std::as_const(candidates)) {
                if (pool.contains(c->id)) {
                    continue;
                }
                const bool touches = forward ? near(acrossStart(c->rect, o), acrossStart(edge, o) + acrossLength(edge, o))
                                             : near(acrossStart(c->rect, o) + acrossLength(c->rect, o), acrossStart(edge, o));
                if (touches) {
                    forward ? pool.append(c->id) : pool.prepend(c->id);
                    grown = true;
                    break;
                }
            }
        }
    };
    extend(true);
    extend(false);
    return pool;
}

QString insertLane(Drawing &drawing, Page &page, const QString &laneId, bool after, const QString &type, QPointF at)
{
    const Shape *reference = page.shape(laneId);
    if (reference == nullptr || !isLane(*reference)) {
        // No lane chosen: join the nearest pool of this kind of lane.
        if (Diagram::stencil(type) == nullptr) {
            return {};
        }
        QString best;
        double bestDistance = 1e18;
        for (const Shape &s : page.shapes) {
            if (s.type == type && isLane(s)) {
                const QStringList pool = poolOf(page, s.id);
                const QRectF b = poolBounds(page, pool);
                const double dx = std::max({b.left() - at.x(), 0.0, at.x() - b.right()});
                const double dy = std::max({b.top() - at.y(), 0.0, at.y() - b.bottom()});
                const double d = std::hypot(dx, dy);
                if (d < bestDistance) {
                    bestDistance = d;
                    best = pool.last();
                }
            }
        }
        return best.isEmpty() ? addShape(drawing, page, type, at) : insertLane(drawing, page, best, true, type, at);
    }
    const Qt::Orientation o = laneOrientation(*reference);
    const QRectF reference0 = reference->rect;
    const QStringList pool = poolOf(page, laneId);
    const QRectF oldBounds = poolBounds(page, pool);
    const double thickness = acrossLength(reference0, o);
    const qsizetype index = pool.indexOf(laneId);
    const double slot = after ? acrossStart(reference0, o) + thickness : acrossStart(reference0, o);
    // Lanes from the slot on make room, with their members — all found
    // before anything moves (a moved lane would cover the next one's).
    QSet<QString> moving;
    for (qsizetype i = after ? index + 1 : index; i < pool.size(); ++i) {
        moving.unite(membersDeep(page, pool.at(i)));
        moving.insert(pool.at(i));
    }
    translate(page, moving, acrossDelta(o, thickness));
    Shape lane = *page.shape(laneId);
    lane.id = drawing.newId(QStringLiteral("s"));
    const Stencil *stencil = Diagram::stencil(lane.type);
    lane.text = stencil != nullptr ? stencil->defaultText : QString();
    lane.data.clear();
    lane.links.clear();
    lane.dataLink = {};
    lane.groupId.clear();
    lane.extra = {};
    lane.rect = makeRect(o, alongStart(reference0, o), alongLength(reference0, o), slot, thickness);
    page.shapes.insert(page.shapeIndex(laneId) + (after ? 1 : 0), lane);
    QStringList grown = pool;
    grown.insert(after ? index + 1 : index, lane.id);
    stretchPhases(page, o, oldBounds, poolBounds(page, grown));
    return lane.id;
}

QString insertPhase(Drawing &drawing, Page &page, QPointF at)
{
    // The pool under (or just beside) the point.
    QStringList pool;
    for (const Shape &s : page.shapes) {
        if (isLane(s)) {
            const QStringList candidate = poolOf(page, s.id);
            if (poolBounds(page, candidate).adjusted(-48, -48, 48, 48).contains(at)) {
                pool = candidate;
                break;
            }
        }
    }
    const QString id = addShape(drawing, page, QStringLiteral("phase"), at);
    Shape *phase = page.shape(id);
    if (phase == nullptr || pool.isEmpty()) {
        return id;
    }
    const Shape &first = *page.shape(pool.first());
    const Qt::Orientation o = laneOrientation(first);
    const QRectF b = poolBounds(page, pool);
    // Across the whole pool, clear of the lanes' header band.
    const QMarginsF header = headerOf(first);
    const double lead = o == Qt::Horizontal ? header.left() : header.top();
    const double length = o == Qt::Horizontal ? 240.0 : 200.0;
    const double minStart = alongStart(b, o) + lead;
    const double maxStart = std::max(minStart, alongStart(b, o) + alongLength(b, o) - length);
    const double centre = o == Qt::Horizontal ? at.x() : at.y();
    const double start = std::clamp(centre - length / 2, minStart, maxStart);
    page.shape(id)->rect = makeRect(o, start, length, acrossStart(b, o), acrossLength(b, o));
    return id;
}

} // namespace QindaOffice::Diagram::Ops
