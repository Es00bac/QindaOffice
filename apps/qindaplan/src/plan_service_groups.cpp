// SPDX-License-Identifier: GPL-3.0-or-later
// PlanService (M89, D-097): Project's Group By and Highlight — the rows the
// task sheet and the Gantt share once a view is grouped, as QindaTK's group
// header rows (D-312) — and the planning bars QindaTK's GanttView draws
// (D-313): total and free slack, deadlines, the progress point and the
// progress line, split pieces and a second baseline. All of it is view
// state: not undoable, not saved (plan_service.h has the contract).
#include "plan_document.h"
#include "plan_reports.h"
#include "plan_service.h"
#include "plan_time.h"
#include "scheduler.h"
#include "task_segments.h"
#include "tracking.h"
#include "work_calendar.h"

#include <QUuid>

#include <algorithm>

namespace QindaOffice::Apps::Plan {

namespace {

// AGENT-GUARD: QindaTK's Gantt rows are canonical UUIDs (one id space with
// the SelectionModel), so a band's row is a name-based UUID of the grouping
// and its value — the same in the sheet, the Gantt and every rebuild, and
// never a task's GUID.
const QUuid kGroupNamespace(QStringLiteral("8f2b7c4e-1d3a-5e6f-9a0b-c1d2e3f4a5b6"));

QString groupRowId(const QString &groupBy, const QString &key)
{
    return QUuid::createUuidV5(kGroupNamespace, groupBy + QChar(0) + key).toString(QUuid::WithoutBraces);
}

} // namespace

void PlanService::rebuildGroups()
{
    // After the sort, the collapse and the filter (rebuildVisible).
    const Engine::Project &p = project();
    const Engine::Minutes reference = Engine::reportReference(p, m_today);
    m_display.clear();
    m_groups.clear();
    m_groupRows.clear();
    if (m_groupBy != QLatin1String("none")) {
        // Bands take every task the filter keeps: a summary's collapse
        // hides nothing here, summaries being left out of a grouped view.
        m_groups = Engine::groupTasks(p, m_filtered, m_groupBy, reference, m_locale);
        QSet<QString> keys;
        QList<int> shown;
        for (int g = 0; g < m_groups.size(); ++g) {
            const QString key = m_groups.at(g).key;
            keys.insert(key);
            m_groupRows.insert(groupRowId(m_groupBy, key), g);
            m_display.append({-1, g});
            if (!m_collapsedGroups.contains(key)) {
                for (int i : m_groups.at(g).tasks) {
                    m_display.append({i, g});
                    shown.append(i);
                }
            }
        }
        m_collapsedGroups.intersect(keys);
        m_visible = shown;
    } else {
        for (int i : std::as_const(m_visible)) {
            m_display.append({i, -1});
        }
    }
    m_highlighted.clear();
    if (m_highlight != QLatin1String("none")) {
        Engine::TaskFilter filter;
        filter.id = m_highlight;
        for (int i : std::as_const(m_visible)) {
            if (Engine::taskMatches(p, i, filter, reference)) {
                m_highlighted.insert(i);
            }
        }
    }
}

QStringList PlanService::displayRowIds() const
{
    QStringList out;
    out.reserve(m_display.size());
    for (const DisplayRow &r : m_display) {
        out.append(r.task >= 0 ? idOfRow(r.task) : groupRowId(m_groupBy, m_groups.at(r.group).key));
    }
    return out;
}

void PlanService::setGroupBy(const QString &id)
{
    if (id == m_groupBy || !Engine::isTaskGroupId(id)) {
        return;
    }
    m_groupBy = id;
    m_collapsedGroups.clear();
    rebuild();
    emit viewChanged();
}

QString PlanService::groupTitle() const
{
    return Engine::taskGroupTitle(project(), m_groupBy);
}

QVariantMap PlanService::setCustomGroup(const QString &field)
{
    const QString id = QStringLiteral("custom:") + field;
    if (!Engine::isTaskGroupId(id)) {
        return fail(QStringLiteral("Choose the custom field to group by."));
    }
    setGroupBy(id);
    return ok({{QStringLiteral("groups"), int(m_groups.size())}});
}

QVariant PlanService::groupHeader(const QString &rowId) const
{
    const auto at = m_groupRows.constFind(rowId);
    if (at == m_groupRows.constEnd()) {
        return {};
    }
    const Engine::TaskGroup &g = m_groups.at(*at);
    const Engine::Project &p = project();
    const QString &currency = p.info.currency;
    QVariantMap values{{QStringLiteral("work"), Engine::formatWork(g.work, m_locale)},
                       {QStringLiteral("cost"), Engine::formatMoney(g.cost, currency, m_locale)},
                       {QStringLiteral("percent"), m_locale.toString(g.percentComplete) + QLatin1Char('%')}};
    if (g.start != Engine::kNoDate) {
        values.insert(QStringLiteral("start"), Engine::formatDate(g.start, m_locale));
    }
    if (g.finish != Engine::kNoDate) {
        values.insert(QStringLiteral("finish"), Engine::formatDate(g.finish, m_locale));
        values.insert(QStringLiteral("duration"), Engine::formatSpan(g.duration, Engine::durationFormatOf(p.info), m_locale));
    }
    return QVariantMap{{QStringLiteral("label"), g.label},
                       {QStringLiteral("summary"), tr("%n task(s)", "", int(g.tasks.size()))},
                       {QStringLiteral("collapsed"), m_collapsedGroups.contains(g.key)},
                       {QStringLiteral("level"), 0},
                       {QStringLiteral("values"), values}};
}

void PlanService::setGroupCollapsed(const QString &groupId, bool collapsed)
{
    const auto at = m_groupRows.constFind(groupId);
    if (at == m_groupRows.constEnd() || m_collapsedGroups.contains(m_groups.at(*at).key) == collapsed) {
        return;
    }
    if (collapsed) {
        m_collapsedGroups.insert(m_groups.at(*at).key);
    } else {
        m_collapsedGroups.remove(m_groups.at(*at).key);
    }
    rebuild();
}

QVariantMap PlanService::groupGanttRow(const Engine::Project &p, int group) const
{
    const Engine::TaskGroup &g = m_groups.at(group);
    // The span from `p` (a moved preview copy has the same task indices).
    Engine::Minutes start = Engine::kNoDate;
    Engine::Minutes finish = Engine::kNoDate;
    Engine::Minutes durations = 0;
    Engine::Minutes weighted = 0;
    for (int i : g.tasks) {
        if (i < 0 || i >= p.tasks.size()) {
            continue;
        }
        const Engine::Task &t = p.tasks.at(i);
        if (t.start != Engine::kNoDate && (start == Engine::kNoDate || t.start < start)) {
            start = t.start;
        }
        if (t.finish != Engine::kNoDate && (finish == Engine::kNoDate || t.finish > finish)) {
            finish = t.finish;
        }
        durations += t.duration;
        weighted += t.duration * t.percentComplete;
    }
    // AGENT-NOTE: every Gantt row needs a start and a duration (QindaTK
    // D-303); a band of undated tasks sits at the project start, empty.
    if (start == Engine::kNoDate) {
        start = p.info.start;
        finish = p.info.start;
    }
    finish = std::max(start, finish == Engine::kNoDate ? start : finish);
    const Engine::WorkCalendar calendar(p, p.info.calendarUid);
    return {{QStringLiteral("taskId"), groupRowId(m_groupBy, g.key)},
            {QStringLiteral("label"), g.label},
            {QStringLiteral("kind"), QStringLiteral("group")},
            {QStringLiteral("startUtc"), utcOf(start)},
            {QStringLiteral("finishUtc"), utcOf(finish)},
            {QStringLiteral("durationMinutes"), qint64(std::max<Engine::Minutes>(0, calendar.workBetween(start, finish)))},
            {QStringLiteral("progress"), durations > 0 ? double(weighted) / double(durations) / 100.0 : 0.0},
            {QStringLiteral("collapsed"), m_collapsedGroups.contains(g.key)},
            {QStringLiteral("summaryText"), tr("%n task(s)", "", int(g.tasks.size()))},
            {QStringLiteral("dependencies"), QVariantList()},
            {QStringLiteral("outlineLevel"), 0}};
}

void PlanService::addPlanningBars(QVariantMap *row, const Engine::Project &p, int index) const
{
    const Engine::Task &t = p.tasks.at(index);
    if (t.deadline != Engine::kNoDate) {
        row->insert(QStringLiteral("deadlineUtc"), utcOf(t.deadline)); // Project shows deadlines always
    }
    if (t.summary || t.start == Engine::kNoDate) {
        return;
    }
    const Engine::WorkCalendar calendar(p, Engine::effectiveCalendarUid(p, index));
    // Detail Gantt: total slack to the late finish, free slack from the finish.
    if (m_showSlack && t.effectiveActive) {
        if (t.totalSlack > 0 && t.lateFinish != Engine::kNoDate && t.lateFinish > t.finish) {
            row->insert(QStringLiteral("slackFinishUtc"), utcOf(t.lateFinish));
        }
        if (t.freeSlack > 0 && t.finish != Engine::kNoDate) {
            const Engine::Minutes free = calendar.add(t.finish, t.freeSlack);
            if (free > t.finish) {
                row->insert(QStringLiteral("freeSlackFinishUtc"), utcOf(free));
            }
        }
    }
    // The progress point: as much of the task's own clock as % complete
    // says is done (tracking.h — the printout's progress line uses it).
    if (t.percentComplete > 0 && t.percentComplete < 100 && t.effectiveActive) {
        const Engine::Minutes reference = Engine::reportReference(p, m_today);
        const QList<Engine::ProgressPoint> point = Engine::progressLine(p, {index}, reference);
        const Engine::Minutes done = point.isEmpty() ? Engine::kNoDate : point.first().at;
        if (done != Engine::kNoDate && done >= t.start) {
            row->insert(QStringLiteral("progressUtc"), utcOf(done));
        }
    }
    // A split task's pieces, from its start (task_segments.h).
    if (!t.splits.isEmpty()) {
        const QList<Engine::TimeInterval> pieces = Engine::taskSegments(t, calendar);
        QVariantList segments;
        Engine::Minutes previous = t.start;
        for (const Engine::TimeInterval &piece : pieces) {
            if (piece.from < previous || piece.to < piece.from || segments.size() >= 256) {
                segments.clear(); // never a malformed list (it rejects every row)
                break;
            }
            segments.append(QVariantMap{{QStringLiteral("startUtc"), utcOf(piece.from)},
                                        {QStringLiteral("finishUtc"), utcOf(piece.to)}});
            previous = piece.to;
        }
        if (segments.size() > 1) {
            row->insert(QStringLiteral("segments"), segments);
        }
    }
    // The second baseline lane (Gantt Chart Format ▸ Second Baseline).
    if (m_showBaseline2 && m_ganttStyle != QLatin1String("leveling")) {
        const Engine::BaselineData &b = t.baselines[size_t(m_ganttBaseline2)];
        if (b.set && b.start != Engine::kNoDate && b.finish != Engine::kNoDate) {
            row->insert(QStringLiteral("baseline2StartUtc"), utcOf(b.start));
            row->insert(QStringLiteral("baseline2FinishUtc"), utcOf(std::max(b.start, b.finish)));
        }
    }
}

QVariantList PlanService::progressLines() const
{
    // On the status date, else today (the window's Today marker); the
    // printout draws its line only with a status date.
    if (!m_showProgressLines) {
        return {};
    }
    const Engine::Minutes at = Engine::reportReference(project(), m_today);
    return at == Engine::kNoDate ? QVariantList()
                                 : QVariantList{QVariantMap{{QStringLiteral("utc"), utcOf(at)},
                                                            {QStringLiteral("role"), QStringLiteral("danger")}}};
}

QVariantMap PlanService::applyHighlight(const QString &id)
{
    static const QStringList asking{QStringLiteral("resource"), QStringLiteral("dateRange"), QStringLiteral("custom")};
    if (id != QLatin1String("none") && (!Engine::taskFilterIds().contains(id) || asking.contains(id))) {
        return fail(QStringLiteral("There is no highlight called “%1”.").arg(id));
    }
    m_highlight = id;
    rebuild();
    emit viewChanged();
    return ok({{QStringLiteral("rows"), int(m_highlighted.size())}});
}

void PlanService::setHighlightFill(const QColor &fill)
{
    if (fill != m_highlightFill) {
        m_highlightFill = fill;
        rebuildSheets();
        emit viewChanged();
    }
}

void PlanService::setShowBaseline2(bool on)
{
    if (on != m_showBaseline2) {
        m_showBaseline2 = on;
        rebuildGantt();
        emit viewChanged();
    }
}

void PlanService::setGanttBaseline2(int number)
{
    number = std::clamp(number, 0, Engine::kBaselineCount - 1);
    if (number != m_ganttBaseline2) {
        m_ganttBaseline2 = number;
        rebuildGantt();
        emit viewChanged();
    }
}

} // namespace QindaOffice::Apps::Plan
