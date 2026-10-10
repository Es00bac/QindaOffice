// SPDX-License-Identifier: GPL-3.0-or-later
#include "page_view.h"

#include "diagram_service.h"
#include "geometry.h"
#include "layers.h"
#include "ops.h"
#include "print_tiles.h"
#include "painter.h"
#include "routing.h"
#include "stencils.h"
#include "themes.h"

#include <qindatk/canvas_surface.h>

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace QindaOffice::Apps::Diagram {

using namespace ::QindaOffice::Diagram;

namespace {

// The affine map taking three corners of `from` to three points.
QTransform affineFrom(const QRectF &from, const QVariantList &corners)
{
    if (corners.size() < 4 || from.width() <= 0 || from.height() <= 0) {
        return {};
    }
    const QPointF p0 = corners.at(0).toPointF();
    const QPointF p1 = corners.at(1).toPointF();
    const QPointF p3 = corners.at(3).toPointF();
    // Unit square -> preview parallelogram, composed with from -> unit square.
    const QTransform unitToPreview(p1.x() - p0.x(), p1.y() - p0.y(), p3.x() - p0.x(), p3.y() - p0.y(), p0.x(), p0.y());
    QTransform fromToUnit;
    fromToUnit.scale(1 / from.width(), 1 / from.height());
    fromToUnit.translate(-from.left(), -from.top());
    return fromToUnit * unitToPreview;
}

void applyToShape(Shape &shape, const QTransform &t)
{
    // Scale factors along the shape's own axes, then rotation added.
    const QPointF centre = t.map(shape.rect.center());
    QTransform turn;
    turn.rotate(shape.rotation);
    const QPointF ax = turn.map(QPointF(1, 0));
    const QPointF ay = turn.map(QPointF(0, 1));
    const auto linear = [&](QPointF v) { return QPointF(t.m11() * v.x() + t.m21() * v.y(), t.m12() * v.x() + t.m22() * v.y()); };
    const QPointF mx = linear(ax);
    const QPointF my = linear(ay);
    const double sx = std::hypot(mx.x(), mx.y());
    const double sy = std::hypot(my.x(), my.y());
    const QSizeF size(std::max(4.0, shape.rect.width() * sx), std::max(4.0, shape.rect.height() * sy));
    shape.rect = QRectF(centre - QPointF(size.width() / 2, size.height() / 2), size);
    shape.rotation = std::atan2(mx.y(), mx.x()) * 180.0 / M_PI;
}

} // namespace

DiagramPageView::DiagramPageView(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setOpaquePainting(false);
    connect(this, &DiagramPageView::configChanged, this, [this] { update(); });
}

void DiagramPageView::setService(DiagramService *service)
{
    if (m_service == service) {
        return;
    }
    if (m_service != nullptr) {
        disconnect(m_service, nullptr, this, nullptr);
    }
    m_service = service;
    if (service != nullptr) {
        const auto repaint = [this] { update(); };
        connect(service, &DiagramService::changed, this, repaint);
        connect(service, &DiagramService::currentPageChanged, this, repaint);
        connect(service, &DiagramService::selectionChanged, this, repaint);
        connect(service, &DiagramService::editingTextIdChanged, this, repaint);
    }
    emit configChanged();
}

void DiagramPageView::setSurface(QindaTK::CanvasSurface *surface)
{
    if (m_surface == surface) {
        return;
    }
    if (m_surface != nullptr) {
        disconnect(m_surface, nullptr, this, nullptr);
    }
    m_surface = surface;
    if (surface != nullptr) {
        connect(surface, &QindaTK::CanvasSurface::viewChanged, this, [this] { update(); });
    }
    emit configChanged();
}

QTransform DiagramPageView::documentToView() const
{
    // docs/canvas.md#coordinate-spaces: T(size/2) · R(rotation) · S(zoom) · T(-centre)
    if (m_surface == nullptr) {
        return {};
    }
    QTransform t;
    t.translate(m_surface->width() / 2, m_surface->height() / 2);
    t.rotate(m_surface->viewRotation());
    t.scale(m_surface->zoom(), m_surface->zoom());
    t.translate(-m_surface->centerX(), -m_surface->centerY());
    return t;
}

Page DiagramPageView::previewPage() const
{
    const Page *current = m_service != nullptr ? m_service->currentPage() : nullptr;
    if (current == nullptr) {
        return {};
    }
    Page page = *current;
    if (m_previewActive && !m_service->selection().isEmpty()) {
        const QTransform t = affineFrom(m_previewFrom, m_previewCorners);
        if (!t.isIdentity()) {
            QStringList moving = m_service->selection();
            // Containers carry their members in the preview too.
            for (const QString &id : m_service->selection()) {
                for (const QString &member : Ops::containerMembers(page, id)) {
                    if (!moving.contains(member)) {
                        moving.append(member);
                    }
                }
            }
            for (Shape &shape : page.shapes) {
                if (moving.contains(shape.id)) {
                    applyToShape(shape, t);
                }
            }
            for (Connector &connector : page.connectors) {
                if (!connector.from.glued() && moving.contains(connector.id)) {
                    connector.from.point = t.map(connector.from.point);
                }
                if (!connector.to.glued() && moving.contains(connector.id)) {
                    connector.to.point = t.map(connector.to.point);
                }
            }
        }
    }
    if (!m_endDragConnector.isEmpty()) {
        if (Connector *connector = page.connector(m_endDragConnector)) {
            const Endpoint end = Ops::glueAt(page, m_endDragPoint, 10);
            (m_endDragStart ? connector->from : connector->to) = end;
        }
    }
    return page;
}

QVariantList DiagramPageView::connectorEnds(const QString &connectorId) const
{
    const Page *page = m_service != nullptr ? m_service->currentPage() : nullptr;
    const Connector *connector = page != nullptr ? page->connector(connectorId) : nullptr;
    if (connector == nullptr) {
        return {};
    }
    const Route route = routeConnector(*page, *connector);
    return {route.start(), route.end(), connector->from.glued(), connector->to.glued()};
}

void DiagramPageView::paint(QPainter *painter)
{
    painter->setRenderHint(QPainter::Antialiasing);
    painter->fillRect(QRectF(0, 0, width(), height()), m_canvasColor);
    if (m_service == nullptr || m_surface == nullptr || m_service->currentPage() == nullptr) {
        return;
    }
    const Page page = previewPage();
    const QTransform toView = documentToView();
    painter->save();
    painter->setTransform(toView, true);
    // The sheet lifts off the canvas with a soft shadow (chrome colour).
    const QRectF sheet(QPointF(0, 0), page.size);
    const double unit = 1.0 / std::max(0.01, m_surface->zoom());
    painter->setPen(Qt::NoPen);
    QColor shadow = m_shadowColor;
    for (int i = 1; i <= 3; ++i) {
        shadow.setAlphaF(m_shadowColor.alphaF() * (0.5 / i));
        painter->fillRect(sheet.translated(0, unit * i * 1.5).adjusted(-unit * i, 0, unit * i, unit * i), shadow);
    }
    PaintOptions options;
    options.grid = m_showGrid && page.grid && !m_overview;
    options.gridColor = m_gridColor;
    if (!m_service->editingTextId().isEmpty()) {
        options.hideTextOf.insert(m_service->editingTextId());
    }
    const QHash<QString, Route> routes = routePage(page);
    options.routes = &routes;
    // M87: the background pages show under the page (never editable here).
    options.backgrounds = backgroundChain(m_service->drawing(), page);
    paintPage(painter, page, options);
    if (!m_dropStencil.isEmpty() && !m_overview) {
        Drawing ghost;
        ghost.themeId = m_service->themeId();
        Page ghostPage;
        const QString id = Ops::addShape(ghost, ghostPage, m_dropStencil, m_dropPoint);
        if (Shape *shape = ghostPage.shape(id)) {
            painter->setOpacity(0.55);
            paintShape(painter, *shape);
            painter->setOpacity(1);
        }
    }
    painter->restore();
    if (!m_overview) {
        paintGuides(painter, page, toView);
        paintFeedback(painter, page, toView);
        paintReview(painter, page, toView);
    }
}

QVariantList DiagramPageView::autoConnectArrows() const
{
    const Page *page = m_service != nullptr ? m_service->currentPage() : nullptr;
    const Shape *shape = page != nullptr ? page->shape(m_autoConnectShape) : nullptr;
    if (shape == nullptr || isContainer(*shape) || m_surface == nullptr) {
        return {};
    }
    const QTransform toView = documentToView();
    const QRectF b = toView.mapRect(shapeBounds(*shape));
    const double gap = 16; // view pixels outside the shape's box
    return {QPointF(b.center().x(), b.top() - gap), QPointF(b.right() + gap, b.center().y()),
            QPointF(b.center().x(), b.bottom() + gap), QPointF(b.left() - gap, b.center().y())};
}

QString DiagramPageView::commentMarkerAt(double x, double y) const
{
    if (m_service == nullptr || m_surface == nullptr) {
        return {};
    }
    const QTransform toView = documentToView();
    for (const QVariant &v : m_service->commentMarkers()) {
        const QVariantMap m = v.toMap();
        const QPointF at = toView.map(QPointF(m.value(QStringLiteral("x")).toDouble(), m.value(QStringLiteral("y")).toDouble()));
        if (QRectF(at.x() - 2, at.y() - 16, 20, 18).contains(QPointF(x, y))) {
            return m.value(QStringLiteral("id")).toString();
        }
    }
    return {};
}

void DiagramPageView::paintReview(QPainter *painter, const Page &page, const QTransform &toView)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    if (m_showPageBreaks) {
        // Where one sheet ends and the next begins, each cut drawn once
        // across the page (Visio's page breaks).
        QList<double> xs;
        QList<double> ys;
        const auto add = [](QList<double> &list, double v, double edge) {
            if (v > edge + 0.5 && std::none_of(list.cbegin(), list.cend(), [v](double o) { return std::abs(o - v) < 0.5; })) {
                list.append(v);
            }
        };
        for (const QRectF &tile : pageBreakTiles(page)) {
            add(xs, tile.left(), 0);
            add(ys, tile.top(), 0);
        }
        painter->setPen(QPen(m_accentColor, 1, Qt::DashLine));
        for (const double x : std::as_const(xs)) {
            painter->drawLine(toView.map(QPointF(x, 0)), toView.map(QPointF(x, page.size.height())));
        }
        for (const double y : std::as_const(ys)) {
            painter->drawLine(toView.map(QPointF(0, y)), toView.map(QPointF(page.size.width(), y)));
        }
    }
    // A speech-bubble marker on every commented item (screen only).
    for (const QVariant &v : m_service->commentMarkers()) {
        const QVariantMap m = v.toMap();
        const QPointF at = toView.map(QPointF(m.value(QStringLiteral("x")).toDouble(), m.value(QStringLiteral("y")).toDouble()));
        QPainterPath bubble;
        bubble.addRoundedRect(QRectF(at.x(), at.y() - 14, 16, 11), 3, 3);
        bubble.moveTo(at.x() + 3, at.y() - 3.5);
        bubble.lineTo(at.x() + 2, at.y() + 1);
        bubble.lineTo(at.x() + 7, at.y() - 3.5);
        const bool active = m.value(QStringLiteral("id")).toString() == m_service->activeComment();
        QColor fill = m_accentColor;
        fill.setAlphaF(m.value(QStringLiteral("done")).toBool() ? 0.45f : 1.0f);
        painter->setPen(QPen(active ? m_handleColor : fill, active ? 2 : 1));
        painter->setBrush(fill);
        painter->drawPath(bubble);
    }
    // Visio's AutoConnect arrows: a small triangle beyond each side.
    const QVariantList arrows = autoConnectArrows();
    for (int i = 0; i < arrows.size(); ++i) {
        const QPointF c = arrows.at(i).toPointF();
        static const QPointF dirs[] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
        const QPointF d = dirs[i];
        const QPointF n(-d.y(), d.x());
        const double r = i == m_autoConnectHover ? 8 : 6;
        QColor fill = m_accentColor;
        fill.setAlphaF(i == m_autoConnectHover ? 1.0f : 0.7f);
        painter->setPen(Qt::NoPen);
        painter->setBrush(fill);
        painter->drawPolygon(QPolygonF({c + d * r, c - d * r * 0.6 + n * r, c - d * r * 0.6 - n * r}));
    }
    painter->restore();
}

