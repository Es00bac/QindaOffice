// SPDX-License-Identifier: GPL-3.0-or-later
// DiagramService: shapes, connectors, arrangement, style, text and data —
// each call one Ops call in one undo step.
#include "diagram_service.h"

#include "drawing_document.h"
#include "geometry.h"
#include "ops.h"
#include "routing.h"
#include "stencils.h"
#include "themes.h"

#include <QColor>

#include <cmath>

namespace QindaOffice::Apps::Diagram {

using namespace ::QindaOffice::Diagram;

namespace {

std::optional<QColor> colorOf(const QVariantMap &map, const char *key)
{
    const QString k = QString::fromLatin1(key);
    if (!map.contains(k)) {
        return std::nullopt;
    }
    const QString text = map.value(k).toString();
    if (text.isEmpty() || text == QLatin1String("none")) {
        return QColor(Qt::transparent);
    }
    const QColor color(text);
    return color.isValid() ? std::optional<QColor>(color) : std::nullopt;
}

std::optional<LinePattern> patternOf(const QVariant &value)
{
    const QString text = value.toString();
    if (text == QLatin1String("solid")) return LinePattern::Solid;
    if (text == QLatin1String("dash")) return LinePattern::Dash;
    if (text == QLatin1String("dot")) return LinePattern::Dot;
    if (text == QLatin1String("dashDot")) return LinePattern::DashDot;
    return std::nullopt;
}

std::optional<ArrowStyle> arrowOf(const QVariant &value)
{
    const QString text = value.toString();
    if (text == QLatin1String("none")) return ArrowStyle::None;
    if (text == QLatin1String("filled")) return ArrowStyle::Filled;
    if (text == QLatin1String("open")) return ArrowStyle::Open;
    if (text == QLatin1String("diamond")) return ArrowStyle::Diamond;
    if (text == QLatin1String("circle")) return ArrowStyle::Circle;
    return std::nullopt;
}

Endpoint endpointOf(const QVariantMap &map)
{
    Endpoint end;
    end.shapeId = map.value(QStringLiteral("shapeId")).toString();
    end.port = map.value(QStringLiteral("port"), -1).toInt();
    end.point = QPointF(map.value(QStringLiteral("x")).toDouble(), map.value(QStringLiteral("y")).toDouble());
    return end;
}

} // namespace

bool DiagramService::applyGeometry(const QVector<Geometry> &shapes, const QStringList &movedConnectors,
                                   QPointF connectorDelta, const QStringList &removed, const QString &label)
{
    return editPage(label, [&](Drawing &, Page &page) {
        bool changed = false;
        // A plain move of the whole selection (the common drag) goes through
        // moveItems so containers carry their members and free connector
        // ends travel with the shapes they hang from.
        QStringList moved = movedConnectors;
        QPointF common;
        bool uniformMove = !shapes.isEmpty();
        for (int i = 0; i < shapes.size() && uniformMove; ++i) {
            const Shape *before = page.shape(shapes.at(i).id);
            if (before == nullptr) {
                uniformMove = false;
                break;
            }
            const QPointF delta = shapes.at(i).rect.topLeft() - before->rect.topLeft();
            uniformMove = qFuzzyCompare(shapes.at(i).rect.width(), before->rect.width())
                && qFuzzyCompare(shapes.at(i).rect.height(), before->rect.height())
                && qFuzzyCompare(shapes.at(i).rotation + 360, before->rotation + 360)
                && (i == 0 || QLineF(delta, common).length() < 1e-6);
            common = delta;
            moved.append(shapes.at(i).id);
        }
        if (uniformMove) {
            changed = Ops::moveItems(page, moved, common);
            if (!removed.isEmpty()) {
                changed = Ops::removeItems(page, removed) > 0 || changed;
            }
            return changed;
        }
        for (const Geometry &g : shapes) {
            const Shape *before = page.shape(g.id);
            if (before == nullptr) {
                continue;
            }
            const QPointF delta = g.rect.topLeft() - before->rect.topLeft();
            const bool pureMove = qFuzzyCompare(g.rect.width(), before->rect.width())
                && qFuzzyCompare(g.rect.height(), before->rect.height())
                && qFuzzyCompare(g.rotation + 360, before->rotation + 360);
            // A moved container carries what is inside it (Ops::moveItems).
            if (pureMove && isContainer(*before)) {
                changed = Ops::moveItems(page, {g.id}, delta) || changed;
            } else {
                changed = Ops::setShapeGeometry(page, g.id, g.rect, g.rotation) || changed;
            }
        }
        if (!movedConnectors.isEmpty() && !connectorDelta.isNull()) {
            changed = Ops::moveItems(page, movedConnectors, connectorDelta) || changed;
        }
        if (!removed.isEmpty()) {
            changed = Ops::removeItems(page, removed) > 0 || changed;
        }
        return changed;
    });
}

QString DiagramService::addShape(const QString &stencilId, double x, double y)
{
    QString id;
    const Stencil *stencil = Diagram::stencil(stencilId);
    editPage(tr("Add %1").arg(stencil != nullptr ? stencil->title : stencilId), [&](Drawing &d, Page &page) {
        id = Ops::addShape(d, page, stencilId, QPointF(x, y));
        if (!id.isEmpty()) {
            // Drop onto the grid, like Visio's snap on drop. The CENTRE
            // snaps, so shapes of different heights dropped on one row
            // line up and the connector between them runs straight.
            Shape *shape = page.shape(id);
            const double step = page.gridSpacing / 4;
            if (step > 0) {
                shape->rect.moveCenter(QPointF(std::round(shape->rect.center().x() / step) * step,
                                               std::round(shape->rect.center().y() / step) * step));
            }
        }
        return !id.isEmpty();
    });
    if (!id.isEmpty()) {
        setSelection({id});
    }
    return id;
}

QString DiagramService::connectItems(const QVariantMap &from, const QVariantMap &to)
{
    const Endpoint a = endpointOf(from);
    const Endpoint b = endpointOf(to);
    // A click without a drag between two free points draws nothing.
    if (!a.glued() && !b.glued() && QLineF(a.point, b.point).length() < 4) {
        return {};
    }
    QString id;
    editPage(tr("Connect"), [&](Drawing &d, Page &page) {
        id = Ops::addConnector(d, page, a, b);
        return !id.isEmpty();
    });
    if (!id.isEmpty()) {
        setSelection({id});
    }
    return id;
}

bool DiagramService::setConnectorEnd(const QString &connectorId, bool start, double x, double y)
{
    const Page *page = currentPage();
    if (page == nullptr || page->connector(connectorId) == nullptr) {
        return false;
    }
    const Endpoint end = Ops::glueAt(*page, QPointF(x, y), 10);
    return editPage(tr("Move connector end"),
                    [&](Drawing &, Page &p) { return Ops::setEndpoint(p, connectorId, start, end); });
}

bool DiagramService::setText(const QString &id, const QString &text)
{
    return editPage(tr("Edit text"), [&](Drawing &, Page &page) { return Ops::setText(page, id, text); });
}

bool DiagramService::deleteSelection()
{
    const QStringList ids = m_selection;
    // M87: Visio's Delete — a container takes its members, a lane's pool
    // closes up (Ops::deleteItems).
    const bool ok = editPage(tr("Delete"), [&](Drawing &, Page &page) { return Ops::deleteItems(page, ids) > 0; });
    if (ok) {
        setSelection({});
    }
    return ok;
}

QStringList DiagramService::duplicateSelection()
{
    QStringList copies;
    const QStringList ids = m_selection;
    editPage(tr("Duplicate"), [&](Drawing &d, Page &page) {
        copies = Ops::duplicate(d, page, ids, QPointF(24, 24));
        return !copies.isEmpty();
    });
    if (!copies.isEmpty()) {
        setSelection(copies);
    }
    return copies;
}

bool DiagramService::nudgeSelection(double dx, double dy)
{
    const QStringList ids = m_selection;
    return editPage(tr("Nudge"), [&](Drawing &, Page &page) { return Ops::moveItems(page, ids, QPointF(dx, dy)); },
                    QStringLiteral("nudge"));
}

bool DiagramService::align(const QString &edge)
{
    static const QHash<QString, Ops::Align> edges = {
        {QStringLiteral("left"), Ops::Align::Left},   {QStringLiteral("center"), Ops::Align::Center},
        {QStringLiteral("right"), Ops::Align::Right}, {QStringLiteral("top"), Ops::Align::Top},
        {QStringLiteral("middle"), Ops::Align::Middle}, {QStringLiteral("bottom"), Ops::Align::Bottom}};
    if (!edges.contains(edge)) {
        return false;
    }
    const QStringList ids = m_selection;
    const bool ok = editPage(tr("Align"), [&](Drawing &, Page &page) { return Ops::align(page, ids, edges.value(edge)); });
    if (!ok && ids.size() < 2) {
        refuse(tr("Align"), tr("Select two or more shapes to align."));
    }
    return ok;
}

bool DiagramService::distribute(const QString &axis)
{
    const QStringList ids = m_selection;
    const Qt::Orientation orientation = axis == QLatin1String("vertical") ? Qt::Vertical : Qt::Horizontal;
    const bool ok = editPage(tr("Distribute"), [&](Drawing &, Page &page) { return Ops::distribute(page, ids, orientation); });
    if (!ok && ids.size() < 3) {
        refuse(tr("Distribute"), tr("Select three or more shapes to distribute."));
    }
    return ok;
}

bool DiagramService::order(const QString &how)
{
    static const QHash<QString, Ops::Order> orders = {{QStringLiteral("front"), Ops::Order::Front},
                                                      {QStringLiteral("forward"), Ops::Order::Forward},
                                                      {QStringLiteral("backward"), Ops::Order::Backward},
                                                      {QStringLiteral("back"), Ops::Order::Back}};
    const QStringList ids = m_selection;
    return orders.contains(how)
        && editPage(tr("Order"), [&](Drawing &, Page &page) { return Ops::reorder(page, ids, orders.value(how)); });
}

bool DiagramService::group()
{
    const QStringList ids = m_selection;
    const bool ok = editPage(tr("Group"), [&](Drawing &d, Page &page) { return !Ops::group(d, page, ids).isEmpty(); });
    if (!ok) {
        refuse(tr("Group"), tr("Select two or more items to group."));
    }
    return ok;
}

bool DiagramService::ungroup()
{
    const QStringList ids = m_selection;
    return editPage(tr("Ungroup"), [&](Drawing &, Page &page) { return Ops::ungroup(page, ids); });
}

bool DiagramService::rotateSelection(double degrees)
{
    const QStringList ids = m_selection;
    return editPage(tr("Rotate"), [&](Drawing &, Page &page) { return Ops::rotate(page, ids, degrees); });
}

bool DiagramService::flipSelection(const QString &axis)
{
    const QStringList ids = m_selection;
    const Qt::Orientation orientation = axis == QLatin1String("vertical") ? Qt::Vertical : Qt::Horizontal;
    return editPage(tr("Flip"), [&](Drawing &, Page &page) { return Ops::flip(page, ids, orientation); });
}

bool DiagramService::setAspectLocked(bool locked)
{
    const QStringList ids = m_selection;
    return editPage(tr("Lock aspect ratio"), [&](Drawing &, Page &page) { return Ops::setAspectLocked(page, ids, locked); });
}

bool DiagramService::setStyle(const QVariantMap &style)
{
    Ops::ShapeStylePatch patch;
    patch.fill = colorOf(style, "fill");
    patch.gradient = colorOf(style, "gradient");
    patch.line = colorOf(style, "line");
    if (style.contains(QStringLiteral("lineWidth"))) patch.lineWidth = style.value(QStringLiteral("lineWidth")).toDouble();
    if (style.contains(QStringLiteral("pattern"))) patch.pattern = patternOf(style.value(QStringLiteral("pattern")));
    if (style.contains(QStringLiteral("corner"))) patch.cornerRadius = style.value(QStringLiteral("corner")).toDouble();
    if (style.contains(QStringLiteral("shadow"))) patch.shadow = style.value(QStringLiteral("shadow")).toBool();
    if (style.contains(QStringLiteral("opacity"))) patch.opacity = style.value(QStringLiteral("opacity")).toDouble();
    const QStringList ids = m_selection;
    return editPage(tr("Format shape"), [&](Drawing &, Page &page) { return Ops::applyShapeStyle(page, ids, patch); },
                    style.value(QStringLiteral("merge")).toString());
}

bool DiagramService::setTextStyle(const QVariantMap &text)
{
    Ops::TextStylePatch patch;
    if (text.contains(QStringLiteral("family"))) patch.family = text.value(QStringLiteral("family")).toString();
    if (text.contains(QStringLiteral("size"))) patch.size = text.value(QStringLiteral("size")).toDouble();
    if (text.contains(QStringLiteral("bold"))) patch.bold = text.value(QStringLiteral("bold")).toBool();
    if (text.contains(QStringLiteral("italic"))) patch.italic = text.value(QStringLiteral("italic")).toBool();
    if (text.contains(QStringLiteral("underline"))) patch.underline = text.value(QStringLiteral("underline")).toBool();
    patch.color = colorOf(text, "color");
    const QString align = text.value(QStringLiteral("align")).toString();
    if (!align.isEmpty()) {
        patch.horizontal = align == QLatin1String("left") ? Qt::AlignLeft : align == QLatin1String("right") ? Qt::AlignRight : Qt::AlignHCenter;
    }
    const QString valign = text.value(QStringLiteral("valign")).toString();
    if (!valign.isEmpty()) {
        patch.vertical = valign == QLatin1String("top") ? Qt::AlignTop : valign == QLatin1String("bottom") ? Qt::AlignBottom : Qt::AlignVCenter;
    }
    const int grow = text.value(QStringLiteral("grow")).toInt();
    const QStringList ids = m_selection;
    return editPage(tr("Format text"), [&](Drawing &, Page &page) {
        if (grow != 0) {
            // Grow/shrink steps each item from its own size.
            bool changed = false;
            for (const QString &id : ids) {
                const Shape *s = page.shape(id);
                const Connector *c = page.connector(id);
                const double size = s != nullptr ? s->textStyle.size : c != nullptr ? c->textStyle.size : 0;
                if (size > 0) {
                    Ops::TextStylePatch step = patch;
                    step.size = std::max(4.0, size + (size >= 16 ? 2 : 1) * grow);
                    changed = Ops::applyTextStyle(page, {id}, step) || changed;
                }
            }
            return changed;
        }
        return Ops::applyTextStyle(page, ids, patch);
    });
}

bool DiagramService::setConnectorStyle(const QVariantMap &style)
{
    Ops::ConnectorPatch patch;
    const QString routing = style.value(QStringLiteral("routing")).toString();
    if (routing == QLatin1String("orthogonal")) patch.routing = Routing::Orthogonal;
    else if (routing == QLatin1String("straight")) patch.routing = Routing::Straight;
    else if (routing == QLatin1String("curved")) patch.routing = Routing::Curved;
    if (style.contains(QStringLiteral("startArrow"))) patch.startArrow = arrowOf(style.value(QStringLiteral("startArrow")));
    if (style.contains(QStringLiteral("endArrow"))) patch.endArrow = arrowOf(style.value(QStringLiteral("endArrow")));
    if (style.contains(QStringLiteral("labelPosition"))) patch.labelPosition = style.value(QStringLiteral("labelPosition")).toDouble();
    const QStringList ids = m_selection;
    const bool ok = editPage(tr("Format connector"),
                             [&](Drawing &, Page &page) { return Ops::applyConnectorPatch(page, ids, patch); });
    if (!ok && selectionKind() == QLatin1String("shape")) {
        refuse(tr("Connector"), tr("Select one or more connectors first."));
    }
    return ok;
}

bool DiagramService::quickStyle(int variant)
{
    const Theme &palette = theme(drawing().themeId);
    const QStringList ids = m_selection;
    return editPage(tr("Quick style"), [&](Drawing &, Page &page) {
        bool changed = false;
        for (Shape &shape : page.shapes) {
            if (ids.contains(shape.id)) {
                const ShapeStyle next = Diagram::quickStyle(palette, variant, shape.style);
                changed = changed || !(next == shape.style);
                shape.style = next;
            }
        }
        for (Connector &connector : page.connectors) {
            if (ids.contains(connector.id) && variant >= 0 && variant < palette.roles.size()) {
                changed = changed || connector.line != palette.roles.at(variant).line;
                connector.line = palette.roles.at(variant).line;
            }
        }
        return changed;
    });
}

bool DiagramService::applyFormatTo(const QStringList &ids)
{
    if (!m_formatPainter || m_formatSource.isEmpty()) {
        return false;
    }
    const QString source = m_formatSource;
    const bool ok = editPage(tr("Format painter"), [&](Drawing &, Page &page) { return Ops::copyFormat(page, source, ids); });
    setFormatPainter(false);
    return ok;
}

bool DiagramService::setData(const QString &id, const QString &key, const QString &value)
{
    if (key.trimmed().isEmpty()) {
        refuse(tr("Shape data"), tr("A data field needs a name."));
        return false;
    }
    return editPage(tr("Shape data"), [&](Drawing &, Page &page) { return Ops::setData(page, id, key, value); });
}

bool DiagramService::removeData(const QString &id, const QString &key)
{
    return editPage(tr("Remove data field"), [&](Drawing &, Page &page) { return Ops::removeData(page, id, key); });
}

} // namespace QindaOffice::Apps::Diagram
