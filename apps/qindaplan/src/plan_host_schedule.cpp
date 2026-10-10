// SPDX-License-Identifier: GPL-3.0-or-later
// PlanHost: the commands M83 added (D-092) — task types and effort driving
// for the selection, Split Task and Remove Splits, Insert Recurring Task and
// Recurring Task Information, Project ▸ Custom Fields, Reschedule
// Uncompleted Work, Sort by Custom Field, progress lines and Import Tasks —
// with their enablement and check state. Ids are forever (rule 5;
// plan_command_table.h); the Custom Fields table and the Custom Field
// filter run through plan_host_views.cpp's table and filter loops.
#include "plan_command_table.h"
#include "plan_document.h"
#include "plan_host.h"

namespace QindaOffice::Apps::Plan {

using Quick::ActionEntry;

namespace {

QString id(const char *raw)
{
    return QString::fromLatin1(raw);
}

const QList<std::pair<const char *, const char *>> &taskTypes()
{
    static const QList<std::pair<const char *, const char *>> types{{"planTaskTypeFixedUnitsAction", "units"},
                                                                     {"planTaskTypeFixedDurationAction", "duration"},
                                                                     {"planTaskTypeFixedWorkAction", "work"}};
    return types;
}

} // namespace

void PlanHost::createScheduleActions()
{
    const auto on = [this](const char *raw, auto &&slot) {
        connect(action(id(raw)), &ActionEntry::triggered, this, std::forward<decltype(slot)>(slot));
    };
    for (const auto &[raw, key] : taskTypes()) {
        const QString type = id(key);
        on(raw, [this, type] {
            report(m_service->setSelectedTaskType(type));
            syncChecks();
        });
    }
    on("planEffortDrivenAction", [this] {
        report(m_service->setSelectedEffortDriven(action(QStringLiteral("planEffortDrivenAction"))->isChecked()));
        syncChecks();
    });
    on("planSplitTaskAction", [this] { emit splitTaskRequested(); });
    on("planRemoveSplitsAction", [this] { report(m_service->removeSplitsSelected(), tr("Splits removed")); });
    on("planInsertRecurringAction", [this] { emit recurringTaskRequested(QString()); });
    on("planRecurringInfoAction", [this] { emit recurringTaskRequested(m_service->currentTaskId()); });
    on("planCustomFieldsAction", [this] { emit customFieldsRequested(); });
    on("planRescheduleWorkAction", [this] { emit rescheduleRequested(); });
    on("planSortCustomAction", [this] {
        emit filterRequested(QStringLiteral("customSort"));
        syncChecks(); // checked once a field is chosen
    });
    on("planImportTasksAction", [this] { emit fileRequested(QStringLiteral("importTasks")); });
    connect(action(QStringLiteral("planShowProgressLinesAction")), &ActionEntry::toggled, m_service,
            &PlanService::setShowProgressLines);
}

void PlanHost::syncScheduleState()
{
    Quick::ActionModel *model = actions();
    const QList<int> rows = m_service->selectedRows();
    const QVariantMap current = m_service->currentTask();
    const bool leaf = !current.isEmpty() && !current.value(QStringLiteral("summary")).toBool();
    model->setEnabled({QStringLiteral("planTaskTypeFixedUnitsAction"), QStringLiteral("planTaskTypeFixedDurationAction"),
                       QStringLiteral("planTaskTypeFixedWorkAction"), QStringLiteral("planEffortDrivenAction"),
                       QStringLiteral("planRemoveSplitsAction")},
                      !rows.isEmpty());
    model->setEnabled(QStringLiteral("planSplitTaskAction"), leaf && !current.value(QStringLiteral("milestoneFlag")).toBool());
    model->setEnabled(QStringLiteral("planRecurringInfoAction"), current.value(QStringLiteral("recurring")).toBool());
    const QString type = current.value(QStringLiteral("type")).toString();
    for (const auto &[raw, key] : taskTypes()) {
        if (type == QLatin1String(key)) {
            action(id(raw))->setChecked(true);
        }
    }
    action(QStringLiteral("planEffortDrivenAction"))->setChecked(current.value(QStringLiteral("effortDriven")).toBool());
    action(QStringLiteral("planShowProgressLinesAction"))->setChecked(m_service->showProgressLines());
    if (m_service->sortKey().startsWith(QLatin1String("custom:"))) {
        action(QStringLiteral("planSortCustomAction"))->setChecked(true);
    }
}

} // namespace QindaOffice::Apps::Plan