void DiagramPageView::paintGuides(QPainter *painter, const Page &page, const QTransform &toView)
{
    if (!m_previewActive || m_service->selection().isEmpty()) {
        return;
    }
    // Dynamic alignment guides: edges and centres of the moving selection
    // that line up with another shape's.
    QRectF moving;
    for (const Shape &shape : page.shapes) {
        if (m_service->selection().contains(shape.id)) {
            moving = moving.isNull() ? shapeBounds(shape) : moving | shapeBounds(shape);
        }
    }
    if (moving.isNull()) {
        return;
    }
    const double tolerance = 0.75;
    QPen pen(m_accentColor, 1, Qt::DashLine);
    painter->setPen(pen);
    for (const Shape &shape : page.shapes) {
        if (m_service->selection().contains(shape.id) || isContainer(shape)) {
            continue;
        }
        const QRectF other = shapeBounds(shape);
        const double xs[] = {moving.left(), moving.center().x(), moving.right()};
        const double oxs[] = {other.left(), other.center().x(), other.right()};
        const double ys[] = {moving.top(), moving.center().y(), moving.bottom()};
        const double oys[] = {other.top(), other.center().y(), other.bottom()};
        for (double x : xs) {
            for (double ox : oxs) {
                if (std::abs(x - ox) < tolerance) {
                    const double top = std::min(moving.top(), other.top());
                    const double bottom = std::max(moving.bottom(), other.bottom());
                    painter->drawLine(toView.map(QPointF(x, top - 12)), toView.map(QPointF(x, bottom + 12)));
                }
            }
        }
        for (double y : ys) {
            for (double oy : oys) {
                if (std::abs(y - oy) < tolerance) {
                    const double left = std::min(moving.left(), other.left());
                    const double right = std::max(moving.right(), other.right());
                    painter->drawLine(toView.map(QPointF(left - 12, y)), toView.map(QPointF(right + 12, y)));
                }
            }
        }
    }
}

