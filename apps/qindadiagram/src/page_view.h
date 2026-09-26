// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// DiagramPageView: paints the current page of a DiagramService UNDER a
// transparent Tk.CanvasView, in that view's pose (M50, D-050; why:
// canvas_adapter.h). Everything is drawn by diagram_engine's painter, so the
// screen is what prints. On top of the page it draws the editor's live
// feedback, all in view space:
//  * a transform preview: while SpatialTransform drags, the selected
//    shapes follow the preview frame and connectors re-route live;
//  * dynamic alignment guides against the other shapes during that drag;
//  * a stencil ghost while a shape is dragged in from the Shapes panel;
//  * connection points of the shape under a connector gesture, the glue
//    target ring, a pending connector, and a selected connector's end
//    handles (filled when glued, hollow when free).
// `overview` paints just the page and its items (the minimap).
//
// AGENT-GUARD: the item must be exactly the CanvasView's size and position
// (anchors.fill the view) — the document -> view transform is computed from
// the view's own pose and size.

#include "diagram_service.h"

#include <qindatk/canvas_surface.h>

#include <QColor>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QRectF>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaOffice::Diagram {
struct Page;
}

namespace QindaOffice::Apps::Diagram {

class DiagramPageView : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaOffice::Apps::Diagram::DiagramService *service READ service WRITE setService NOTIFY configChanged)
    Q_PROPERTY(QindaTK::CanvasSurface *surface READ surface WRITE setSurface NOTIFY configChanged)
    Q_PROPERTY(bool overview MEMBER m_overview NOTIFY configChanged)
    Q_PROPERTY(bool showGrid MEMBER m_showGrid NOTIFY configChanged)
    // Chrome roles from the QML side (Tk.Theme): never literals in C++.
    Q_PROPERTY(QColor canvasColor MEMBER m_canvasColor NOTIFY configChanged)
    Q_PROPERTY(QColor accentColor MEMBER m_accentColor NOTIFY configChanged)
    Q_PROPERTY(QColor gridColor MEMBER m_gridColor NOTIFY configChanged)
    Q_PROPERTY(QColor shadowColor MEMBER m_shadowColor NOTIFY configChanged)
    Q_PROPERTY(QColor handleColor MEMBER m_handleColor NOTIFY configChanged)
    // Transform preview (SpatialTransform.selectionBounds -> previewCorners).
    Q_PROPERTY(bool previewActive MEMBER m_previewActive NOTIFY configChanged)
    Q_PROPERTY(QRectF previewFrom MEMBER m_previewFrom NOTIFY configChanged)
    Q_PROPERTY(QVariantList previewCorners MEMBER m_previewCorners NOTIFY configChanged)
    // Stencil ghost while dragging from the Shapes panel (page units).
    Q_PROPERTY(QString dropStencil MEMBER m_dropStencil NOTIFY configChanged)
    Q_PROPERTY(QPointF dropPoint MEMBER m_dropPoint NOTIFY configChanged)
    // Connector gesture feedback (page units).
    Q_PROPERTY(QString hoverShapeId MEMBER m_hoverShapeId NOTIFY configChanged)
    Q_PROPERTY(bool glueVisible MEMBER m_glueVisible NOTIFY configChanged)
    Q_PROPERTY(QPointF gluePoint MEMBER m_gluePoint NOTIFY configChanged)
    Q_PROPERTY(bool pendingVisible MEMBER m_pendingVisible NOTIFY configChanged)
    Q_PROPERTY(QPointF pendingFrom MEMBER m_pendingFrom NOTIFY configChanged)
    Q_PROPERTY(QPointF pendingTo MEMBER m_pendingTo NOTIFY configChanged)
    // A connector end being dragged: which connector, which end, where.
    Q_PROPERTY(QString endDragConnector MEMBER m_endDragConnector NOTIFY configChanged)
    Q_PROPERTY(bool endDragStart MEMBER m_endDragStart NOTIFY configChanged)
    Q_PROPERTY(QPointF endDragPoint MEMBER m_endDragPoint NOTIFY configChanged)
public:
    explicit DiagramPageView(QQuickItem *parent = nullptr);

    [[nodiscard]] DiagramService *service() const { return m_service; }
    void setService(DiagramService *service);
    [[nodiscard]] QindaTK::CanvasSurface *surface() const { return m_surface; }
    void setSurface(QindaTK::CanvasSurface *surface);

    void paint(QPainter *painter) override;

    // The page as currently previewed (transform, end drag applied).
    [[nodiscard]] QindaOffice::Diagram::Page previewPage() const;
    // Page-space end points of a connector (for its handles), [start, end].
    Q_INVOKABLE QVariantList connectorEnds(const QString &connectorId) const;

signals:
    void configChanged();

private:
    [[nodiscard]] QTransform documentToView() const;
    void paintFeedback(QPainter *painter, const QindaOffice::Diagram::Page &page, const QTransform &toView);
    void paintGuides(QPainter *painter, const QindaOffice::Diagram::Page &page, const QTransform &toView);

    QPointer<DiagramService> m_service;
    QPointer<QindaTK::CanvasSurface> m_surface;
    bool m_overview = false;
    bool m_showGrid = true;
    QColor m_canvasColor;
    QColor m_accentColor;
    QColor m_gridColor;
    QColor m_shadowColor;
    QColor m_handleColor;
    bool m_previewActive = false;
    QRectF m_previewFrom;
    QVariantList m_previewCorners;
    QString m_dropStencil;
    QPointF m_dropPoint;
    QString m_hoverShapeId;
    bool m_glueVisible = false;
    QPointF m_gluePoint;
    bool m_pendingVisible = false;
    QPointF m_pendingFrom;
    QPointF m_pendingTo;
    QString m_endDragConnector;
    bool m_endDragStart = false;
    QPointF m_endDragPoint;
};

} // namespace QindaOffice::Apps::Diagram
