// SPDX-License-Identifier: GPL-3.0-or-later
// One page's plan: what is written verbatim, moved, merged or fresh, and
// with which Visio IDs (visio_write_p.h; the writing is visio_write_emit.cpp).
#include "visio_write_cells.h"

#include "geometry.h"

#include <QJsonObject>

#include <algorithm>
#include <cmath>

namespace QindaOffice::Diagram::Visio {

namespace {

bool sameExceptPosition(Shape a, const Shape &b, QPointF delta)
{
    a.rect.translate(-delta);
    return a == b;
}

bool sameGeometry(const Shape &a, const Shape &b)
{
    return a.rect == b.rect && a.rotation == b.rotation && a.flipX == b.flipX && a.flipY == b.flipY;
}

void collectIds(const XmlElement &shape, QSet<int> &ids)
{
    ids.insert(intAttribute(shape, u"ID", -1));
    if (const XmlElement *nested = child(shape, u"Shapes")) {
        for (const XmlElement *s : children(*nested, u"Shape")) {
            collectIds(*s, ids);
        }
    }
}

} // namespace

PageWriter::PageWriter(WriteContext &ctx, const Page &page, const SourcePage *source, const QString &part)
    : m_ctx(ctx)
    , m_page(page)
    , m_source(source)
    , m_part(part)
{
    m_scale = source != nullptr ? source->scale : 1.0;
    const double k = kPx * m_scale;
    m_toPage = QTransform(1 / k, 0, 0, -1 / k, 0, page.size.height() / k);
    // A taller or shorter page moves everything relative to Visio's origin,
    // the bottom of the page: nothing top-level stays verbatim then.
    m_pageShifted = source != nullptr && std::abs(source->size.height() - page.size.height()) > 0.25;
}

const SourceItem *PageWriter::itemOf(const QJsonObject &extra) const
{
    if (m_ctx.source == nullptr) {
        return nullptr;
    }
    const QJsonObject vsdx = extra.value(QLatin1String("vsdx")).toObject();
    if (vsdx.value(QLatin1String("src")).toString() != m_ctx.source->token) {
        return nullptr;
    }
    const auto it = m_ctx.source->items.constFind(vsdx.value(QLatin1String("key")).toString());
    return it == m_ctx.source->items.constEnd() ? nullptr : &*it;
}

bool PageWriter::unchanged(const Shape &shape, const SourceItem &item) const
{
    return item.shape && shape == *item.shape && !m_portsNeeded.contains(shape.id);
}

bool PageWriter::unchanged(const Connector &c, const SourceItem &item) const
{
    if (!item.connector || !(c == *item.connector) || m_pageShifted) {
        return false;
    }
    for (const Endpoint *end : {&c.from, &c.to}) {
        if (!end->glued()) {
            continue;
        }
        const Shape *target = m_page.shape(end->shapeId);
        const SourceItem *targetItem = target != nullptr ? itemOf(target->extra) : nullptr;
        if (targetItem == nullptr || !targetItem->shape || !sameGeometry(*target, *targetItem->shape)) {
            return false; // the route Visio stored no longer meets the shape
        }
    }
    return true;
}

int PageWriter::freshId()
{
    while (m_reserved.contains(m_nextId)) {
        ++m_nextId;
    }
    m_reserved.insert(m_nextId);
    return m_nextId++;
}

void PageWriter::planGroups()
{
    QHash<QString, QStringList> members;
    QStringList order;
    const auto add = [&](const QString &id, const QString &groupId) {
        if (!groupId.isEmpty()) {
            order.append(groupId);
            members[groupId].append(id);
        }
    };
    for (const Shape &s : m_page.shapes) {
        add(s.id, s.groupId);
    }
    for (const Connector &c : m_page.connectors) {
        add(c.id, c.groupId);
    }
    order.removeDuplicates();
    QSet<QString> usedGroups;
    for (const QString &groupId : std::as_const(order)) {
        GroupPlan plan;
        plan.members = members.value(groupId);
        // Built from a source group when every member came from the same one
        // on this page (a rebuilt group keeps the source group's own cells,
        // and the Master its children's MasterShape refers to).
        QString key;
        bool common = m_source != nullptr;
        QList<const SourceItem *> items;
        for (const QString &id : std::as_const(plan.members)) {
            const Shape *s = m_page.shape(id);
            const SourceItem *item = itemOf(s != nullptr ? s->extra : m_page.connector(id)->extra);
            common = common && item != nullptr && !item->group.isEmpty() && item->page == m_source->id
                  && (key.isEmpty() || item->group == key);
            if (!common) {
                break;
            }
            key = item->group;
            items.append(item);
        }
        const auto group = common ? m_ctx.source->groups.constFind(key) : m_ctx.source->groups.constEnd();
        if (!common || group == m_ctx.source->groups.constEnd() || usedGroups.contains(key)) {
            m_groups.insert(groupId, plan);
            continue;
        }
        usedGroups.insert(key);
        plan.group = &*group;
        // Rigid: every member there, nothing else, unchanged but for one
        // common shift (connectors: unchanged, no shift).
        QPointF delta;
        bool first = true;
        bool rigid = true;
        QSet<QString> written;
        for (qsizetype i = 0; i < plan.members.size() && rigid; ++i) {
            const SourceItem *item = items.at(i);
            written.insert(item->key);
            const Shape *s = m_page.shape(plan.members.at(i));
            if (s == nullptr) {
                rigid = unchanged(*m_page.connector(plan.members.at(i)), *item);
                continue;
            }
            const QPointF d = item->shape ? s->rect.topLeft() - item->shape->rect.topLeft() : QPointF();
            if (first) {
                delta = d;
                first = false;
            }
            rigid = item->shape && sameExceptPosition(*s, *item->shape, delta) && !m_portsNeeded.contains(s->id);
        }
        const bool hasConnector = std::any_of(plan.members.cbegin(), plan.members.cend(),
                                              [&](const QString &id) { return m_page.connector(id) != nullptr; });
        rigid = rigid && written.size() == plan.members.size()
             && written == QSet<QString>(plan.group->members.cbegin(), plan.group->members.cend())
             && (!hasConnector || (delta.isNull() && !m_pageShifted));
        plan.id = plan.group->visioId;
        if (rigid) {
            plan.rigid = true;
            plan.moved = delta;
            for (const int id : plan.group->ids) {
                m_reserved.insert(id);
                m_keptIds.insert(id);
                m_verbatimIds.insert(id);
            }
            for (qsizetype i = 0; i < plan.members.size(); ++i) {
                m_usedKeys.insert(items.at(i)->key);
                Plan member;
                member.mode = Mode::Verbatim;
                member.item = items.at(i);
                member.id = items.at(i)->visioId;
                m_plans.insert(plan.members.at(i), member);
            }
        } else {
            m_reserved.insert(plan.id);
            m_keptIds.insert(plan.id);
        }
        m_groups.insert(groupId, plan);
    }
}

void PageWriter::planItem(const QString &id, const QJsonObject &extra, const QString &groupId, bool unchangedItem)
{
    if (m_plans.contains(id)) {
        return; // a member of a rigid group
    }
    Plan plan;
    plan.item = itemOf(extra);
    const SourceItem *item = plan.item;
    const GroupPlan *group = groupId.isEmpty() ? nullptr : &m_groups[groupId];
    const QString base = group != nullptr && group->group != nullptr
                             ? itemKey(group->group->page, group->group->visioId)
                             : QString();
    const bool home = item != nullptr && !item->group.isEmpty() && item->group == base;
    // AGENT-GUARD: a child naming only a MasterShape, or a group's own
    // sheet, means nothing outside its source group — written fresh.
    const bool homeless = item != nullptr && (item->masterChild || item->groupSheet) && !home;
    if (item == nullptr || homeless) {
        m_plans.insert(id, plan);
        return;
    }
    if (home && item->groupSheet) {
        // The group's own sheet is the rebuilt group's element itself.
        m_usedKeys.insert(item->key);
        plan.mode = Mode::Merged;
        plan.id = group->id;
        plan.frame = FrameChange{false, {}};
        m_plans.insert(id, plan);
        return;
    }
    const bool firstUse = !m_usedKeys.contains(item->key) && m_source != nullptr && item->page == m_source->id;
    m_usedKeys.insert(item->key);
    const bool topLevel = group == nullptr && item->group.isEmpty();
    plan.mode = firstUse && topLevel && unchangedItem && !m_pageShifted ? Mode::Verbatim : Mode::Merged;
    if (firstUse && !m_reserved.contains(item->visioId)) {
        plan.id = item->visioId;
        m_reserved.insert(plan.id);
        m_keptIds.insert(plan.id);
        if (plan.mode == Mode::Verbatim) {
            m_verbatimIds.insert(plan.id);
        }
    } else {
        plan.mode = Mode::Merged; // a copy: a new ID and name
    }
    if (topLevel && firstUse) {
        // Same parent (the page); its origin moves when the page height did.
        const double k = kPx * m_scale;
        plan.frame = FrameChange{false, QPointF(0, (m_page.size.height() - m_source->size.height()) / k)};
    }
    m_plans.insert(id, plan);
}

void PageWriter::plan()
{
    if (m_source != nullptr) {
        m_nextId = m_source->maxId + 1;
        for (auto it = m_source->unmodelled.cbegin(); it != m_source->unmodelled.cend(); ++it) {
            QSet<int> ids;
            collectIds(**it, ids);
            m_reserved.unite(ids);
            m_keptIds.unite(ids);
            m_verbatimIds.unite(ids);
        }
    }
    for (const Connector &c : m_page.connectors) {
        for (const Endpoint *end : {&c.from, &c.to}) {
            const Shape *s = end->glued() && end->port >= 0 ? m_page.shape(end->shapeId) : nullptr;
            if (s != nullptr && s->ports.isEmpty()) {
                m_portsNeeded.insert(s->id);
            }
        }
    }
    planGroups();
    for (const Shape &s : m_page.shapes) {
        const SourceItem *item = itemOf(s.extra);
        planItem(s.id, s.extra, s.groupId, item != nullptr && unchanged(s, *item));
    }
    for (const Connector &c : m_page.connectors) {
        const SourceItem *item = itemOf(c.extra);
        planItem(c.id, c.extra, c.groupId, item != nullptr && unchanged(c, *item));
    }
    for (const int id : std::as_const(m_reserved)) {
        m_nextId = std::max(m_nextId, id + 1); // fresh IDs above everything kept
    }
    for (auto it = m_groups.begin(); it != m_groups.end(); ++it) {
        if (it->id == 0) {
            it->id = freshId();
        }
    }
    for (const Shape &s : m_page.shapes) {
        Plan &p = m_plans[s.id];
        p.id = p.id == 0 ? freshId() : p.id;
        m_visioIds.insert(s.id, p.id);
    }
    for (const Connector &c : m_page.connectors) {
        Plan &p = m_plans[c.id];
        p.id = p.id == 0 ? freshId() : p.id;
        m_visioIds.insert(c.id, p.id);
    }
}

} // namespace QindaOffice::Diagram::Visio
