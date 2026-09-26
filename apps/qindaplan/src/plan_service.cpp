// SPDX-License-Identifier: GPL-3.0-or-later
// PlanService: models, selection and view state (plan_service.h).
#include "plan_service.h"

#include "outline.h"
#include "plan_document.h"
#include "plan_edit.h"
#include "plan_gantt_adapter.h"
#include "plan_reports.h"
#include "plan_time.h"
#include "sheet_sources.h"
#include "work_calendar.h"

#include <QUndoStack>

#include <algorithm>

namespace QindaOffice::Apps::Plan {

PlanService::PlanService(PlanDocument *document, QObject *parent)
    : QObject(parent)
    , m_document(document)
    , m_today(QDate::currentDate())
{
    m_ganttCalendar = new QindaTK::GanttCalendar(this);
    m_ganttCalendar->setTimeZoneId(QStringLiteral("UTC"));
    m_adapter = new PlanGanttAdapter(this);
    m_gantt = new QindaTK::GanttTaskModel(this);
    m_gantt->setCalendar(m_ganttCalendar);
    m_gantt->setTransactionAdapter(m_adapter);
    m_selection = new QindaTK::SelectionModel(this);
    m_sheetSource = new TaskSheetSource(this);
    m_sheet = new QindaTK::EditableGridModel(this);
    m_sheet->setSourceCommits(true);
    m_sheet->setSource(m_sheetSource);
    m_resourceSource = new ResourceSheetSource(this);
    m_resourceSheet = new QindaTK::EditableGridModel(this);
    m_resourceSheet->setSourceCommits(true);
    m_resourceSheet->setSource(m_resourceSource);
    QStringList resourceFields;
    for (const QVariant &c : resourceColumns()) {
        resourceFields.append(c.toMap().value(QStringLiteral("id")).toString());
    }
    m_resourceSheet->setColumnIds(resourceFields);
    m_usage = new QindaTK::ResourcePlanModel(this);
    connect(m_selection, &QindaTK::SelectionModel::selectionModified, this,
            [this](const QStringList &ids, const QString &current) {
                if (m_syncingSelection) {
                    return;
                }
                QStringList ordered = ids;
                ordered.removeAll(current);
                if (!current.isEmpty()) {
                    ordered.prepend(current);
                }
                if (ordered != m_selected) {
                    m_selected = ordered;
                    emit selectionChanged();
                }
            });
    connect(m_document, &PlanDocument::projectChanged, this, &PlanService::rebuild);
    rebuild();
}

PlanService::~PlanService() = default;

const Engine::Project &PlanService::project() const
{
    return m_document->project();
}

void PlanService::setLocale(const QLocale &locale)
{
    m_locale = locale;
    rebuild();
}

void PlanService::setToday(const QString &iso)
{
    const QDate d = QDate::fromString(iso, Qt::ISODate);
    if (d.isValid() && d != m_today) {
        m_today = d;
        rebuild();
    }
}

QString PlanService::title() const
{
    const QString t = project().info.title;
    return t.isEmpty() ? m_document->displayTitle() : t;
}

int PlanService::taskCount() const
{
    return int(project().tasks.size());
}

int PlanService::resourceCount() const
{
    return int(project().resources.size());
}

QVariantMap PlanService::summary() const
{
    const Engine::Project &p = project();
    const Engine::PlanReport r = Engine::buildReport(p, QStringLiteral("overview"), m_today, m_locale);
    QVariantMap out{{QStringLiteral("start"), Engine::formatDate(p.info.start, m_locale)},
                    {QStringLiteral("finish"), Engine::formatDate(p.finish, m_locale)},
                    {QStringLiteral("tasks"), taskCount()}};
    for (const Engine::ReportKpi &k : r.kpis) {
        out.insert(k.id, k.value);
        out.insert(k.id + QStringLiteral("Detail"), k.detail);
    }
    return out;
}

QVariantList PlanService::issues() const
{
    QVariantList out;
    for (const Engine::ScheduleIssue &i : project().issues) {
        const int row = project().taskIndex(i.taskUid);
        out.append(QVariantMap{{QStringLiteral("taskId"), row >= 0 ? idOfRow(row) : QString()},
                               {QStringLiteral("task"), row >= 0 ? project().tasks.at(row).name : QString()},
                               {QStringLiteral("message"), i.message}});
    }
    return out;
}

void PlanService::setTimescale(const QString &timescale)
{
    static const QStringList known{QStringLiteral("days"), QStringLiteral("weeks"), QStringLiteral("months")};
    if (timescale != m_timescale && known.contains(timescale)) {
        m_timescale = timescale;
        emit viewChanged();
    }
}

double PlanService::dayScale() const
{
    return m_timescale == QLatin1String("months") ? 0.22 : m_timescale == QLatin1String("weeks") ? 0.6 : 1.3;
}

void PlanService::setShowCritical(bool on)
{
    if (on != m_showCritical) {
        m_showCritical = on;
        rebuildGantt();
        emit viewChanged();
    }
}

void PlanService::setShowBaseline(bool on)
{
    if (on != m_showBaseline) {
        m_showBaseline = on;
        rebuildGantt();
        emit viewChanged();
    }
}

void PlanService::setShowBarText(bool on)
{
    if (on != m_showBarText) {
        m_showBarText = on;
        rebuildGantt();
        emit viewChanged();
    }
}

void PlanService::setUsageWeeks(bool on)
{
    if (on != m_usageWeeks) {
        m_usageWeeks = on;
        rebuildUsage();
        emit viewChanged();
        emit changed();
    }
}

QString PlanService::ganttStartUtc() const
{
    // Two days of air before the project starts, from midnight.
    const Engine::Minutes start = project().info.start;
    return utcOf(Engine::toMinutes(Engine::dateOf(start).addDays(-2), 0));
}

QString PlanService::projectStartIso() const
{
    return Engine::dateOf(project().info.start).toString(Qt::ISODate);
}

QVariantList PlanService::markers() const
{
    QVariantList out;
    out.append(QVariantMap{{QStringLiteral("utc"), utcOf(Engine::toMinutes(m_today, 12 * 60))},
                           {QStringLiteral("label"), QStringLiteral("Today")},
                           {QStringLiteral("role"), QStringLiteral("accent")}});
    if (project().info.statusDate != Engine::kNoDate) {
        out.append(QVariantMap{{QStringLiteral("utc"), utcOf(project().info.statusDate)},
                               {QStringLiteral("label"), QStringLiteral("Status")},
                               {QStringLiteral("role"), QStringLiteral("danger")}});
    }
    return out;
}

QVariantList PlanService::calendarSources() const
{
    // AGENT-NOTE: calendar colours are document data in CalendarGridView;
    // empty colours take the theme's accent/success/warning/danger/info in
    // this order, so the order below picks the roles.
    return {QVariantMap{{QStringLiteral("id"), QStringLiteral("task")}, {QStringLiteral("name"), QStringLiteral("Tasks")},
                        {QStringLiteral("color"), QString()}, {QStringLiteral("visible"), true}},
            QVariantMap{{QStringLiteral("id"), QStringLiteral("done")}, {QStringLiteral("name"), QStringLiteral("Complete")},
                        {QStringLiteral("color"), QString()}, {QStringLiteral("visible"), true}},
            QVariantMap{{QStringLiteral("id"), QStringLiteral("milestone")}, {QStringLiteral("name"), QStringLiteral("Milestones")},
                        {QStringLiteral("color"), QString()}, {QStringLiteral("visible"), true}},
            QVariantMap{{QStringLiteral("id"), QStringLiteral("critical")}, {QStringLiteral("name"), QStringLiteral("Critical")},
                        {QStringLiteral("color"), QString()}, {QStringLiteral("visible"), true}}};
}

QString PlanService::currentTaskId() const
{
    return m_selected.isEmpty() ? QString() : m_selected.first();
}

void PlanService::setCurrentTaskId(const QString &taskId)
{
    setSelectedTaskIds(taskId.isEmpty() ? QStringList() : QStringList{taskId});
}

void PlanService::setSelectedTaskIds(const QStringList &taskIds)
{
    QStringList valid;
    for (const QString &id : taskIds) {
        if (m_rowByGuid.contains(id) && !valid.contains(id)) {
            valid.append(id);
        }
    }
    if (valid == m_selected) {
        return;
    }
    m_selected = valid;
    // The toolkit SelectionModel carries the current task (what the Gantt
    // highlights); the multi-selection lives here.
    m_syncingSelection = true;
    if (valid.isEmpty()) {
        m_selection->clear();
    } else {
        m_selection->select(valid.first());
    }
    m_syncingSelection = false;
    emit selectionChanged();
}

QVariantMap PlanService::currentTask() const
{
    const int row = rowOf(currentTaskId());
    if (row < 0) {
        return {};
    }
    const Engine::Task &t = project().tasks.at(row);
    return {{QStringLiteral("taskId"), currentTaskId()},
            {QStringLiteral("id"), t.id},
            {QStringLiteral("name"), t.name},
            {QStringLiteral("summary"), t.summary},
            {QStringLiteral("milestone"), t.isMilestone()},
            {QStringLiteral("manual"), t.manual},
            {QStringLiteral("active"), t.active},
            {QStringLiteral("critical"), t.critical},
            {QStringLiteral("collapsed"), m_collapsed.contains(t.uid)},
            {QStringLiteral("percent"), t.percentComplete},
            {QStringLiteral("startUtc"), utcOf(t.start)},
            {QStringLiteral("finishUtc"), utcOf(t.finish)}};
}

void PlanService::setCurrentResourceUid(int uid)
{
    if (uid != m_currentResource) {
        m_currentResource = uid;
        emit selectionChanged();
    }
}

QList<int> PlanService::selectedRows() const
{
    QList<int> rows;
    for (const QString &id : m_selected) {
        const int r = rowOf(id);
        if (r >= 0) {
            rows.append(r);
        }
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

int PlanService::rowOf(const QString &taskId) const
{
    return m_rowByGuid.value(taskId, -1);
}

QString PlanService::idOfRow(int row) const
{
    return row >= 0 && row < project().tasks.size() ? project().tasks.at(row).guid.toString(QUuid::WithoutBraces) : QString();
}

QVariantMap PlanService::apply(const QString &label, const std::function<QString(Engine::Project &)> &operation)
{
    const QString refused = m_document->apply(label, operation);
    return refused.isEmpty() ? ok() : fail(refused);
}

QVariantMap PlanService::ok(const QVariantMap &extra)
{
    QVariantMap out = extra;
    out.insert(QStringLiteral("ok"), true);
    return out;
}

QVariantMap PlanService::fail(const QString &error)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
}

QString PlanService::utcOf(Engine::Minutes instant)
{
    return instant == Engine::kNoDate ? QString() : QString::number(instant * 60000);
}

Engine::Minutes PlanService::minutesOfUtc(const QString &utc)
{
    bool okNumber = false;
    const qint64 ms = utc.toLongLong(&okNumber);
    if (!okNumber) {
        return Engine::kNoDate;
    }
    return ms >= 0 ? ms / 60000 : -((-ms + 59999) / 60000);
}

QString PlanService::formatUtc(const QString &utc) const
{
    return Engine::formatDateTime(minutesOfUtc(utc), m_locale);
}

void PlanService::rebuild()
{
    m_rowByGuid.clear();
    const Engine::Project &p = project();
    for (int i = 0; i < p.tasks.size(); ++i) {
        m_rowByGuid.insert(p.tasks.at(i).guid.toString(QUuid::WithoutBraces), i);
    }
    // Forget collapsed summaries that are gone or no longer summaries.
    QSet<int> summaries;
    for (const Engine::Task &t : p.tasks) {
        if (t.summary) {
            summaries.insert(t.uid);
        }
    }
    m_collapsed.intersect(summaries);
    rebuildVisible();
    pruneSelection();
    rebuildGanttCalendar();
    rebuildSheets();
    rebuildGantt();
    rebuildCalendar();
    rebuildUsage();
    ++m_revision;
    emit changed();
}

void PlanService::rebuildVisible()
{
    m_visible.clear();
    const Engine::Project &p = project();
    for (int i = 0; i < p.tasks.size();) {
        m_visible.append(i);
        const Engine::Task &t = p.tasks.at(i);
        i = t.summary && m_collapsed.contains(t.uid) ? Engine::subtreeEnd(p, i) : i + 1;
    }
}

void PlanService::pruneSelection()
{
    QStringList kept;
    for (const QString &id : std::as_const(m_selected)) {
        const int row = rowOf(id);
        if (row >= 0 && m_visible.contains(row)) {
            kept.append(id);
        }
    }
    if (kept != m_selected) {
        m_selected = kept;
        if (kept.isEmpty()) {
            m_selection->clear();
        }
        emit selectionChanged();
    }
}

void PlanService::rebuildGanttCalendar()
{
    // The Gantt shades what the project calendar does not work: weekdays
    // from its week, holidays from its whole-day exceptions near the plan.
    const Engine::Project &p = project();
    const Engine::WorkCalendar cal(p, p.info.calendarUid);
    int mask = 0;
    for (int d = 0; d < 7; ++d) {
        if (cal.weekMinutes(d) > 0) {
            mask |= 1 << d;
        }
    }
    QStringList holidays;
    const QDate from = Engine::dateOf(p.info.start).addDays(-60);
    const QDate to = Engine::dateOf(p.finish).addDays(120);
    for (QDate d = from; d.isValid() && d <= to; d = d.addDays(1)) {
        if (d.dayOfWeek() <= 7 && ((mask >> (d.dayOfWeek() - 1)) & 1) && cal.minutesOn(d) == 0) {
            holidays.append(d.toString(Qt::ISODate));
        }
    }
    m_ganttCalendar->setWorkingDays(mask == 0 ? 0x1f : mask);
    m_ganttCalendar->setHolidays(holidays);
}

void PlanService::rebuildSheets()
{
    QStringList rows;
    for (int i : std::as_const(m_visible)) {
        rows.append(idOfRow(i));
    }
    rows.append(QStringLiteral("new"));
    if (m_sheet->columns() == 0) {
        m_sheet->setColumnIds(tableFields());
    }
    // AGENT-GUARD: only a different row set may reset the grid (a reset
    // loses the edit cursor); plain value changes are a source notify.
    if (rows != m_sheetRows) {
        m_sheetRows = rows;
        m_sheet->setRowIds(rows);
    }
    m_sheet->notifySourceChanged(0, int(rows.size()), 0, m_sheet->columns());
    QStringList resources;
    for (const Engine::Resource &r : project().resources) {
        resources.append(QStringLiteral("r%1").arg(r.uid));
    }
    resources.append(QStringLiteral("new"));
    if (resources != m_resourceRows) {
        m_resourceRows = resources;
        m_resourceSheet->setRowIds(resources);
    }
    m_resourceSheet->notifySourceChanged(0, int(resources.size()), 0, m_resourceSheet->columns());
}

} // namespace QindaOffice::Apps::Plan
