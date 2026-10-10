// SPDX-License-Identifier: GPL-3.0-or-later
#include "canvas_adapter.h"

#include "diagram_service.h"
#include "drawing_document.h"
#include "layers.h"
#include "routing.h"

#include <qindatk/canvas_geometry.h>

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace QindaOffice::Apps::Diagram {

using namespace ::QindaOffice::Diagram;
using QindaTK::CanvasObject;
using QindaTK::CanvasObjectId;

DiagramCanvasAdapter::DiagramCanvasAdapter(QObject *parent)
    : QindaTK::CanvasDocumentAdapter(parent)
{
    m_layer.id = 1;
    m_layer.name = QStringLiteral("Page");
    // One shared transparent pixel for every shape proxy.
    auto blank = std::make_shared<QindaTK::CanvasTiledImage>();
    blank->id = 1;
    blank->size = QSize(1, 1);
    blank->tileSize = 256;
    QImage pixel(1, 1, QImage::Format_ARGB32_Premultiplied);
    pixel.fill(Qt::transparent);
    blank->tiles << pixel;
    m_blank = blank;
}

void DiagramCanvasAdapter::setService(DiagramService *service)
{
    if (m_service == service) {
        return;
    }
    if (m_service != nullptr) {
        disconnect(m_service, nullptr, this, nullptr);
    }
    m_service = service;
    if (service != nullptr) {
        connect(service, &DiagramService::changed, this, &DiagramCanvasAdapter::rebuild);
        connect(service, &DiagramService::currentPageChanged, this, &DiagramCanvasAdapter::rebuild);
    }
    emit serviceChanged();
    rebuild();
}

QString DiagramCanvasAdapter::documentId() const
{
    return m_service != nullptr ? QStringLiteral("qindadiagram#") + m_service->currentPageId() : QString();
}

QRectF DiagramCanvasAdapter::pageRect() const
{
    const Page *page = m_service != nullptr ? m_service->currentPage() : nullptr;
    return page != nullptr ? QRectF(QPointF(0, 0), page->size) : QRectF(0, 0, 1056, 816);
}

QRectF DiagramCanvasAdapter::contentBounds() const
{
    QRectF bounds = pageRect();
    if (m_service != nullptr) {
        const QRectF content = m_service->contentBounds();
        if (!content.isNull()) {
            bounds |= content;
        }
    }
    return bounds;
}

const CanvasObject *DiagramCanvasAdapter::object(CanvasObjectId id) const
{
    const int i = m_index.value(id, -1);
    return i < 0 ? nullptr : &m_objects.at(i);
}

const QindaTK::CanvasLayer *DiagramCanvasAdapter::layer(QindaTK::CanvasLayerId id) const
{
    return id == m_layer.id ? &m_layer : nullptr;
}

QindaTK::CanvasTiledImagePtr DiagramCanvasAdapter::image(QindaTK::CanvasImageId id) const
{
    return id == 1 ? m_blank : nullptr;
}

QVector<CanvasObjectId> DiagramCanvasAdapter::candidates(const QRectF &region) const
{
    QVector<CanvasObjectId> out;
    for (const CanvasObject &o : m_objects) {
        if (QindaTK::CanvasGeometry::documentBounds(o).intersects(region)) {
            out << o.id;
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

QStringList DiagramCanvasAdapter::itemIds(const QVariantList &canvasIds) const
{
    QStringList out;
    for (const QVariant &id : canvasIds) {
        const QString item = m_itemIds.value(id.toULongLong());
        if (!item.isEmpty()) {
            out << item;
        }
    }
    return out;
}

QVariantList DiagramCanvasAdapter::canvasIds(const QStringList &itemIds) const
{
    QVariantList out;
    for (const QString &id : itemIds) {
        if (const CanvasObjectId canvasId = m_canvasIds.value(id)) {
            if (m_index.contains(canvasId)) {
                out << QVariant::fromValue<qulonglong>(canvasId);
            }
        }
    }
    return out;
}

QString DiagramCanvasAdapter::itemId(const QVariant &canvasId) const
{
    return m_itemIds.value(canvasId.toULongLong());
}

CanvasObjectId DiagramCanvasAdapter::idFor(const QString &itemId)
{
    const auto it = m_canvasIds.constFind(itemId);
    if (it != m_canvasIds.constEnd()) {
        return *it;
    }
    const CanvasObjectId id = m_nextId++;
    m_canvasIds.insert(itemId, id);
    m_itemIds.insert(id, itemId);
    return id;
}

QTransform DiagramCanvasAdapter::objectTransform(const QRectF &rect, double rotation)
{
    QTransform t;
    t.translate(rect.center().x(), rect.center().y());
    t.rotate(rotation);
    t.translate(-rect.width() / 2, -rect.height() / 2);
    return t;
}

void DiagramCanvasAdapter::projectTransform(const QRectF &localRect, const QTransform &transform, QRectF *rect,
                                            double *rotation)
{
    // The local x axis gives the angle and x scale; the y scale is the
    // determinant over it.
    const qreal sx = std::hypot(transform.m11(), transform.m12());
    const qreal sy = sx > 0 ? std::abs(transform.determinant()) / sx : 0;
    const QPointF centre = transform.map(localRect.center());
    const QSizeF size(localRect.width() * sx, localRect.height() * sy);
    *rect = QRectF(centre - QPointF(size.width() / 2, size.height() / 2), size);
    double degrees = qRadiansToDegrees(std::atan2(transform.m12(), transform.m11()));
    if (degrees <= -180) degrees += 360;
    if (degrees > 180) degrees -= 360;
    *rotation = std::abs(degrees) < 1e-9 ? 0 : degrees;
}

void DiagramCanvasAdapter::rebuild()
{
    QVector<CanvasObject> objects;
    const Page *page = m_service != nullptr ? m_service->currentPage() : nullptr;
    if (page != nullptr) {
        qint64 z = 0;
        for (const Shape &shape : page->shapes) {
            // M87: a hidden layer's items are not there to pick; a locked
            // layer's cannot be picked or moved.
            if (!layersVisible(*page, shape.layers)) {
                continue;
            }
            CanvasObject o;
            o.id = idFor(shape.id);
            o.layer = m_layer.id;
            o.shape = QindaTK::CanvasShape::Image;
            o.rect = QRectF(QPointF(0, 0), shape.rect.size());
            o.transform = objectTransform(shape.rect, shape.rotation);
            o.fill = QColor(255, 255, 255); // image proxies are "filled" for picking
            o.image = 1;
            o.z = z++;
            o.locked = shape.locked || layersLocked(*page, shape.layers);
            o.name = shape.text.section(QLatin1Char('\n'), 0, 0).left(40);
            objects << o;
        }
        const QHash<QString, Route> routes = routePage(*page);
        for (const Connector &connector : page->connectors) {
            if (!layersVisible(*page, connector.layers)) {
                continue;
            }
            CanvasObject o;
            o.id = idFor(connector.id);
            o.locked = layersLocked(*page, connector.layers);
            o.layer = m_layer.id;
            o.shape = QindaTK::CanvasShape::Polyline;
            o.points = routePolyline(routes.value(connector.id));
            if (o.points.size() < 2) {
                continue;
            }
            o.stroke = QColor(Qt::transparent); // drawn by the page view
            o.strokeWidth = std::max(1.0, connector.lineWidth);
            o.z = z++;
            o.name = connector.text.left(40);
            objects << o;
        }
    }
    m_objects = std::move(objects);
    m_index.clear();
    for (int i = 0; i < m_objects.size(); ++i) {
        m_index.insert(m_objects.at(i).id, i);
    }
    ++m_revision;
    emit reset(m_revision);
    emit edited(m_revision);
    emit changed();
}

bool DiagramCanvasAdapter::commit(const QindaTK::CanvasEdit &edit)
{
    if (m_service == nullptr || !edit.layers.isEmpty() || !edit.images.isEmpty() || edit.page || edit.guides
        || edit.artboards) {
        return false;
    }
    if (edit.changes.isEmpty()) {
        return true;
    }
    QVector<DiagramService::Geometry> shapes;
    QStringList connectors;
    QPointF connectorDelta;
    QStringList removed;
    for (const QindaTK::CanvasEdit::Change &change : edit.changes) {
        const CanvasObject *current = object(change.id);
        // Stale, or an insert: diagram items are created through the service.
        if (current == nullptr || !change.before || !(*change.before == *current)) {
            return false;
        }
        const QString id = m_itemIds.value(change.id);
        if (!change.after) {
            removed << id;
            continue;
        }
        if (change.after->shape != current->shape) {
            return false;
        }
        if (current->shape == QindaTK::CanvasShape::Polyline) {
            // Connectors only translate (their ends follow glue anyway).
            const QPointF origin = current->points.isEmpty() ? QPointF() : current->points.first();
            connectorDelta = change.after->transform.map(origin) - current->transform.map(origin);
            connectors << id;
            continue;
        }
        DiagramService::Geometry g;
        g.id = id;
        projectTransform(current->rect, change.after->transform, &g.rect, &g.rotation);
        shapes.append(g);
    }
    const QString label = edit.label.isEmpty() ? tr("Transform") : edit.label;
    m_service->applyGeometry(shapes, connectors, connectorDelta, removed, label);
    // A gesture that changed nothing (a click) is still a successful commit.
    return true;
}

} // namespace QindaOffice::Apps::Diagram
