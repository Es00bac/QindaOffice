// SPDX-License-Identifier: GPL-3.0-or-later
// PlanHost: the view commands — views, tables, Project's filters and sorts,
// the baseline the Gantt draws, slack — and the tracking and leveling
// commands M79 added (D-088). Ids are forever (rule 5; plan_command_table.h).
#include "plan_command_table.h"
#include "plan_document.h"
#include "plan_host.h"
#include "plan_views.h"

namespace QindaOffice::Apps::Plan {

using Quick::ActionEntry;

namespace {

QString id(const char *raw)
{
    return QString::fromLatin1(raw);
}

QString capitalized(const QString &key)
{
    return key.left(1).toUpper() + key.mid(1);
}

} // namespace

QString PlanHost::viewActionId(const QString &view)
{
    static const QHash<QString, QString> ids{{QStringLiteral("gantt"), QStringLiteral("planViewGanttAction")},
                                             {QStringLiteral("sheet"), QStringLiteral("planViewTaskSheetAction")},
                                             {QStringLiteral("resources"), QStringLiteral("planViewResourceSheetAction")},
                                             {QStringLiteral("usage"), QStringLiteral("planViewResourceUsageAction")},
                                             {QStringLiteral("calendar"), QStringLiteral("planViewCalendarAction")},
                                             {QStringLiteral("reports"), QStringLiteral("planViewReportsAction")},
                                             {QStringLiteral("tracking"), QStringLiteral("planViewTrackingGanttAction")},
                                             {QStringLiteral("leveling"), QStringLiteral("planViewLevelingGanttAction")},
                                             {QStringLiteral("network"), QStringLiteral("planViewNetworkAction")},
                                             {QStringLiteral("taskUsage"), QStringLiteral("planViewTaskUsageAction")}};
    return ids.value(view);
}

bool PlanHost::isGanttView() const
{
    return m_view == QLatin1String("gantt") || m_view == QLatin1String("tracking") || m_view == QLatin1String("leveling");
}

void PlanHost::switchTable(const QString &from, const QString &to)
{
    // AGENT-NOTE: Project gives each view its own table — the Tracking
    // Gantt opens on the Tracking table, the Leveling Gantt on Delay — and
    // remembers a table the user picked there. The Gantt Chart and the
    // Task Sheet share the Entry table's slot.
    const auto slot = [](const QString &view) {
        return view == QLatin1String("tracking") || view == QLatin1String("leveling") ? view : QStringLiteral("entry");
    };
    static const QStringList sheetViews{QStringLiteral("gantt"), QStringLiteral("sheet"), QStringLiteral("tracking"),
                                        QStringLiteral("leveling")};
    if (sheetViews.contains(from)) {
        m_tableOfView.insert(slot(from), m_service->table());
    }
    if (!sheetViews.contains(to)) {
        return;
    }
    const QString fallback = to == QLatin1String("tracking")   ? QStringLiteral("tracking")
                             : to == QLatin1String("leveling") ? QStringLiteral("delay")
                                                               : QStringLiteral("entry");
    m_service->setTable(m_tableOfView.value(slot(to), fallback));
}

void PlanHost::createViewActions()
{
    for (const QString &key : viewKeys()) {
        connect(action(viewActionId(key)), &ActionEntry::toggled, this, [this, key](bool checked) {
            if (checked) {
                setCurrentView(key);
            }
        });
    }
    for (const QString &table : PlanService::tableIds()) {
        connect(action(QStringLiteral("planTable") + capitalized(table) + QStringLiteral("Action")), &ActionEntry::toggled,
                this, [this, table](bool checked) {
                    if (checked) {
                        m_service->setTable(table);
                    }
                });
    }
    // Filters: a click applies one; Using Resource… and Date Range… ask
    // first (the window's FilterDialog calls PlanService::applyFilter), so
    // their check waits for the answer.
    for (const QString &filter : ::QindaOffice::Plan::taskFilterIds()) {
        ActionEntry *entry = action(QStringLiteral("planFilter") + capitalized(filter) + QStringLiteral("Action"));
        connect(entry, &ActionEntry::triggered, this, [this, filter] {
            if (filter == QLatin1String("resource") || filter == QLatin1String("dateRange")
                || filter == QLatin1String("custom")) { // M83: Custom Field… asks too
                emit filterRequested(filter);
                syncChecks();
                return;
            }
            report(m_service->applyFilter(filter));
        });
    }
    action(QStringLiteral("planFilterAllAction"))->setChecked(true);
    ActionEntry *related = action(QStringLiteral("planFilterRelatedSummariesAction"));
    related->setChecked(true);
    connect(related, &ActionEntry::toggled, m_service, &PlanService::setRelatedSummaries);
    for (const QString &key : ::QindaOffice::Plan::taskSortIds()) {
        connect(action(QStringLiteral("planSort") + capitalized(key) + QStringLiteral("Action")), &ActionEntry::triggered,
                this, [this, key] { m_service->setSortKey(key); });
    }
    action(QStringLiteral("planSortIdAction"))->setChecked(true);
    for (int n = 0; n < ::QindaOffice::Plan::kBaselineCount; ++n) {
        ActionEntry *entry = action(QStringLiteral("planBaselineShown%1Action").arg(n));
        connect(entry, &ActionEntry::triggered, this, [this, n] {
            m_service->setGanttBaseline(n);
            if (Quick::ActionEntry *show = action(QStringLiteral("planShowBaselineAction"))) {
                show->setChecked(true); // picking a baseline shows it
            }
        });
    }
    action(QStringLiteral("planBaselineShown0Action"))->setChecked(true);
    connect(action(QStringLiteral("planShowSlackAction")), &ActionEntry::toggled, m_service, &PlanService::setShowSlack);
}

void PlanHost::createTrackingActions()
{
    const auto on = [this](const char *raw, auto &&slot) {
        connect(action(id(raw)), &ActionEntry::triggered, this, std::forward<decltype(slot)>(slot));
    };
    on("planUpdateTasksAction", [this] { emit updateTasksRequested(); });
    on("planMarkOnTrackAction", [this] { report(m_service->markOnTrack(), tr("Marked on track through the status date")); });
    on("planLevelingOptionsAction", [this] { emit levelingOptionsRequested(); });
    on("planLevelSelectionAction", [this] { report(m_service->levelSelection()); });
    on("planClearLevelingSelectedAction",
       [this] { report(m_service->clearLevelingSelected(), tr("Leveling delays of the selected tasks removed")); });
}

} // namespace QindaOffice::Apps::Plan