void DiagramPageView::paintFeedback(QPainter *painter, const Page &page, const QTransform &toView)
{
    const double r = 4.5;
    // Connection points of the shape under a connector gesture.
    if (const Shape *shape = page.shape(m_hoverShapeId)) {
        QPen outline(m_accentColor, 1.5);
        painter->setPen(outline);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(toView.map(pageOutline(*shape)));
        painter->setPen(QPen(m_accentColor, 1.5));
        for (int i = 0; i < portCount(*shape); ++i) {
            const QPointF p = toView.map(portPoint(*shape, i));
            painter->drawLine(p + QPointF(-r * 0.8, -r * 0.8), p + QPointF(r * 0.8, r * 0.8));
            painter->drawLine(p + QPointF(-r * 0.8, r * 0.8), p + QPointF(r * 0.8, -r * 0.8));
        }
    }
    if (m_pendingVisible) {
        QPen pen(m_accentColor, 1.75, Qt::DashLine);
        painter->setPen(pen);
        painter->drawLine(toView.map(m_pendingFrom), toView.map(m_pendingTo));
    }
    if (m_glueVisible) {
        painter->setPen(QPen(m_accentColor, 2));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(toView.map(m_gluePoint), r * 1.8, r * 1.8);
    }
    // End handles of a lone selected connector.
    const QStringList selection = m_service->selection();
    if (selection.size() == 1) {
        if (const Connector *connector = page.connector(selection.first())) {
            const Route route = routeConnector(page, *connector);
            const QPair<QPointF, bool> ends[] = {{route.start(), connector->from.glued()},
                                                 {route.end(), connector->to.glued()}};
            for (const auto &end : ends) {
                painter->setPen(QPen(m_accentColor, 1.5));
                painter->setBrush(end.second ? QBrush(m_accentColor) : QBrush(m_handleColor));
                painter->drawEllipse(toView.map(end.first), r, r);
            }
        }
    }
}

} // namespace QindaOffice::Apps::Diagram
