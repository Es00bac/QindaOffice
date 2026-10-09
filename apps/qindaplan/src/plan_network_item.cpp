// SPDX-License-Identifier: GPL-3.0-or-later
#include "plan_network_item.h"

#include "network_print.h"
#include "plan_service.h"

#include <QPainter>

#include <algorithm>

namespace QindaOffice::Apps::Plan {

PlanNetworkItem::PlanNetworkItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setObjectName(QStringLiteral("planNetwork"));
}

void PlanNetworkItem::setService(PlanService *service)
{
    if (service == m_service) {
        return;
    }
    if (m_service != nullptr) {
        disconnect(m_service, nullptr, this, nullptr);
    }
    m_service = service;
    if (m_service != nullptr) {
        connect(m_service, &PlanService::changed, this, [this] { update(); });
        connect(m_service, &PlanService::selectionChanged, this, [this] { update(); });
        connect(m_service, &PlanService::viewChanged, this, [this] { update(); });
    }
    emit serviceChanged();
    update();
}

void PlanNetworkItem::setZoom(qreal zoom)
{
    zoom = std::clamp<qreal>(zoom, 0.1, 4.0);
    if (!qFuzzyCompare(zoom, m_zoom)) {
        m_zoom = zoom;
        emit viewportChanged();
        update();
    }
}

void PlanNetworkItem::setOriginX(qreal x)
{
    if (!qFuzzyCompare(x + 1, m_originX + 1)) {
        m_originX = x;
        emit viewportChanged();
        update();
    }
}

void PlanNetworkItem::setOriginY(qreal y)
{
    if (!qFuzzyCompare(y + 1, m_originY + 1)) {
        m_originY = y;
        emit viewportChanged();
        update();
    }
}

void PlanNetworkItem::paint(QPainter *painter)
{
    namespace Engine = ::QindaOffice::Plan;
    const Engine::PlanPaperStyle paper;
    painter->fillRect(QRectF(0, 0, width(), height()), paper.paper);
    if (m_service == nullptr) {
        return;
    }
    Engine::NetworkPaintOptions options;
    options.locale = m_service->locale();
    options.showCritical = m_service->showCritical();
    const int row = m_service->rowOf(m_service->currentTaskId());
    options.selectedTaskUid = row >= 0 ? m_service->project().tasks.at(row).uid : 0;
    painter->save();
    painter->translate(-m_originX, -m_originY);
    painter->scale(m_zoom, m_zoom);
    Engine::paintNetwork(painter, m_service->project(), m_service->networkLayout(), options);
    painter->restore();
}

} // namespace QindaOffice::Apps::Plan
