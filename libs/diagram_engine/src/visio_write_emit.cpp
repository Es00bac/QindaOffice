// SPDX-License-Identifier: GPL-3.0-or-later
// One page's <PageContents> as planned: the shapes in the model's order
// (source shapes the model does not hold at their old places), the groups,
// then the <Connects> (visio_write_p.h).
#include "visio_write_cells.h"

#include "geometry.h"
#include "routing.h"

#include <algorithm>

namespace QindaOffice::Diagram::Visio {

namespace {

QString baseNameOf(const XmlElement &element)
{
    QString name = element.attribute({}, u"NameU", element.attribute({}, u"Name", QStringLiteral("Shape")));
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot > 0 && name.mid(dot + 1).toInt() > 0) {
        name.truncate(dot); // "Process.12" → "Process"
    }
    return name;
}

QRectF boundsOf(const Page &page, const QStringList &ids)
{
    QRectF bounds;
    for (const QString &id : ids) {
        QRectF r;
        if (const Shape *s = page.shape(id)) {
            r = shapeBounds(*s);
        } else if (const Connector *c = page.connector(id)) {
            r = QPolygonF(routePolyline(routeConnector(page, *c))).boundingRect();
        }
        bounds = bounds.isNull() ? r : bounds | r;
    }
    return bounds.isNull() ? QRectF(0, 0, 1, 1) : bounds;
}

} // namespace

XmlElementPtr PageWriter::cloneSource(const SourceItem &item)
{
    XmlElementPtr element = item.element->clone();
    if (m_ctx.copier != nullptr) {
        m_ctx.copier->rewrite(*element, item.part, m_part);
    }
    return element;
}

const XmlElement *PageWriter::masterOf(const SourceItem &item) const
{
    return item.master >= 0 && m_ctx.source != nullptr ? masterShape(*m_ctx.source, item.master, item.masterShape)
                                                       : nullptr;
}

void PageWriter::noteMasters(const XmlElement &element)
{
    if (element.hasAttribute({}, u"Master")) {
        m_masters.insert(intAttribute(element, u"Master", -1));
    }
    if (const XmlElement *nested = child(element, u"Shapes")) {
        for (const XmlElement *s : children(*nested, u"Shape")) {
            noteMasters(*s);
        }
    }
}

GlueEnd PageWriter::glueOf(const Endpoint &end) const
{
    GlueEnd glue;
    if (!end.glued() || m_page.shape(end.shapeId) == nullptr) {
        return glue;
    }
    glue.sheet = m_visioIds.value(end.shapeId, 0);
    glue.port = glue.sheet > 0 ? end.port : -1;
    return glue;
}

XmlElementPtr PageWriter::writeItem(const QString &modelId, const QTransform &toParent)
{
    const Plan &p = m_plans[modelId];
    XmlElementPtr element;
    const bool renamed = p.item != nullptr && p.id != p.item->visioId;
    if (p.mode != Mode::Fresh) {
        element = cloneSource(*p.item);
        if (renamed) {
            element->setAttribute({}, QStringLiteral("ID"), QString::number(p.id));
            setShapeNames(*element, baseNameOf(*element), p.id);
        }
    }
    if (const Shape *shape = m_page.shape(modelId)) {
        if (p.mode == Mode::Fresh) {
            element = freshShape(m_ctx, *shape, p.id, toParent, m_part, true);
        } else if (p.mode == Mode::Merged) {
            updateShape(m_ctx, *element, *shape, *p.item->shape, toParent, p.frame, m_part,
                        m_portsNeeded.contains(modelId), masterOf(*p.item));
        }
    } else if (const Connector *c = m_page.connector(modelId)) {
        const Route route = routeConnector(m_page, *c);
        const GlueEnd begin = glueOf(c->from);
        const GlueEnd end = glueOf(c->to);
        if (p.mode == Mode::Fresh) {
            element = freshConnector(m_ctx, *c, p.id, route.points, route.curved, toParent, begin, end);
            m_generated.insert(p.id);
        } else if (p.mode == Mode::Merged) {
            const bool routeChanged = !unchanged(*c, *p.item) || p.frame.changed || renamed || !p.frame.shift.isNull();
            updateConnector(m_ctx, *element, *c, *p.item->connector, route.points, route.curved, toParent, begin, end,
                            routeChanged, masterOf(*p.item));
            m_generated.insert(p.id);
        }
    }
    noteMasters(*element);
    return element;
}

