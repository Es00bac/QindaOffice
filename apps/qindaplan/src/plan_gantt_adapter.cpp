// SPDX-License-Identifier: GPL-3.0-or-later
#include "plan_gantt_adapter.h"

#include "plan_document.h"
#include "plan_edit.h"
#include "plan_service.h"
#include "scheduler.h"

#include <QUndoStack>

namespace QindaOffice::Apps::Plan {

PlanGanttAdapter::PlanGanttAdapter(PlanService *service)
    : QindaTK::GanttTransactionAdapter(service)
    , m_service(service)
{
}

QVariantMap PlanGanttAdapter::previewMove(const QString &taskId, const QString &startUtc, const QVariantList &tasks,
                                          QindaTK::GanttCalendar *calendar)
{
    Q_UNUSED(tasks)
    Q_UNUSED(calendar)
    const int row = m_service->rowOf(taskId);
    if (row < 0) {
        return {{QStringLiteral("error"), QStringLiteral("There is no such task.")}};
    }
    Engine::Project moved = m_service->project();
    const Engine::EditResult r = Engine::moveTaskStart(moved, row, PlanService::minutesOfUtc(startUtc));
    if (!r.ok) {
        return {{QStringLiteral("error"), r.error}};
    }
    Engine::schedule(moved);
    return {{QStringLiteral("tasks"), m_service->ganttRows(moved)},
            {QStringLiteral("taskId"), taskId},
            {QStringLiteral("startUtc"), startUtc}};
}

bool PlanGanttAdapter::commitMove(const QVariantMap &preview, const QVariantList &before)
{
    Q_UNUSED(before)
    // AGENT-GUARD: the model adopts the preview rows itself after we
    // accept; the service must not re-enter replaceTasks meanwhile.
    const PlanService::GanttGuard guard(m_service);
    const QVariantMap r = m_service->moveTaskStart(preview.value(QStringLiteral("taskId")).toString(),
                                                   preview.value(QStringLiteral("startUtc")).toString());
    return r.value(QStringLiteral("ok")).toBool();
}

void PlanGanttAdapter::cancelMove(const QVariantMap &preview)
{
    Q_UNUSED(preview)
}

QVariantList PlanGanttAdapter::undoMove()
{
    QUndoStack *stack = m_service->document()->undoStack();
    if (!stack->canUndo()) {
        return {};
    }
    const PlanService::GanttGuard guard(m_service);
    stack->undo();
    return m_service->ganttRows(m_service->project());
}

} // namespace QindaOffice::Apps::Plan
