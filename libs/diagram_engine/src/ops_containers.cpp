// SPDX-License-Identifier: GPL-3.0-or-later
// Ops (M87, D-095): containers as Visio handles them (ops.h). Membership
// stays geometric (D-050: a shape lying wholly inside a container and above
// it is a member); M87 adds Visio's behaviour on top — containers that
// expand around (or fit) their members, Delete that takes the members
// along, Disband, Fit to Contents and wrapping the selection — and `settle`,
// which applies it (and the pools' order, ops_lanes.cpp) after every edit.
//
// AGENT-NOTE (why `settle` compares with the page before the edit): which
// shapes a container must grow around is decided by what was inside it
// BEFORE the edit (its members) plus shapes new to the page dropped in it;
// geometry after the edit alone cannot tell a member dragged half out (the
// container expands) from a stranger lying across the edge (it does not).
#include "ops.h"
#include "ops_pool_p.h"

#include "geometry.h"
#include "layers.h"
#include "stencils.h"

#include <algorithm>
#include <cmath>

namespace QindaOffice::Diagram::Ops {

using namespace Pool;

namespace {

constexpr double kDefaultMargin = 12; // an eighth of an inch

// Step 2 of settle for one container: grows it around (or fits it to) the
// members it had and the shapes dropped in it, when the edit touched it.
bool settleContainer(Page &page, const Page &before, const QString &id)
{
    const Shape *was = before.shape(id);
    QStringList members = was != nullptr ? containerMembers(before, id) : QStringList();
    const int containerAt = page.shapeIndex(id);
    const bool lane = isLane(page.shapes.at(containerAt));
    for (int i = containerAt + 1; i < page.shapes.size(); ++i) {
        const Shape &s = page.shapes.at(i);
        // Dropped onto the page in this edit. A phase lies across its pool's
        // lanes and a lane beside them: neither is dropped INTO a lane.
        if (before.shape(s.id) == nullptr && !members.contains(s.id) && !isPhase(s) && !(lane && isLane(s))) {
            members.append(s.id);
        }
    }
    // Only a container the edit touched — moved, resized, or one whose
    // members moved, appeared or went — reacts: an unrelated edit never
    // reshapes a container.
    const Shape &now = page.shapes.at(containerAt);
    bool touched = was == nullptr || was->rect != now.rect;
    for (const QString &m : std::as_const(members)) {
        const Shape *s = page.shape(m);
        const Shape *then = before.shape(m);
        touched = touched || s == nullptr || then == nullptr || s->rect != then->rect || s->rotation != then->rotation;
    }
    if (!touched) {
        return false;
    }
    QRectF content;
    for (const QString &m : std::as_const(members)) {
        const Shape *s = page.shape(m);
        if (s == nullptr || !now.rect.contains(shapeBounds(*s).center()) || !layersVisible(page, s->layers)) {
            continue; // gone, hidden, or taken out of the container
        }
        content = content.isNull() ? shapeBounds(*s) : content | shapeBounds(*s);
    }
    if (content.isNull()) {
        return false;
    }
    const QRectF needed = holding(now, content);
    // Lanes only expand: their pool decides their length.
    const bool fit = effectiveResize(now) == ContainerResize::Fit && !isLane(now);
    const QRectF next = fit ? needed : (now.rect | needed);
    if (next == now.rect) {
        return false;
    }
    const Page snapshot = page; // before writing (ops_pool_p.h AGENT-GUARD)
    page.shapes[containerAt].rect = next;
    if (isLane(page.shapes.at(containerAt))) {
        syncPool(page, snapshot, poolOf(snapshot, id), id);
    }
    return true;
}

} // namespace

ContainerResize effectiveResize(const Shape &container)
{
    if (container.containerResize != ContainerResize::Default) {
        return container.containerResize;
    }
    return isPhase(container) ? ContainerResize::None : ContainerResize::Expand;
}

double containerMargin(const Shape &container)
{
    return container.containerMargin >= 0 ? container.containerMargin : kDefaultMargin;
}

QString wrapInContainer(Drawing &drawing, Page &page, const QStringList &ids, const QString &type)
{
    QRectF bounds;
    int firstContainer = -1;
    for (int i = 0; i < page.shapes.size(); ++i) {
        const Shape &s = page.shapes.at(i);
        if (ids.contains(s.id) && !s.locked) {
            bounds = bounds.isNull() ? shapeBounds(s) : bounds | shapeBounds(s);
            if (isContainer(s) && firstContainer < 0) {
                firstContainer = i;
            }
        }
    }
    if (bounds.isNull()) {
        return {};
    }
    const QString id = addShape(drawing, page, type, bounds.center());
    const int at = page.shapeIndex(id);
    if (at < 0) {
        return {};
    }
    Shape shape = page.shapes.takeAt(at);
    shape.rect = holding(shape, bounds);
    // Below everything it holds, containers included, so all are members.
    page.shapes.insert(firstContainer >= 0 ? std::min(firstContainer, at) : at, shape);
    return id;
}

bool fitToContents(Page &page, const QStringList &containerIds)
{
    bool changed = false;
    for (const QString &id : containerIds) {
        const Shape *c = page.shape(id);
        if (c == nullptr || !isContainer(*c) || c->locked) {
            continue;
        }
        QRectF content;
        for (const QString &m : containerMembers(page, id)) {
            const QRectF r = shapeBounds(*page.shape(m));
            content = content.isNull() ? r : content | r;
        }
        if (content.isNull()) {
            continue;
        }
        QRectF next = holding(*c, content);
        if (isLane(*c)) {
            // A lane keeps its pool's length; only its depth fits.
            const Qt::Orientation o = laneOrientation(*c);
            next = makeRect(o, alongStart(c->rect, o), alongLength(c->rect, o), acrossStart(next, o), acrossLength(next, o));
        }
        if (next == c->rect) {
            continue;
        }
        const Page before = page; // before writing (ops_pool_p.h AGENT-GUARD)
        page.shape(id)->rect = next;
        if (isLane(*page.shape(id))) {
            syncPool(page, before, poolOf(before, id), id);
        }
        changed = true;
    }
    return changed;
}

bool disband(Page &page, const QStringList &containerIds)
{
    QStringList targets;
    for (const QString &id : containerIds) {
        const Shape *s = page.shape(id);
        if (s != nullptr && isContainer(*s) && !s->locked) {
            targets.append(id);
        }
    }
    return removeItems(page, targets) > 0;
}

bool setContainerResize(Page &page, const QStringList &containerIds, ContainerResize resize)
{
    bool changed = false;
    for (Shape &s : page.shapes) {
        if (containerIds.contains(s.id) && isContainer(s) && s.containerResize != resize) {
            s.containerResize = resize;
            changed = true;
        }
    }
    return changed;
}

bool setContainerMargin(Page &page, const QStringList &containerIds, double margin)
{
    if (!std::isfinite(margin)) {
        return false;
    }
    const double m = std::clamp(margin, 0.0, 480.0);
    bool changed = false;
    for (Shape &s : page.shapes) {
        if (containerIds.contains(s.id) && isContainer(s) && std::abs(containerMargin(s) - m) > 1e-9) {
            s.containerMargin = m;
            changed = true;
        }
    }
    return changed;
}

int deleteItems(Page &page, const QStringList &ids)
{
    QSet<QString> targets(ids.cbegin(), ids.cend());
    // Pools lose the lanes being deleted; what follows closes the gap.
    struct Shift {
        QSet<QString> shapes;
        QPointF delta;
    };
    QList<Shift> shifts;
    QSet<QString> poolsSeen;
    // Pools as they were (their phases follow them once the lanes go).
    QList<QPair<QStringList, Qt::Orientation>> pools;
    for (const QString &id : ids) {
        const Shape *s = page.shape(id);
        if (s == nullptr || s->locked || !isContainer(*s)) {
            continue;
        }
        targets.unite(membersDeep(page, id));
        if (!isLane(*s) || poolsSeen.contains(id)) {
            continue;
        }
        const QStringList pool = poolOf(page, id);
        for (const QString &lane : pool) {
            poolsSeen.insert(lane);
        }
        const Qt::Orientation o = laneOrientation(*s);
        pools.append({pool, o});
        double gap = 0;
        for (const QString &lane : pool) {
            if (ids.contains(lane)) {
                gap += acrossLength(page.shape(lane)->rect, o);
            } else if (gap > 0) {
                QSet<QString> moving = membersDeep(page, lane);
                moving.insert(lane);
                shifts.append({moving, acrossDelta(o, -gap)});
            }
        }
    }
    // A connector between two shapes that both go, goes with them (with one
    // end left it stays, the other end freed where it was: D-050).
    for (const Connector &c : std::as_const(page.connectors)) {
        if (c.from.glued() && c.to.glued() && targets.contains(c.from.shapeId) && targets.contains(c.to.shapeId)) {
            targets.insert(c.id);
        }
    }
    QList<QRectF> oldBounds;
    for (const auto &[pool, o] : std::as_const(pools)) {
        oldBounds.append(poolBounds(page, pool));
    }
    const int removed = removeItems(page, QStringList(targets.cbegin(), targets.cend()));
    for (const Shift &shift : std::as_const(shifts)) {
        translate(page, shift.shapes, shift.delta);
    }
    for (int i = 0; i < pools.size(); ++i) {
        stretchPhases(page, pools.at(i).second, oldBounds.at(i), poolBounds(page, pools.at(i).first));
    }
    return removed;
}

bool settle(Page &page, const Page &before)
{
    bool changed = false;
    // 1. Lanes the edit resized or dragged: their pools (as they were) put
    // themselves back in order.
    QSet<QString> pooled;
    for (const Shape &s : before.shapes) {
        const Shape *now = page.shape(s.id);
        if (!isLane(s) || now == nullptr || now->rect == s.rect || pooled.contains(s.id)) {
            continue;
        }
        const QStringList pool = poolOf(before, s.id);
        if (pool.size() < 2) {
            continue; // a lane on its own is just a container
        }
        for (const QString &id : pool) {
            pooled.insert(id);
        }
        // A pool still whole — moved as one, grown by an inserted lane,
        // closed up after a deleted one (insertLane and deleteItems keep it
        // stacked), its outer lane stretched outwards — is already in order;
        // only a lane that left the stack (resized into or away from its
        // neighbours, dragged, made longer) sends it back in order.
        const QStringList nowPool = poolOf(page, s.id);
        const bool whole = std::all_of(pool.cbegin(), pool.cend(), [&](const QString &id) {
            return page.shape(id) == nullptr || nowPool.contains(id);
        });
        if (!whole) {
            changed = syncPool(page, before, pool, s.id) || changed;
        }
    }
    // 2. Containers around their members, innermost first.
    QStringList order;
    for (const Shape &c : std::as_const(page.shapes)) {
        if (isContainer(c) && !c.locked && std::abs(c.rotation) < 1e-6 && effectiveResize(c) != ContainerResize::None) {
            order.append(c.id);
        }
    }
    std::stable_sort(order.begin(), order.end(), [&](const QString &a, const QString &b) {
        const QRectF ra = page.shape(a)->rect;
        const QRectF rb = page.shape(b)->rect;
        return ra.width() * ra.height() < rb.width() * rb.height();
    });
    for (const QString &id : std::as_const(order)) {
        changed = settleContainer(page, before, id) || changed;
    }
    return changed;
}

} // namespace QindaOffice::Diagram::Ops