XmlElementPtr PageWriter::writeGroup(const QString &groupId)
{
    const GroupPlan &g = m_groups[groupId];
    const double k = kPx * m_scale;
    if (g.rigid) {
        XmlElementPtr element = g.group->element->clone();
        if (m_ctx.copier != nullptr) {
            m_ctx.copier->rewrite(*element, g.group->part, m_part);
        }
        const QPointF shift = QPointF(g.moved.x() / k, -g.moved.y() / k)
                            + QPointF(0, m_source != nullptr ? (m_page.size.height() - m_source->size.height()) / k : 0);
        if (!shift.isNull()) {
            // The pin may come from the group's master: read it merged.
            const XmlElement *master = nullptr;
            if (g.group->element->hasAttribute({}, u"Master")) {
                master = masterShape(*m_ctx.source, intAttribute(*g.group->element, u"Master"),
                                     intAttribute(*g.group->element, u"MasterShape"));
            }
            const Sheet base = master != nullptr ? Sheet::merge(*master, nullptr) : Sheet();
            const Sheet sheet = Sheet::merge(*g.group->element, master != nullptr ? &base : nullptr);
            const auto value = [&](const char16_t *name) {
                const Cell *c = sheet.cell(name);
                return c != nullptr ? c->number().value_or(0) : 0.0;
            };
            setCell(*element, QStringLiteral("PinX"), numberText(value(u"PinX") + shift.x()));
            setCell(*element, QStringLiteral("PinY"), numberText(value(u"PinY") + shift.y()));
        }
        noteMasters(*element);
        return element;
    }
    // Rebuilt: the source group's element (its own cells, its Master) or a
    // new group, framing the members written into it.
    QString sheetId;
    for (const QString &id : g.members) {
        const Plan &p = m_plans[id];
        if (g.group != nullptr && p.item != nullptr && p.item->groupSheet && p.mode == Mode::Merged && p.id == g.id) {
            sheetId = id;
        }
    }
    XmlElementPtr element;
    QTransform localToPage;
    if (g.group != nullptr) {
        element = g.group->element->clone();
        element->removeChildren(kNs, u"Shapes");
        if (m_ctx.copier != nullptr) {
            m_ctx.copier->rewrite(*element, g.group->part, m_part);
        }
    } else {
        element = makeElement(QStringLiteral("Shape"));
        element->setAttribute({}, QStringLiteral("ID"), QString::number(g.id));
        setShapeNames(*element, QStringLiteral("Group"), g.id);
        element->setAttribute({}, QStringLiteral("Type"), QStringLiteral("Group"));
        for (const char *style : {"LineStyle", "FillStyle", "TextStyle"}) {
            element->setAttribute({}, QString::fromLatin1(style), QString::number(m_ctx.shapeStyle));
        }
    }
    if (!sheetId.isEmpty()) {
        const Shape &sheetShape = *m_page.shape(sheetId);
        const Plan &sp = m_plans[sheetId];
        updateShape(m_ctx, *element, sheetShape, *sp.item->shape, m_toPage, FrameChange{true, {}}, m_part,
                    m_portsNeeded.contains(sheetId), masterOf(*sp.item));
        // The group's local space as the model places its own shape.
        const Placement placed = placementOf(sheetShape, m_toPage);
        localToPage = QTransform(placed.pxPerInch, 0, 0, -placed.pxPerInch, 0, placed.height * placed.pxPerInch)
                    * shapeTransform(sheetShape);
    } else {
        const QRectF bounds = boundsOf(m_page, g.members);
        setGroupFrame(*element, bounds, m_toPage);
        localToPage = QTransform(k, 0, 0, -k, bounds.left(), bounds.bottom());
    }
    const QTransform toGroup = localToPage.inverted();
    XmlElement &shapes = appendElement(*element, QStringLiteral("Shapes"));
    for (const QString &id : g.members) {
        if (id != sheetId) {
            shapes.appendElement(writeItem(id, toGroup));
        }
    }
    noteMasters(*element);
    return element;
}

void PageWriter::writeConnects(XmlElement &contents)
{
    XmlElementPtr connects = makeElement(QStringLiteral("Connects"));
    if (m_source != nullptr) {
        // A source row stays while the shape it glues is written as it was
        // and what it glues to keeps its ID.
        for (const XmlElementPtr &row : m_source->connects) {
            const int from = intAttribute(*row, u"FromSheet", -1);
            const int to = intAttribute(*row, u"ToSheet", -1);
            if (!m_generated.contains(from) && m_verbatimIds.contains(from) && m_keptIds.contains(to)) {
                connects->appendElement(row->clone());
            }
        }
    }
    for (const Connector &c : m_page.connectors) {
        const int id = m_visioIds.value(c.id);
        if (!m_generated.contains(id)) {
            continue;
        }
        for (const bool begin : {true, false}) {
            const GlueEnd glue = glueOf(begin ? c.from : c.to);
            if (glue.sheet <= 0) {
                continue;
            }
            XmlElement &row = appendElement(*connects, QStringLiteral("Connect"));
            row.setAttribute({}, QStringLiteral("FromSheet"), QString::number(id));
            row.setAttribute({}, QStringLiteral("FromCell"), begin ? QStringLiteral("BeginX") : QStringLiteral("EndX"));
            row.setAttribute({}, QStringLiteral("FromPart"), begin ? QStringLiteral("9") : QStringLiteral("12"));
            row.setAttribute({}, QStringLiteral("ToSheet"), QString::number(glue.sheet));
            row.setAttribute({}, QStringLiteral("ToCell"), glue.port >= 0 ? QStringLiteral("Connections.X%1").arg(glue.port + 1)
                                                                            : QStringLiteral("PinX"));
            row.setAttribute({}, QStringLiteral("ToPart"), QString::number(glue.port >= 0 ? 100 + glue.port : 3));
        }
    }
    if (!connects->children.isEmpty()) {
        contents.appendElement(connects);
    }
}

