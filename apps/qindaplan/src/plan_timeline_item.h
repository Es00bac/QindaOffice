// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PlanTimeline: the Timeline strip above the Gantt, painted by the same
// plan_output painter as File ▸ Export ▸ Timeline Image, so the window and
// the PNG cannot differ. The strip is document content (a printable
// picture of the plan), so it is paper — white with black ink (D-004) —
// on every theme; the QML frames it in a Tk.Card.

#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

namespace QindaOffice::Apps::Plan {

class PlanService;

class PlanTimelineItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_NAMED_ELEMENT(PlanTimeline)
    Q_PROPERTY(QindaOffice::Apps::Plan::PlanService *service READ service WRITE setService NOTIFY serviceChanged)
public:
    explicit PlanTimelineItem(QQuickItem *parent = nullptr);

    [[nodiscard]] PlanService *service() const { return m_service; }
    void setService(PlanService *service);
    void paint(QPainter *painter) override;

signals:
    void serviceChanged();

private:
    PlanService *m_service = nullptr;
};

} // namespace QindaOffice::Apps::Plan
