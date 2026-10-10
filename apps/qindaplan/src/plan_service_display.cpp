// SPDX-License-Identifier: GPL-3.0-or-later
// PlanService: what the task views show (M79, D-088; plan_service.h has
// the contract) — Project's filter and sort, the Gantt styles and the
// baseline the bars draw, the Network Diagram's layout and the Task Usage
// table. All of it is view state: not undoable, not saved.
#include "custom_fields.h"
#include "network_layout.h"
#include "plan_document.h"
#include "plan_reports.h"
#include "plan_service.h"
#include "plan_time.h"
#include "plan_views.h"
#include "resources.h"

#include <algorithm>

namespace QindaOffice::Apps::Plan {

QString PlanService::filterTitle() const
{
    if (m_filter.id == QLatin1String("custom")) { // M83: name the field
        return QStringLiteral("Custom Field: %1").arg(Engine::customFieldTitle(project(), m_filter.field));
    }
    return Engine::taskFilterTitle(m_filter.id);
}

QVariantMap PlanService::applyFilter(const QString &id, const QVariantMap &params)
{
    if (!Engine::taskFilterIds().contains(id)) {
        return fail(QStringLiteral("There is no filter called “%1”.").arg(id));
    }
    Engine::TaskFilter filter;
    filter.id = id;
    if (id == QLatin1String("resource")) {
        filter.resourceUid = params.value(QStringLiteral("resourceUid")).toInt();
        if (project().resourceIndex(filter.resourceUid) < 0) {
            return fail(QStringLiteral("Choose the resource whose tasks to show."));
        }
    } else if (id == QLatin1String("dateRange")) {
        const std::optional<Engine::Minutes> from = Engine::parseDate(params.value(QStringLiteral("from")).toString(), 0, m_locale);
        const std::optional<Engine::Minutes> to = Engine::parseDate(params.value(QStringLiteral("to")).toString(), 0, m_locale);
        if (!from || !to || *to < *from) {
            return fail(QStringLiteral("Give the range a first day and a last day on or after it."));
        }
        filter.from = Engine::dateOf(*from);
        filter.to = Engine::dateOf(*to);
    }
    m_filter = filter;
    rebuild();
    emit viewChanged();
    return ok({{QStringLiteral("rows"), int(m_visible.size())}});
}

void PlanService::setRelatedSummaries(bool on)
{
    if (on != m_relatedSummaries) {
        m_relatedSummaries = on;
        rebuild();
        emit viewChanged();
    }
}

void PlanService::setSortKey(const QString &key)
{
    if (key != m_sortKey && Engine::taskSortIds().contains(key)) {
        m_sortKey = key;
        rebuild();
        emit viewChanged();
    }
}

void PlanService::setGanttStyle(const QString &style)
{
    static const QStringList known{QStringLiteral("gantt"), QStringLiteral("tracking"), QStringLiteral("leveling")};
    if (style != m_ganttStyle && known.contains(style)) {
        m_ganttStyle = style;
        rebuildGantt();
        emit viewChanged();
    }
}

void PlanService::setGanttBaseline(int number)
{
    number = std::clamp(number, 0, Engine::kBaselineCount - 1);
    if (number != m_ganttBaseline) {
        m_ganttBaseline = number;
        rebuildGantt();
        emit viewChanged();
    }
}

void PlanService::setShowSlack(bool on)
{
    if (on != m_showSlack) {
        m_showSlack = on;
        rebuildGantt();
        emit viewChanged();
    }
}

void PlanService::rebuildVisible()
{
    // Sort (outline kept), drop what a collapsed summary hides, filter.
    const Engine::Project &p = project();
    const QList<int> order = Engine::sortedOutline(p, m_sortKey);
    QList<int> open;
    open.reserve(order.size());
    for (int i : order) {
        bool hidden = false;
        for (int a = p.tasks.at(i).parentIndex; a >= 0 && !hidden; a = p.tasks.at(a).parentIndex) {
            hidden = m_collapsed.contains(p.tasks.at(a).uid);
        }
        if (!hidden) {
            open.append(i);
        }
    }
    const Engine::Minutes reference = Engine::reportReference(p, m_today);
    m_visible = Engine::filterRows(p, open, m_filter, reference, m_relatedSummaries);
    m_filtered = Engine::filterRows(p, order, m_filter, reference, m_relatedSummaries);
}

void PlanService::rebuildNetwork()
{
    m_network = Engine::layOutNetwork(project(), m_visible);
}

QString PlanService::networkTaskAt(double x, double y) const
{
    const int node = Engine::networkNodeAt(m_network, QPointF(x, y));
    return node < 0 ? QString() : idOfRow(m_network.nodes.at(node).taskIndex);
}

QRectF PlanService::networkBoxOf(const QString &taskId) const
{
    const int node = m_network.nodeOfTask.value(rowOf(taskId), -1);
    return node < 0 ? QRectF() : m_network.nodes.at(node).box;
}

void PlanService::rebuildTaskUsage()
{
    const Engine::Project &p = project();
    QDate from = Engine::dateOf(p.info.start);
    from = from.addDays(1 - from.dayOfWeek());
    const QDate to = Engine::dateOf(p.finish);
    int count = std::clamp(m_usageWeeks ? int(from.daysTo(to) / 7) + 2 : int(from.daysTo(to)) + 7, 1, 256);
    // AGENT-GUARD: QindaTK's ResourcePlanModel holds at most 1,000,000
    // cells; a very large plan by day keeps fewer buckets rather than fail.
    const qsizetype rowsEstimate = m_visible.size() + p.assignments.size();
    count = int(std::clamp<qsizetype>(900000 / std::max<qsizetype>(1, rowsEstimate), 1, count));
    const Engine::UsageTable table = Engine::taskUsageTable(p, m_visible, from, count, m_usageWeeks);
    QVariantList rows;
    QVariantList buckets;
    m_taskUsageRows.clear();
    for (const Engine::UsageRow &row : table.rows) {
        // Outline indent for tasks; an assignment one step deeper.
        const int ti = p.taskIndex(row.taskUid);
        const int level = (ti >= 0 ? p.tasks.at(ti).outlineLevel - 1 : 0) + (row.assignment ? 1 : 0);
        const QString name = QString(level, QChar(0x2003)) + row.name;
        Engine::Minutes most = 1;
        for (Engine::Minutes w : row.work) {
            most = std::max(most, w);
        }
        // AGENT-NOTE: tasks have no capacity; the most a row holds in a
        // bucket is given so the table never marks a task's own hours as a
        // conflict (overallocation is the Resource Usage view's job).
        rows.append(QVariantMap{{QStringLiteral("id"), row.id},
                                {QStringLiteral("name"), name},
                                {QStringLiteral("unit"), QStringLiteral("h")},
                                {QStringLiteral("capacity"), double(qRound(most * 100.0 / 60.0)) / 100.0}});
        m_taskUsageRows.append(QVariantMap{{QStringLiteral("id"), row.id},
                                           {QStringLiteral("name"), row.name},
                                           {QStringLiteral("assignment"), row.assignment},
                                           {QStringLiteral("taskId"), ti >= 0 ? idOfRow(ti) : QString()},
                                           {QStringLiteral("total"), Engine::formatWork(row.total, m_locale)}});
    }
    for (int b = 0; b < table.bucketStarts.size(); ++b) {
        const QDate d = table.bucketStarts.at(b);
        buckets.append(QVariantMap{{QStringLiteral("id"), QStringLiteral("b%1").arg(b)},
                                   {QStringLiteral("name"), m_usageWeeks ? m_locale.toString(d, QStringLiteral("d MMM"))
                                                                         : m_locale.toString(d, QStringLiteral("ddd d"))}});
    }
    m_taskUsage->setReadOnly(false);
    m_taskUsage->setResources({});
    m_taskUsage->setBuckets(buckets);
    m_taskUsage->setResources(rows);
    for (const Engine::UsageRow &row : table.rows) {
        for (int b = 0; b < row.work.size(); ++b) {
            if (row.work.at(b) > 0) {
                m_taskUsage->requestAllocation(row.id, QStringLiteral("b%1").arg(b),
                                               double(qRound(row.work.at(b) * 100.0 / 60.0)) / 100.0);
            }
        }
    }
    m_taskUsage->setReadOnly(true);
}

} // namespace QindaOffice::Apps::Plan