XmlElementPtr PageWriter::build()
{
    plan();
    XmlElementPtr root = makeElement(QStringLiteral("PageContents"));
    root->setAttribute(QString::fromUtf16(Ooxml::Ns::XML), QStringLiteral("space"), QStringLiteral("preserve"));
    // Emission units in the model's paint order: shapes, then connectors;
    // a group at its first member. Each knows its index in the source page.
    struct Unit {
        QString id;      // model item, or empty for a kept source shape
        QString groupId;
        double index = -1;
        XmlElementPtr kept;
    };
    QList<Unit> units;
    QSet<QString> groupsSeen;
    const auto sourceIndex = [&](const QString &key) {
        return m_source != nullptr ? double(m_source->order.indexOf(key)) : -1.0;
    };
    const auto add = [&](const QString &id, const QString &groupId) {
        if (!groupId.isEmpty()) {
            if (groupsSeen.contains(groupId)) {
                return;
            }
            groupsSeen.insert(groupId);
            const GroupPlan &g = m_groups[groupId];
            units.append({id, groupId, g.group != nullptr ? sourceIndex(itemKey(g.group->page, g.group->visioId)) : -1, {}});
            return;
        }
        const Plan &p = m_plans[id];
        const bool home = p.item != nullptr && p.item->group.isEmpty() && m_source != nullptr && p.item->page == m_source->id
                       && p.id == p.item->visioId;
        units.append({id, QString(), home ? sourceIndex(p.item->key) : -1, {}});
    };
    for (const Shape &s : m_page.shapes) {
        add(s.id, s.groupId);
    }
    const qsizetype shapeUnits = units.size();
    for (const Connector &c : m_page.connectors) {
        add(c.id, c.groupId);
    }
    // AGENT-NOTE: Visio stacks shapes in document order. When the model
    // kept the source shapes' order (nobody brought one forward or back),
    // every source shape — connectors and the shapes the model does not
    // hold included — goes back to its source place, so an untouched page
    // is written in its own order; new items follow what preceded them.
    // After a reorder, the model's order wins (connectors on top, as the
    // model paints them).
    double last = -1;
    bool ordered = true;
    for (qsizetype i = 0; i < shapeUnits; ++i) {
        if (units.at(i).index >= 0) {
            ordered = ordered && units.at(i).index > last;
            last = units.at(i).index;
        }
    }
    if (m_source != nullptr) {
        for (auto it = m_source->unmodelled.cbegin(); it != m_source->unmodelled.cend(); ++it) {
            units.append({QString(), QString(), sourceIndex(it.key()), it.value()});
        }
    }
    if (ordered) {
        double previous = -1;
        for (Unit &u : units) {
            if (u.index >= 0) {
                previous = u.index;
            } else if (u.kept == nullptr) {
                u.index = previous + 0.5;
            }
        }
        std::stable_sort(units.begin(), units.end(), [](const Unit &a, const Unit &b) { return a.index < b.index; });
    } else {
        // Kept source shapes stay in front of what followed them.
        QList<Unit> kept;
        QList<Unit> rest;
        for (const Unit &u : std::as_const(units)) {
            (u.kept != nullptr ? kept : rest).append(u);
        }
        std::sort(kept.begin(), kept.end(), [](const Unit &a, const Unit &b) { return a.index < b.index; });
        units.clear();
        for (const Unit &u : std::as_const(rest)) {
            while (!kept.isEmpty() && u.index >= 0 && kept.first().index < u.index) {
                units.append(kept.takeFirst());
            }
            units.append(u);
        }
        units.append(kept);
    }
    XmlElementPtr shapes = makeElement(QStringLiteral("Shapes"));
    for (const Unit &u : std::as_const(units)) {
        if (u.kept != nullptr) {
            XmlElementPtr element = u.kept->clone();
            if (m_ctx.copier != nullptr) {
                m_ctx.copier->rewrite(*element, m_source->part, m_part);
            }
            noteMasters(*element);
            shapes->appendElement(element);
        } else if (!u.groupId.isEmpty()) {
            shapes->appendElement(writeGroup(u.groupId));
        } else {
            shapes->appendElement(writeItem(u.id, m_toPage));
        }
    }
    if (!shapes->children.isEmpty()) {
        root->appendElement(shapes);
    }
    writeConnects(*root);
    return root;
}

} // namespace QindaOffice::Diagram::Visio
