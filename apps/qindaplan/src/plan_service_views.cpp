// SPDX-License-Identifier: GPL-3.0-or-later
// PlanService: the view projections — Gantt rows, calendar events and the
// resource usage table (plan_service.h).
#include "plan_document.h"
#include "plan_edit.h"
#include "plan_types.h"
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
    // The view's rows (plan_service.h, M79: sorted, filtered, collapsed).
    // AGENT-GUARD: a preview copy (a bar drag) has the committed project's
    // tasks in the same order, so the same indices give the same ids —
    // GanttTaskModel requires that of a preview.
    QSet<int> shown;
    for (int i : m_visible) {
        if (i < p.tasks.size()) {
            shown.insert(p.tasks.at(i).uid);
        }
    }
    QHash<int, QString> guidByUid;
    for (const Engine::Task &t : p.tasks) {
        guidByUid.insert(t.uid, guidOf(t));
    }
    QVariantList out;
    for (int i : m_visible) {
        if (i < p.tasks.size()) {
            out.append(ganttRow(p, i, shown, guidByUid));
        }
    }
    return out;
}

QVariantMap PlanService::ganttRow(const Engine::Project &p, int i, const QSet<int> &shown,
                                  const QHash<int, QString> &guidByUid) const
{
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
    // M79: what Project's Gantt views write after a bar — resources (Gantt
    // Chart), % complete (Tracking Gantt), the leveling delay (Leveling
    // Gantt) — and, with Slack on, the total slack (QindaTK's GanttView
    // draws no slack bar; the printout does, gantt_print_extras.cpp).
    const bool tracking = m_ganttStyle == QLatin1String("tracking");
    const bool leveling = m_ganttStyle == QLatin1String("leveling");
    if (m_showBarText || m_showSlack) {
        QString text;
        if (m_showBarText) {
            if (tracking) {
                text = m_locale.toString(t.percentComplete) + QLatin1Char('%');
            } else if (t.isMilestone()) {
                text = m_locale.toString(Engine::dateOf(t.finish), QStringLiteral("d MMM"));
            } else if (leveling && !t.summary && t.levelingDelay > 0) {
                text = Engine::taskFieldText(p, i, QStringLiteral("levelingDelay"), m_locale);
            } else if (!leveling && !t.summary) {
                text = Engine::resourceNamesText(p, i, m_locale);
            }
        }
        if (m_showSlack && !t.summary && t.effectiveActive && t.totalSlack > 0) {
            const QString slack = QStringLiteral("+%1 slack").arg(Engine::formatSpan(t.totalSlack, Engine::durationFormatOf(p.info), m_locale));
            text = text.isEmpty() ? slack : text + QStringLiteral("  ·  ") + slack;
        }
        row.insert(QStringLiteral("barText"), text);
    }
    // The lower lane: the chosen baseline, or the pre-leveled dates.
    Engine::Minutes lowStart = Engine::kNoDate;
    Engine::Minutes lowFinish = Engine::kNoDate;
    Engine::Minutes lowDuration = 0;
    if (leveling) {
        if (!t.summary && t.preLeveledStart != Engine::kNoDate && t.preLeveledFinish != Engine::kNoDate) {
            lowStart = t.preLeveledStart;
            lowFinish = t.preLeveledFinish;
            lowDuration = t.duration;
        }
    } else if (m_showBaseline || tracking) {
        const Engine::BaselineData &b = t.baselines[size_t(m_ganttBaseline)];
        if (b.set && b.start != Engine::kNoDate && b.finish != Engine::kNoDate) {
            lowStart = b.start;
            lowFinish = b.finish;
            lowDuration = b.duration;
        }
    }
    if (lowStart != Engine::kNoDate) {
        row.insert(QStringLiteral("baselineStartUtc"), utcOf(lowStart));
        row.insert(QStringLiteral("baselineFinishUtc"), utcOf(std::max(lowStart, lowFinish)));
        row.insert(QStringLiteral("baselineDurationMinutes"), qint64(std::max<Engine::Minutes>(0, lowDuration)));
    }
    return row;
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
    for (int i : std::as_const(m_filtered)) { // M79: the filter applies here too
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
