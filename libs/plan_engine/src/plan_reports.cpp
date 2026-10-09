// SPDX-License-Identifier: GPL-3.0-or-later
#include "plan_reports.h"
#include "plan_reports_p.h"

#include "baselines.h"
#include "plan_edit.h"
#include "plan_time.h"
#include "scheduler.h"
#include "work_calendar.h"

#include <algorithm>

namespace QindaOffice::Plan {

namespace {

struct Totals {
    int tasks = 0;
    int complete = 0;
    int critical = 0;
    int late = 0;
    int slipped = 0;
    int milestones = 0;
    Minutes duration = 0;
    Minutes weighted = 0;
    Minutes work = 0;
    qint64 cost = 0;
    qint64 baselineCost = 0;
    bool baseline = false;
    Minutes baselineFinish = kNoDate;
};

Totals totalsOf(const Project &project, Minutes reference)
{
    Totals t;
    for (const Task &task : project.tasks) {
        if (task.baselines[0].set) {
            t.baseline = true;
            t.baselineFinish = std::max(t.baselineFinish, task.baselines[0].finish);
            if (task.parentIndex < 0) {
                t.baselineCost += task.baselines[0].cost;
            }
        }
        if (task.parentIndex < 0 && task.effectiveActive) {
            t.cost += task.cost;
            t.work += task.work;
        }
        if (!task.effectiveActive || task.summary) {
            continue;
        }
        ++t.tasks;
        t.complete += task.percentComplete >= 100 ? 1 : 0;
        t.critical += task.critical ? 1 : 0;
        t.milestones += task.isMilestone() ? 1 : 0;
        t.late += isLate(project, task, reference) ? 1 : 0;
        t.slipped += isSlipped(project, task) ? 1 : 0;
        t.duration += task.duration;
        t.weighted += task.duration * task.percentComplete;
    }
    return t;
}

int percentOf(const Totals &t)
{
    return t.duration > 0 ? int(divRound(t.weighted, t.duration)) : (t.tasks > 0 ? 100 * t.complete / t.tasks : 0);
}

ReportTable taskTable(const QString &id, const QString &title, const QStringList &fields, const QString &emptyText)
{
    ReportTable table;
    table.id = id;
    table.title = title;
    table.emptyText = emptyText;
    for (int c = 0; c < fields.size(); ++c) {
        table.columns.append(taskFieldTitle(fields.at(c)));
        const QString &f = fields.at(c);
        if (f == QLatin1String("id") || f == QLatin1String("duration") || f == QLatin1String("percent")
            || f == QLatin1String("totalSlack") || f == QLatin1String("cost") || f == QLatin1String("finishVariance")
            || f == QLatin1String("work")) {
            table.numericColumns.append(c);
        }
    }
    return table;
}

void addTaskRow(ReportTable &table, const Project &project, int index, const QStringList &fields, const QLocale &locale,
                const QString &tone = {})
{
    QStringList row;
    for (const QString &f : fields) {
        row.append(taskFieldText(project, index, f, locale));
    }
    table.rows.append(row);
    table.rowTaskUids.append(project.tasks.at(index).uid);
    table.rowTones.append(tone);
}

PlanReport overview(const Project &project, Minutes reference, const QLocale &locale)
{
    PlanReport r;
    const Totals t = totalsOf(project, reference);
    const DurationFormat fmt = durationFormatOf(project.info);
    const QString &cur = project.info.currency;
    const WorkCalendar cal(project, project.info.calendarUid);
    const auto kpi = [&](const char *id, const QString &label, const QString &value, const QString &detail,
                         int percent = -1, const QString &tone = {}) {
        r.kpis.append({QString::fromLatin1(id), label, value, detail, percent, tone});
    };
    kpi("start", QStringLiteral("Start"), formatDate(project.info.start, locale), QString());
    QString finishDetail;
    QString finishTone;
    if (t.baseline) {
        const Minutes slip = cal.workBetween(t.baselineFinish, project.finish);
        finishDetail = slip == 0 ? QStringLiteral("On the baseline")
                                 : QStringLiteral("%1 %2 the baseline")
                                       .arg(formatSpan(std::abs(slip), fmt, locale),
                                            slip > 0 ? QStringLiteral("behind") : QStringLiteral("ahead of"));
        finishTone = slip > 0 ? QStringLiteral("danger") : QStringLiteral("good");
    }
    kpi("finish", QStringLiteral("Finish"), formatDate(project.finish, locale), finishDetail, -1, finishTone);
    kpi("complete", QStringLiteral("Complete"), locale.toString(percentOf(t)) + QLatin1Char('%'),
        QStringLiteral("%1 of %2 tasks done").arg(t.complete).arg(t.tasks), percentOf(t));
    QString costDetail;
    QString costTone;
    if (t.baseline) {
        const qint64 over = t.cost - t.baselineCost;
        costDetail = over == 0 ? QStringLiteral("On budget")
                               : QStringLiteral("%1 %2 budget")
                                     .arg(formatMoney(std::abs(over), cur, locale),
                                          over > 0 ? QStringLiteral("over") : QStringLiteral("under"));
        costTone = over > 0 ? QStringLiteral("warning") : QStringLiteral("good");
    }
    kpi("cost", QStringLiteral("Cost"), formatMoney(t.cost, cur, locale), costDetail, -1, costTone);
    kpi("late", QStringLiteral("Late tasks"), locale.toString(t.late),
        t.slipped > 0 ? QStringLiteral("as of %1 · %2 slipped").arg(formatDate(reference, locale)).arg(t.slipped)
                      : QStringLiteral("as of %1").arg(formatDate(reference, locale)),
        -1, t.late > 0 ? QStringLiteral("danger") : QString());
    kpi("critical", QStringLiteral("Critical tasks"), locale.toString(t.critical), QString());

    ReportTable phases;
    phases.id = QStringLiteral("phases");
    phases.title = QStringLiteral("Phases");
    phases.columns = {QStringLiteral("Name"), QStringLiteral("Start"), QStringLiteral("Finish"), QStringLiteral("% Complete"),
                      QStringLiteral("Cost")};
    phases.numericColumns = {3, 4};
    phases.emptyText = QStringLiteral("The plan has no summary tasks.");
    for (int i = 0; i < project.tasks.size(); ++i) {
        const Task &task = project.tasks.at(i);
        if (task.parentIndex >= 0 || !task.summary || !task.effectiveActive) {
            continue;
        }
        phases.rows.append({task.name, formatDate(task.start, locale), formatDate(task.finish, locale),
                            locale.toString(task.percentComplete) + QLatin1Char('%'), formatMoney(task.cost, cur, locale)});
        phases.rowTaskUids.append(task.uid);
        phases.rowTones.append(task.critical ? QStringLiteral("danger") : QString());
    }
    r.tables.append(phases);

    ReportTable milestones;
    milestones.id = QStringLiteral("milestones");
    milestones.title = QStringLiteral("Milestones");
    milestones.columns = {QStringLiteral("Milestone"), QStringLiteral("Finish"), QStringLiteral("Baseline"),
                          QStringLiteral("Status")};
    milestones.emptyText = QStringLiteral("The plan has no milestones.");
    for (int i = 0; i < project.tasks.size(); ++i) {
        const Task &task = project.tasks.at(i);
        if (task.summary || !task.effectiveActive || !task.isMilestone()) {
            continue;
        }
        QString status = QStringLiteral("Upcoming");
        QString tone;
        const Variance v = variance(project, task, 0);
        if (task.percentComplete >= 100) {
            status = QStringLiteral("Complete");
            tone = QStringLiteral("good");
        } else if (task.finish < reference) {
            status = QStringLiteral("Overdue");
            tone = QStringLiteral("danger");
        } else if (v.valid && v.finish > 0) {
            status = QStringLiteral("Slipped %1").arg(formatSpan(v.finish, fmt, locale));
            tone = QStringLiteral("warning");
        }
        milestones.rows.append({task.name, formatDate(task.finish, locale),
                                task.baselines[0].set ? formatDate(task.baselines[0].finish, locale) : QStringLiteral("NA"),
                                status});
        milestones.rowTaskUids.append(task.uid);
        milestones.rowTones.append(tone);
    }
    r.tables.append(milestones);

    const QStringList lateFields{QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("finish"),
                                 QStringLiteral("percent"), QStringLiteral("resources")};
    ReportTable late = taskTable(QStringLiteral("late"), QStringLiteral("Late tasks"), lateFields,
                                 QStringLiteral("Nothing is behind schedule."));
    for (int i = 0; i < project.tasks.size(); ++i) {
        if (isLate(project, project.tasks.at(i), reference)) {
            addTaskRow(late, project, i, lateFields, locale, QStringLiteral("danger"));
        }
    }
    r.tables.append(late);
    return r;
}

} // namespace

QStringList reportIds()
{
    return {QStringLiteral("overview"), QStringLiteral("critical"), QStringLiteral("resources"), QStringLiteral("late"),
            QStringLiteral("cost"),     QStringLiteral("work")};
}

QString reportTitle(const QString &id)
{
    if (id == QLatin1String("cost")) {
        return QStringLiteral("Cost Overview");
    }
    if (id == QLatin1String("work")) {
        return QStringLiteral("Work Overview");
    }
    if (id == QLatin1String("critical")) {
        return QStringLiteral("Critical Tasks");
    }
    if (id == QLatin1String("resources")) {
        return QStringLiteral("Resource Overview");
    }
    if (id == QLatin1String("late")) {
        return QStringLiteral("Late Tasks");
    }
    return QStringLiteral("Project Overview");
}

Minutes reportReference(const Project &project, const QDate &today)
{
    return project.info.statusDate != kNoDate ? project.info.statusDate : toMinutes(today, project.info.defaultFinishMinute);
}

bool isSlipped(const Project &project, const Task &task)
{
    if (task.summary || !task.effectiveActive || task.percentComplete >= 100) {
        return false;
    }
    const Variance v = variance(project, task, 0);
    return v.valid && v.finish > 0;
}

bool isLate(const Project &project, const Task &task, Minutes reference)
{
    if (task.summary || !task.effectiveActive || task.percentComplete >= 100 || task.start == kNoDate) {
        return false;
    }
    if (task.start >= reference) {
        return false;
    }
    if (task.duration == 0) {
        return task.finish < reference;
    }
    const WorkCalendar cal(project, effectiveCalendarUid(project, project.taskIndex(task.uid)));
    const Minutes should = std::min(task.duration, cal.workBetween(task.start, reference));
    return should * 100 > Minutes(task.percentComplete) * task.duration;
}

PlanReport buildReport(const Project &project, const QString &id, const QDate &today, const QLocale &locale)
{
    const Minutes reference = reportReference(project, today);
    PlanReport r;
    const QString known = reportIds().contains(id) ? id : QStringLiteral("overview");
    if (known == QLatin1String("overview")) {
        r = overview(project, reference, locale);
    } else if (known == QLatin1String("cost")) {
        r = Reports::costOverview(project, locale); // M79
    } else if (known == QLatin1String("work")) {
        r = Reports::workOverview(project, locale); // M79
    } else if (known == QLatin1String("critical") || known == QLatin1String("late")) {
        const bool critical = known == QLatin1String("critical");
        const QStringList fields = critical
            ? QStringList{QStringLiteral("id"),     QStringLiteral("name"),       QStringLiteral("duration"),
                          QStringLiteral("start"),  QStringLiteral("finish"),     QStringLiteral("totalSlack"),
                          QStringLiteral("percent"), QStringLiteral("resources")}
            : QStringList{QStringLiteral("id"),      QStringLiteral("name"),    QStringLiteral("start"),
                          QStringLiteral("finish"),  QStringLiteral("baselineFinish"), QStringLiteral("finishVariance"),
                          QStringLiteral("percent"), QStringLiteral("resources")};
        ReportTable table = taskTable(known, critical ? QStringLiteral("Unfinished critical tasks") : QStringLiteral("Late and slipped tasks"),
                                      fields,
                                      critical ? QStringLiteral("No unfinished task is critical.")
                                               : QStringLiteral("Nothing is behind schedule."));
        if (!critical) {
            table.columns.append(QStringLiteral("Status"));
        }
        int lateCount = 0;
        for (int i = 0; i < project.tasks.size(); ++i) {
            const Task &t = project.tasks.at(i);
            if (critical) {
                if (!t.summary && t.effectiveActive && t.critical && t.percentComplete < 100) {
                    addTaskRow(table, project, i, fields, locale);
                }
                continue;
            }
            const bool late = isLate(project, t, reference);
            if (!late && !isSlipped(project, t)) {
                continue;
            }
            lateCount += late ? 1 : 0;
            addTaskRow(table, project, i, fields, locale, late ? QStringLiteral("danger") : QStringLiteral("warning"));
            table.rows.last().append(late ? QStringLiteral("Behind") : QStringLiteral("Slipped"));
        }
        const int count = critical ? int(table.rows.size()) : lateCount;
        r.kpis.append({QStringLiteral("count"), critical ? QStringLiteral("Critical tasks") : QStringLiteral("Late tasks"),
                       locale.toString(count),
                       critical ? QString() : QStringLiteral("%1 more slipped past their baseline").arg(table.rows.size() - count),
                       -1, count == 0 ? QStringLiteral("good") : QStringLiteral("danger")});
        r.kpis.append({QStringLiteral("finish"), QStringLiteral("Finish"), formatDate(project.finish, locale), QString(), -1, {}});
        r.tables.append(table);
    } else {
        ReportTable table;
        table.id = QStringLiteral("resources");
        table.title = QStringLiteral("Resources");
        table.columns = {QStringLiteral("Resource"), QStringLiteral("Type"), QStringLiteral("Max. Units"),
                         QStringLiteral("Work"),     QStringLiteral("Cost"), QStringLiteral("Status")};
        table.numericColumns = {2, 3, 4};
        table.emptyText = QStringLiteral("The plan has no resources.");
        int over = 0;
        qint64 cost = 0;
        for (int i = 0; i < project.resources.size(); ++i) {
            const Resource &res = project.resources.at(i);
            over += res.overallocated ? 1 : 0;
            cost += res.cost;
            table.rows.append({res.name, resourceFieldText(project, i, QStringLiteral("type"), locale),
                               resourceFieldText(project, i, QStringLiteral("maxUnits"), locale),
                               resourceFieldText(project, i, QStringLiteral("work"), locale),
                               formatMoney(res.cost, project.info.currency, locale),
                               res.overallocated ? QStringLiteral("Overallocated") : QStringLiteral("OK")});
            table.rowTaskUids.append(0);
            table.rowTones.append(res.overallocated ? QStringLiteral("danger") : QString());
        }
        r.kpis.append({QStringLiteral("resources"), QStringLiteral("Resources"), locale.toString(project.resources.size()),
                       QString(), -1, {}});
        r.kpis.append({QStringLiteral("overallocated"), QStringLiteral("Overallocated"), locale.toString(over), QString(), -1,
                       over > 0 ? QStringLiteral("danger") : QStringLiteral("good")});
        r.kpis.append({QStringLiteral("cost"), QStringLiteral("Resource cost"), formatMoney(cost, project.info.currency, locale),
                       QString(), -1, {}});
        r.tables.append(table);
    }
    r.id = known;
    r.title = reportTitle(known);
    r.subtitle = QStringLiteral("%1 · %2 – %3 · as of %4")
                     .arg(project.info.title.isEmpty() ? QStringLiteral("Untitled project") : project.info.title,
                          formatDate(project.info.start, locale), formatDate(project.finish, locale),
                          formatDate(reference, locale));
    return r;
}

} // namespace QindaOffice::Plan
