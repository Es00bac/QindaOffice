// SPDX-License-Identifier: GPL-3.0-or-later
// PlanHost (M89, D-097): View ▸ Group By and Highlight, and Gantt Chart
// Format ▸ Second Baseline — their commands, their place in the View menu
// and their checks. Ids are forever (rule 5; plan_command_table.h).
#include "plan_groups.h"
#include "plan_host.h"

namespace QindaOffice::Apps::Plan {

using Quick::ActionEntry;
using Quick::MenuNode;

namespace {

QString capitalized(const QString &key)
{
    return key.left(1).toUpper() + key.mid(1);
}

// Highlight offers the filters that ask nothing first.
const QStringList kHighlights{QStringLiteral("none"),       QStringLiteral("critical"), QStringLiteral("incomplete"),
                              QStringLiteral("completed"),  QStringLiteral("late"),     QStringLiteral("slipping"),
                              QStringLiteral("milestones"), QStringLiteral("summary"),  QStringLiteral("deadlines")};

QList<MenuNode> actionNodes(const QStringList &ids)
{
    QList<MenuNode> out;
    for (const QString &id : ids) {
        out.append(id.isEmpty() ? MenuNode::separator() : MenuNode::action(id));
    }
    return out;
}

} // namespace

void PlanHost::createGroupActions()
{
    for (const QString &group : ::QindaOffice::Plan::taskGroupIds()) {
        connect(action(QStringLiteral("planGroup") + capitalized(group) + QStringLiteral("Action")), &ActionEntry::triggered,
                this, [this, group] { m_service->setGroupBy(group); });
    }
    connect(action(QStringLiteral("planGroupCustomAction")), &ActionEntry::triggered, this, [this] {
        emit filterRequested(QStringLiteral("customGroup")); // FilterDialog asks for the field
        syncChecks();
    });
    action(QStringLiteral("planGroupNoneAction"))->setChecked(true);
    for (const QString &highlight : kHighlights) {
        connect(action(QStringLiteral("planHighlight") + capitalized(highlight) + QStringLiteral("Action")),
                &ActionEntry::triggered, this, [this, highlight] { report(m_service->applyHighlight(highlight)); });
    }
    action(QStringLiteral("planHighlightNoneAction"))->setChecked(true);
    connect(action(QStringLiteral("planShowBaseline2Action")), &ActionEntry::toggled, m_service,
            &PlanService::setShowBaseline2);
    for (int n = 0; n < ::QindaOffice::Plan::kBaselineCount; ++n) {
        connect(action(QStringLiteral("planBaseline2Shown%1Action").arg(n)), &ActionEntry::triggered, this, [this, n] {
            m_service->setGanttBaseline2(n);
            action(QStringLiteral("planShowBaseline2Action"))->setChecked(true); // picking one shows it
        });
    }
    action(QStringLiteral("planBaseline2Shown1Action"))->setChecked(true);
}

QList<MenuNode> PlanHost::groupMenuNodes() const
{
    QStringList groups;
    for (const QString &group : ::QindaOffice::Plan::taskGroupIds()) {
        groups.append(QStringLiteral("planGroup") + capitalized(group) + QStringLiteral("Action"));
        if (group == QLatin1String("none")) {
            groups.append(QString());
        }
    }
    groups << QString() << QStringLiteral("planGroupCustomAction");
    QStringList highlights;
    for (const QString &highlight : kHighlights) {
        highlights.append(QStringLiteral("planHighlight") + capitalized(highlight) + QStringLiteral("Action"));
        if (highlight == QLatin1String("none")) {
            highlights.append(QString());
        }
    }
    QStringList baselines;
    for (int n = 0; n < ::QindaOffice::Plan::kBaselineCount; ++n) {
        baselines.append(QStringLiteral("planBaseline2Shown%1Action").arg(n));
    }
    return {MenuNode::submenu(QStringLiteral("planGroupMenu"), tr("&Group By"), actionNodes(groups)),
            MenuNode::submenu(QStringLiteral("planHighlightMenu"), tr("&Highlight"), actionNodes(highlights)),
            MenuNode::action(QStringLiteral("planShowBaseline2Action")),
            MenuNode::submenu(QStringLiteral("planBaseline2ShownMenu"), tr("Second Baseline Sho&wn"), actionNodes(baselines))};
}

void PlanHost::syncGroupChecks()
{
    const QString group = m_service->groupBy();
    const QString groupAction = group.startsWith(QLatin1String("custom:"))
        ? QStringLiteral("planGroupCustomAction")
        : QStringLiteral("planGroup") + capitalized(group) + QStringLiteral("Action");
    if (ActionEntry *entry = action(groupAction)) {
        entry->setChecked(true);
    }
    if (ActionEntry *entry = action(QStringLiteral("planHighlight") + capitalized(m_service->highlight()) + QStringLiteral("Action"))) {
        entry->setChecked(true);
    }
    action(QStringLiteral("planShowBaseline2Action"))->setChecked(m_service->showBaseline2());
    if (ActionEntry *entry = action(QStringLiteral("planBaseline2Shown%1Action").arg(m_service->ganttBaseline2()))) {
        entry->setChecked(true);
    }
}

} // namespace QindaOffice::Apps::Plan
