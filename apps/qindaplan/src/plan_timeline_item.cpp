// SPDX-License-Identifier: GPL-3.0-or-later
#include "plan_timeline_item.h"

#include "plan_service.h"
#include "plan_time.h"
#include "timeline_render.h"

#include <QPainter>

namespace QindaOffice::Apps::Plan {

PlanTimelineItem::PlanTimelineItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setObjectName(QStringLiteral("planTimeline"));
}

void PlanTimelineItem::setService(PlanService *service)
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
    }
    emit serviceChanged();
    update();
}

void PlanTimelineItem::paint(QPainter *painter)
{
    if (m_service == nullptr || width() < 1 || height() < 1) {
        return;
    }
    ::QindaOffice::Plan::TimelineOptions options;
    options.today = ::QindaOffice::Plan::toMinutes(m_service->todayDate(), 12 * 60);
    options.locale = m_service->locale();
    options.paper = true;
    ::QindaOffice::Plan::paintTimeline(painter, QRectF(0, 0, width(), height()), m_service->project(), options);
}

} // namespace QindaOffice::Apps::Plan
