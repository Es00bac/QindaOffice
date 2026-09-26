// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PlanGanttAdapter: the Tk.GanttView's transaction boundary (QindaTK
// docs/gantt.md "Transaction boundary") over the plan engine. Dragging a
// bar previews the whole schedule as the engine would compute it with the
// task moved (auto tasks get Start No Earlier Than, manual tasks move);
// commit applies the same move to the document as one undo step; undo is
// the document's undo.
//
// AGENT-CONTRACT (with PlanService::rebuildGantt): preview rows have
// exactly the committed rows' order and ids — they are built by the same
// function from the moved copy of the project.

#include <qindatk/gantt_task_model.h>

namespace QindaOffice::Apps::Plan {

class PlanService;

class PlanGanttAdapter final : public QindaTK::GanttTransactionAdapter {
    Q_OBJECT
public:
    explicit PlanGanttAdapter(PlanService *service);

    QVariantMap previewMove(const QString &taskId, const QString &startUtc, const QVariantList &tasks,
                            QindaTK::GanttCalendar *calendar) override;
    bool commitMove(const QVariantMap &preview, const QVariantList &before) override;
    void cancelMove(const QVariantMap &preview) override;
    QVariantList undoMove() override;

private:
    PlanService *m_service;
};

} // namespace QindaOffice::Apps::Plan
