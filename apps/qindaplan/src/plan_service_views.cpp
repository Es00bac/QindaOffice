// SPDX-License-Identifier: GPL-3.0-or-later
// PlanService: the view projections — Gantt rows, calendar events and the
// resource usage table (plan_service.h).
#include "plan_document.h"
#include "plan_edit.h"
#include "plan_service.h"
#include "plan_time.h"
#include "resources.h"

#include <QLoggingCategory>

namespace QindaOffice::Apps::Plan {

Q_LOGGING_CATEGORY(lcPlanViews, "qindaoffice.plan.views")

namespace {

QString guidOf(const Engine::Task &t)
{
    return t.guid.toString(QUuid::WithoutBraces);
}

} // namespace

QVariantList PlanService::ganttRows(const Engine::Project &p) const
{
    // Rows under a collapsed summary are hidden (the same rule as
    // rebuildVisible, re-run here because `p` may be a preview copy).
    QList<int> rows;
    for (int i = 0; i < p.tasks.size();) {
        rows.append(i);
        const Engine::Task &t = p.tasks.at(i);
        int next = i + 1;
        if (t.summary && m_collapsed.contains(t.uid)) {
            while (next < p.tasks.size() && p.tasks.at(next).outlineLevel > t.outlineLevel) {
                ++next;
            }
        }
        i = next;
    }
    QSet<int> shown;
    for (int i : std::as_const(rows)) {
        shown.insert(p.tasks.at(i).uid);
    }
    QHash<int, QString> guidByUid;
    for (const Engine::Task &t : p.tasks) {
        guidByUid.insert(t.uid, guidOf(t));
    }
    QVariantList out;
    for (int i : std::as_const(rows)) {
        const Engine::Task &t = p.tasks.at(i);
        QVariantList deps;
        QVariantList types;
        for (int l = 0; l < t.predecessors.size(); ++l) {
            const Engine::Link &link = t.predecessors.at(l);
            if (t.ignoredLinks.contains(l) || !shown.contains(link.predecessorUid)) {
                continue;
            }
            const QString pred = guidByUid.value(link.predecessorUid);
            if (deps.contains(pred)) {
                continue;
            }
            deps.append(pred);
            types.append(Engine::linkTypeCode(link.type));
        }
        const QString kind = t.summary ? QStringLiteral("summary")
                                       : t.isMilestone() ? QStringLiteral("milestone") : QStringLiteral("task");
        QVariantMap row{{QStringLiteral("taskId"), guidOf(t)},
                        {QStringLiteral("label"), t.name},
                        {QStringLiteral("startUtc"), utcOf(t.start)},
                        {QStringLiteral("finishUtc"), utcOf(std::max(t.start, t.finish))},
                        {QStringLiteral("durationMinutes"), qint64(std::max<Engine::Minutes>(0, t.duration))},
                        {QStringLiteral("progress"), double(t.percentComplete) / 100.0},
                        {QStringLiteral("dependencies"), deps},
                        {QStringLiteral("linkTypes"), types},
                        {QStringLiteral("kind"), kind},
                        {QStringLiteral("critical"), m_showCritical && t.critical},
                        {QStringLiteral("inactive"), !t.effectiveActive},
                        {QStringLiteral("outlineLevel"), qMax(0, t.outlineLevel - 1)}};
        if (m_showBarText) {
            QString text;
            if (t.isMilestone()) {
                text = m_locale.toString(Engine::dateOf(t.finish), QStringLiteral("d MMM"));
            } else if (!t.summary) {
                text = Engine::resourceNamesText(p, i, m_locale);
            }
            row.insert(QStringLiteral("barText"), text);
        }
        const Engine::BaselineData &b = t.baselines[0];
        if (m_showBaseline && b.set && b.start != Engine::kNoDate && b.finish != Engine::kNoDate) {
            row.insert(QStringLiteral("baselineStartUtc"), utcOf(b.start));
            row.insert(QStringLiteral("baselineFinishUtc"), utcOf(std::max(b.start, b.finish)));
            row.insert(QStringLiteral("baselineDurationMinutes"), qint64(std::max<Engine::Minutes>(0, b.duration)));
        }
        out.append(row);
    }
    return out;
}

void PlanService::rebuildGantt()
{
    if (m_ganttBusy > 0) {
        return;
    }
    if (m_gantt->previewActive()) {
        m_gantt->cancelPreview();
    }
    if (!m_gantt->replaceTasks(ganttRows(project()))) {
        qCWarning(lcPlanViews) << "Gantt rows refused:" << m_gantt->lastError();
    }
}

void PlanService::rebuildCalendar()
{
    m_calendarEvents.clear();
    const Engine::Project &p = project();
    for (int i = 0; i < p.tasks.size(); ++i) {
        const Engine::Task &t = p.tasks.at(i);
        if (t.summary || t.start == Engine::kNoDate) {
            continue;
        }
        const QDate first = Engine::dateOf(t.isMilestone() ? t.finish : t.start);
        // A finish at midnight belongs to the day before.
        QDate last = Engine::dateOf(t.finish);
        if (Engine::minuteOfDay(t.finish) == 0 && last > first) {
            last = last.addDays(-1);
        }
        const QString calendarId = t.isMilestone() ? QStringLiteral("milestone")
                                   : t.percentComplete >= 100 ? QStringLiteral("done")
                                   : t.critical ? QStringLiteral("critical")
                                                : QStringLiteral("task");
        m_calendarEvents.append(QVariantMap{{QStringLiteral("eventId"), guidOf(t)},
                                            {QStringLiteral("occurrenceId"), guidOf(t)},
                                            {QStringLiteral("title"), t.name},
                                            {QStringLiteral("startLocal"), first.toString(Qt::ISODate)},
                                            {QStringLiteral("endLocal"), last.addDays(1).toString(Qt::ISODate)},
                                            {QStringLiteral("zone"), QString()},
                                            {QStringLiteral("allDay"), true},
                                            {QStringLiteral("calendarId"), calendarId},
                                            {QStringLiteral("readOnly"), true},
                                            {QStringLiteral("location"), Engine::resourceNamesText(p, i, m_locale)}});
    }
}

void PlanService::rebuildUsage()
{
    const Engine::Project &p = project();
    QDate from = Engine::dateOf(p.info.start);
    const QDate to = Engine::dateOf(p.finish);
    int count = 0;
    if (m_usageWeeks) {
        from = from.addDays(1 - from.dayOfWeek());
        count = int(from.daysTo(to) / 7) + 2;
    } else {
        from = from.addDays(1 - from.dayOfWeek());
        count = int(from.daysTo(to)) + 7;
    }
    count = std::clamp(count, 1, 256);
    const Engine::UsageTable table = Engine::usageTable(p, from, count, m_usageWeeks);
    QVariantList resources;
    QVariantList buckets;
    m_usageRows.clear();
    for (const Engine::UsageRow &row : table.rows) {
        const QString name = row.assignment ? QString(QChar(0x2003)) + row.name : row.name;
        resources.append(QVariantMap{{QStringLiteral("id"), row.id},
                                     {QStringLiteral("name"), name},
                                     {QStringLiteral("unit"), QStringLiteral("h")},
                                     {QStringLiteral("capacity"), double(qRound(row.capacityPerBucket * 100.0 / 60.0)) / 100.0}});
        bool over = false;
        for (bool o : row.over) {
            over = over || o;
        }
        m_usageRows.append(QVariantMap{{QStringLiteral("id"), row.id},
                                       {QStringLiteral("name"), row.name},
                                       {QStringLiteral("assignment"), row.assignment},
                                       {QStringLiteral("resourceUid"), row.resourceUid},
                                       {QStringLiteral("total"), Engine::formatWork(row.total, m_locale)},
                                       {QStringLiteral("overallocated"), over}});
    }
    for (int b = 0; b < table.bucketStarts.size(); ++b) {
        const QDate d = table.bucketStarts.at(b);
        buckets.append(QVariantMap{{QStringLiteral("id"), QStringLiteral("b%1").arg(b)},
                                   {QStringLiteral("name"), m_usageWeeks ? m_locale.toString(d, QStringLiteral("d MMM"))
                                                                         : m_locale.toString(d, QStringLiteral("ddd d"))}});
    }
    m_usage->setReadOnly(false);
    m_usage->setResources({});
    m_usage->setBuckets(buckets);
    m_usage->setResources(resources);
    for (const Engine::UsageRow &row : table.rows) {
        for (int b = 0; b < row.work.size(); ++b) {
            if (row.work.at(b) > 0) {
                // Hours to two decimals: the table prints the value as it is.
                m_usage->requestAllocation(row.id, QStringLiteral("b%1").arg(b), double(qRound(row.work.at(b) * 100.0 / 60.0)) / 100.0);
            }
        }
    }
    m_usage->setReadOnly(true);
}

} // namespace QindaOffice::Apps::Plan
