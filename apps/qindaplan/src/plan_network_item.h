// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PlanNetwork (M79, D-088): the Network Diagram view's canvas, painted by
// plan_output's network painter (network_print.h) — the same picture Print
// and Export PDF make of the view. It is document content, so it is paper
// (white, black ink; D-004) on every theme, framed by the QML.
//
// AGENT-CONTRACT (with qml/NetworkView.qml): the item is the size of the
// scroll VIEWPORT and sits at the scroll position inside a canvas Item of
// the diagram's full size × zoom; `originX`/`originY` are that position in
// canvas pixels. Painting only the viewport keeps the texture small however
// large the plan's diagram is. Hit-testing is PlanService::networkTaskAt in
// layout points (canvas pixels / zoom).

#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

namespace QindaOffice::Apps::Plan {

class PlanService;

class PlanNetworkItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_NAMED_ELEMENT(PlanNetwork)
    Q_PROPERTY(QindaOffice::Apps::Plan::PlanService *service READ service WRITE setService NOTIFY serviceChanged)
    Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY viewportChanged)
    Q_PROPERTY(qreal originX READ originX WRITE setOriginX NOTIFY viewportChanged)
    Q_PROPERTY(qreal originY READ originY WRITE setOriginY NOTIFY viewportChanged)
public:
    explicit PlanNetworkItem(QQuickItem *parent = nullptr);

    [[nodiscard]] PlanService *service() const { return m_service; }
    void setService(PlanService *service);
    [[nodiscard]] qreal zoom() const { return m_zoom; }
    void setZoom(qreal zoom);
    [[nodiscard]] qreal originX() const { return m_originX; }
    void setOriginX(qreal x);
    [[nodiscard]] qreal originY() const { return m_originY; }
    void setOriginY(qreal y);
    void paint(QPainter *painter) override;

signals:
    void serviceChanged();
    void viewportChanged();

private:
    PlanService *m_service = nullptr;
    qreal m_zoom = 1;
    qreal m_originX = 0;
    qreal m_originY = 0;
};

} // namespace QindaOffice::Apps::Plan
