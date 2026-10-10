// SPDX-License-Identifier: GPL-3.0-or-later
// PlanService (M83, D-092): Split Task, Reschedule Uncompleted Work,
// recurring tasks, the Excel/CSV task import, recurring calendar exceptions
// and work weeks made here, and the progress line's switch
// (plan_service.h). Every edit is one undo step through apply().
#include "calendar_edit.h"
#include "custom_fields.h"
#include "plan_calendar_text.h"
#include "plan_document.h"
#include "plan_edit.h"
#include "plan_import.h"
#include "plan_import_xlsx.h"
#include "plan_json.h"
#include "plan_service.h"
#include "plan_time.h"
#include "recurring_tasks.h"
#include "tracking.h"

#include <QFile>
#include <QFileInfo>

#include <numeric>

namespace QindaOffice::Apps::Plan {

namespace {

using Kind = Engine::RecurringException::Kind;

QString errorOf(const Engine::EditResult &r)
{
    return r.ok ? QString() : r.error;
}

// A recurrence as the dialogs' fields (plan_service.h lists the keys).
QVariantMap patternFields(const Engine::RecurringException &rx)
{
    const bool byPosition = rx.kind == Kind::MonthlyByPosition || rx.kind == Kind::YearlyByPosition;
    const QString frequency = rx.kind == Kind::EveryDays ? QStringLiteral("daily")
                              : rx.kind == Kind::Weekly  ? QStringLiteral("weekly")
                              : rx.kind == Kind::MonthlyByDay || rx.kind == Kind::MonthlyByPosition ? QStringLiteral("monthly")
                                                                                                     : QStringLiteral("yearly");
    return {{QStringLiteral("frequency"), frequency}, {QStringLiteral("every"), rx.every},
            {QStringLiteral("days"), rx.daysOfWeek},  {QStringLiteral("byPosition"), byPosition},
            {QStringLiteral("day"), rx.day},          {QStringLiteral("position"), rx.position},
            {QStringLiteral("weekday"), rx.weekday},  {QStringLiteral("month"), rx.month},
            {QStringLiteral("name"), rx.name},        {QStringLiteral("from"), rx.from.toString(Qt::ISODate)},
            {QStringLiteral("to"), rx.to.toString(Qt::ISODate)}};
}

std::optional<QDate> dateField(const QVariantMap &f, const char *key, const QLocale &locale)
{
    const QString text = f.value(QLatin1String(key)).toString().trimmed();
    const std::optional<Engine::Minutes> m = text.isEmpty() ? std::nullopt : Engine::parseDate(text, 0, locale);
    return m ? std::optional<QDate>(Engine::dateOf(*m)) : std::nullopt;
}

std::optional<Engine::RecurringException> patternOf(const QVariantMap &f, const QLocale &locale, QString *error)
{
    Engine::RecurringException rx;
    const QString frequency = f.value(QStringLiteral("frequency")).toString();
    const bool byPosition = f.value(QStringLiteral("byPosition")).toBool();
    rx.kind = frequency == QLatin1String("daily")     ? Kind::EveryDays
              : frequency == QLatin1String("weekly")  ? Kind::Weekly
              : frequency == QLatin1String("monthly") ? (byPosition ? Kind::MonthlyByPosition : Kind::MonthlyByDay)
                                                      : (byPosition ? Kind::YearlyByPosition : Kind::YearlyByDay);
    rx.name = f.value(QStringLiteral("name")).toString().trimmed();
    rx.every = std::clamp(f.value(QStringLiteral("every")).toInt(), 1, 999);
    rx.daysOfWeek = f.value(QStringLiteral("days")).toInt() & 0x7F;
    rx.day = std::clamp(f.value(QStringLiteral("day")).toInt(), 1, 31);
    rx.position = std::clamp(f.value(QStringLiteral("position")).toInt(), 0, 4);
    rx.weekday = std::clamp(f.value(QStringLiteral("weekday")).toInt(), 1, 7);
    rx.month = std::clamp(f.value(QStringLiteral("month")).toInt(), 1, 12);
    const std::optional<QDate> from = dateField(f, "from", locale);
    if (!from) {
        *error = QStringLiteral("Give the recurrence a first day.");
        return std::nullopt;
    }
    rx.from = *from;
    rx.to = dateField(f, "to", locale).value_or(QDate());
    if (rx.kind == Kind::Weekly && rx.daysOfWeek == 0) {
        *error = QStringLiteral("Pick the days of the week it falls on.");
        return std::nullopt;
    }
    return rx;
}

std::optional<Engine::ImportTable> readTable(const QUrl &url, QString *error)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    Engine::ImportTable table;
    if (QFileInfo(path).suffix().compare(QLatin1String("xlsx"), Qt::CaseInsensitive) == 0) {
        if (!Engine::readTaskTableXlsx(path, &table, error)) {
            return std::nullopt;
        }
        return table;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return std::nullopt;
    }
    return Engine::parseCsvTable(file.readAll());
}

} // namespace

void PlanService::setShowProgressLines(bool on)
{
    if (on != m_showProgressLines) {
        m_showProgressLines = on;
        emit viewChanged();
    }
}

QVariantMap PlanService::splitCurrentTask(const QString &stop, const QString &resume)
{
    const int row = rowOf(currentTaskId());
    const std::optional<Engine::Minutes> from = Engine::parseDate(stop, project().info.defaultFinishMinute, m_locale);
    const std::optional<Engine::Minutes> to = Engine::parseDate(resume, project().info.defaultStartMinute, m_locale);
    if (row < 0 || !from || !to) {
        return fail(QStringLiteral("Give the date the work stops and the date it resumes."));
    }
    return apply(QStringLiteral("Split Task"), [&](Engine::Project &p) { return errorOf(Engine::splitTask(p, row, *from, *to)); });
}

QVariantMap PlanService::removeSplitsSelected()
{
    const QList<int> rows = selectedRows();
    return apply(QStringLiteral("Remove Splits"), [&](Engine::Project &p) {
        return Engine::removeSplits(p, rows) > 0 ? QString() : QStringLiteral("The selected tasks have no splits.");
    });
}

QVariantMap PlanService::rescheduleUncompletedWork(const QString &after)
{
    const std::optional<Engine::Minutes> date = Engine::parseDate(after, project().info.defaultFinishMinute, m_locale);
    if (!date) {
        return fail(QStringLiteral("“%1” is not a date.").arg(after.trimmed()));
    }
    int moved = 0;
    QVariantMap r = apply(QStringLiteral("Reschedule Uncompleted Work"), [&](Engine::Project &p) {
        moved = Engine::rescheduleUncompletedWork(p, *date);
        return moved > 0 ? QString() : QStringLiteral("No uncompleted work is scheduled before that date.");
    });
    r.insert(QStringLiteral("tasks"), moved);
    return r;
}

QVariantMap PlanService::recurringTaskFields(const QString &taskId) const
{
    const Engine::Project &p = project();
    const int row = rowOf(taskId);
    if (row >= 0 && p.tasks.at(row).recurring && !p.tasks.at(row).recurrence) {
        // Read from Microsoft Project XML, which carries no pattern.
        return {{QStringLiteral("name"), p.tasks.at(row).name}, {QStringLiteral("readOnly"), true}};
    }
    Engine::TaskRecurrence r;
    r.pattern.from = Engine::dateOf(p.info.start);
    r.pattern.daysOfWeek = 1 << (r.pattern.from.dayOfWeek() - 1);
    r.count = 10;
    r.duration = p.info.minutesPerDay;
    r.startMinute = p.info.defaultStartMinute;
    QString name;
    if (row >= 0 && p.tasks.at(row).recurrence) {
        r = *p.tasks.at(row).recurrence;
        name = p.tasks.at(row).name;
    }
    QVariantMap out = patternFields(r.pattern);
    out.insert(QStringLiteral("name"), name);
    out.insert(QStringLiteral("workdays"), r.workdays);
    out.insert(QStringLiteral("count"), r.count);
    out.insert(QStringLiteral("duration"), Engine::formatDuration(r.duration, r.duration % std::max(1, p.info.minutesPerDay) == 0
                                                                                  ? Engine::DurationUnit::Days : Engine::DurationUnit::Hours,
                                                                  Engine::durationFormatOf(p.info), false, m_locale));
    out.insert(QStringLiteral("startTime"), Engine::Json::clock(r.startMinute));
    out.insert(QStringLiteral("readOnly"), false);
    return out;
}

QVariantMap PlanService::saveRecurringTask(const QString &taskId, const QVariantMap &fields)
{
    QString error;
    const std::optional<Engine::RecurringException> pattern = patternOf(fields, m_locale, &error);
    if (!pattern) {
        return fail(error);
    }
    Engine::TaskRecurrence r;
    r.pattern = *pattern;
    r.pattern.name.clear();
    r.workdays = fields.value(QStringLiteral("workdays")).toBool() && pattern->kind == Kind::EveryDays;
    r.count = std::clamp(fields.value(QStringLiteral("count")).toInt(), 0, 999);
    const std::optional<Engine::ParsedDuration> d =
        Engine::parseDuration(fields.value(QStringLiteral("duration")).toString(), Engine::durationFormatOf(project().info));
    if (!d || d->elapsed) {
        return fail(QStringLiteral("Give each occurrence a duration, like 1h or 1d."));
    }
    r.duration = d->minutes;
    const int clock = Engine::Json::parseClock(fields.value(QStringLiteral("startTime")).toString());
    r.startMinute = clock >= 0 && clock < 1440 ? clock : project().info.defaultStartMinute;
    const QString name = fields.value(QStringLiteral("name")).toString().trimmed();
    const int row = rowOf(taskId);
    if (row >= 0) {
        return apply(QStringLiteral("Recurring Task Information"), [&](Engine::Project &p) {
            if (!name.isEmpty()) {
                p.tasks[row].name = name;
            }
            return errorOf(Engine::setRecurrence(p, row, r));
        });
    }
    if (name.isEmpty()) {
        return fail(QStringLiteral("Give the recurring task a name."));
    }
    const QList<int> rows = selectedRows();
    QString created;
    QVariantMap result = apply(QStringLiteral("Insert Recurring Task"), [&](Engine::Project &p) {
        QString why;
        const int at = Engine::insertRecurringTask(p, rows.isEmpty() ? int(p.tasks.size()) : rows.first(), name, r, &why);
        if (at < 0) {
            return why;
        }
        created = p.tasks.at(at).guid.toString(QUuid::WithoutBraces);
        return QString();
    });
    if (!created.isEmpty()) {
        setCurrentTaskId(created);
        emit revealTask(created);
    }
    return result;
}

QVariantMap PlanService::importPreview(const QUrl &url, bool firstRowHeaders)
{
    QString error;
    const std::optional<Engine::ImportTable> table = readTable(url, &error);
    if (!table) {
        return fail(error);
    }
    const qsizetype width = std::accumulate(table->rows.cbegin(), table->rows.cend(), qsizetype(0),
                                            [](qsizetype w, const QStringList &r) { return std::max(w, r.size()); });
    QStringList headers;
    for (qsizetype c = 0; c < width; ++c) {
        const QString h = firstRowHeaders && !table->rows.isEmpty() ? table->rows.first().value(c).trimmed() : QString();
        headers.append(h.isEmpty() ? QStringLiteral("Column %1").arg(c + 1) : h);
    }
    QVariantList rows;
    for (qsizetype r = firstRowHeaders ? 1 : 0; r < table->rows.size() && rows.size() < 8; ++r) {
        rows.append(table->rows.at(r));
    }
    QVariantList fields;
    for (const QString &f : Engine::importFieldIds()) {
        // A renamed custom field as Project's wizard lists it: "Owner (Text1)".
        const QString alias = Engine::isCustomField(f) ? Engine::customFieldTitle(project(), f) : QString();
        const QString title = !alias.isEmpty() && alias != f ? QStringLiteral("%1 (%2)").arg(alias, f)
                                                             : Engine::importFieldTitle(f);
        fields.append(QVariantMap{{QStringLiteral("id"), f}, {QStringLiteral("title"), title}});
    }
    return ok({{QStringLiteral("headers"), headers},
               {QStringLiteral("rows"), rows},
               {QStringLiteral("count"), int(table->rows.size()) - (firstRowHeaders ? 1 : 0)},
               {QStringLiteral("mapping"), firstRowHeaders ? Engine::guessImportMapping(headers, &project())
                                                           : QStringList(width, QString())},
               {QStringLiteral("fields"), fields}});
}

QVariantMap PlanService::importTasks(const QUrl &url, const QStringList &mapping, bool firstRowHeaders)
{
    QString error;
    const std::optional<Engine::ImportTable> table = readTable(url, &error);
    if (!table) {
        return fail(error);
    }
    Engine::ImportReport report;
    const QLocale locale = m_locale;
    QVariantMap r = apply(QStringLiteral("Import Tasks"), [&](Engine::Project &p) {
        return errorOf(Engine::importTasks(p, *table, mapping, firstRowHeaders, &report, locale));
    });
    r.insert(QStringLiteral("tasks"), report.tasks);
    r.insert(QStringLiteral("warnings"), report.warnings);
    return r;
}

QVariantList PlanService::recurringExceptions(int calendarUid) const
{
    QVariantList out;
    const int ci = project().calendarIndex(calendarUid);
    if (ci < 0) {
        return out;
    }
    const QList<Engine::RecurringException> &list = project().calendars.at(ci).recurring;
    for (int k = 0; k < list.size(); ++k) {
        QVariantMap f = patternFields(list.at(k));
        f.insert(QStringLiteral("index"), k);
        f.insert(QStringLiteral("hours"), hoursText(list.at(k).times));
        f.insert(QStringLiteral("working"), !list.at(k).times.isEmpty());
        f.insert(QStringLiteral("text"), describeRecurrence(list.at(k), m_locale));
        f.insert(QStringLiteral("range"), Engine::formatDate(Engine::toMinutes(list.at(k).from), m_locale) + QStringLiteral(" – ")
                                              + Engine::formatDate(Engine::toMinutes(list.at(k).to), m_locale));
        out.append(f);
    }
    return out;
}

QVariantMap PlanService::saveRecurringException(int calendarUid, int index, const QVariantMap &fields)
{
    QString error;
    std::optional<Engine::RecurringException> rx = patternOf(fields, m_locale, &error);
    if (!rx) {
        return fail(error);
    }
    const std::optional<Engine::DayHours> hours = parseHours(fields.value(QStringLiteral("hours")).toString(), &error);
    if (!hours) {
        return fail(error);
    }
    rx->times = *hours;
    const int count = std::clamp(fields.value(QStringLiteral("count")).toInt(), 0, 999);
    return apply(QStringLiteral("Change Working Time"), [&](Engine::Project &p) {
        return errorOf(Engine::setRecurringException(p, calendarUid, index, *rx, count));
    });
}

QVariantMap PlanService::deleteRecurringException(int calendarUid, int index)
{
    return apply(QStringLiteral("Change Working Time"),
                 [&](Engine::Project &p) { return errorOf(Engine::removeRecurringException(p, calendarUid, index)); });
}

QVariantList PlanService::workWeeks(int calendarUid) const
{
    QVariantList out;
    const int ci = project().calendarIndex(calendarUid);
    if (ci < 0) {
        return out;
    }
    const QList<Engine::WorkWeek> &list = project().calendars.at(ci).workWeeks;
    for (int k = 0; k < list.size(); ++k) {
        QStringList days;
        for (const std::optional<Engine::DayHours> &day : list.at(k).week) {
            days.append(day ? hoursText(*day) : QStringLiteral("default"));
        }
        out.append(QVariantMap{{QStringLiteral("index"), k},
                               {QStringLiteral("name"), list.at(k).name},
                               {QStringLiteral("from"), list.at(k).from.toString(Qt::ISODate)},
                               {QStringLiteral("to"), list.at(k).to.toString(Qt::ISODate)},
                               {QStringLiteral("range"), Engine::formatDate(Engine::toMinutes(list.at(k).from), m_locale)
                                                             + QStringLiteral(" – ")
                                                             + Engine::formatDate(Engine::toMinutes(list.at(k).to), m_locale)},
                               {QStringLiteral("days"), days}});
    }
    return out;
}

QVariantMap PlanService::saveWorkWeek(int calendarUid, int index, const QVariantMap &fields)
{
    Engine::WorkWeek week;
    week.name = fields.value(QStringLiteral("name")).toString().trimmed();
    week.from = dateField(fields, "from", m_locale).value_or(QDate());
    week.to = dateField(fields, "to", m_locale).value_or(QDate());
    const QStringList days = fields.value(QStringLiteral("days")).toStringList();
    for (int d = 0; d < 7 && d < days.size(); ++d) {
        const QString text = days.at(d).trimmed();
        if (text.isEmpty() || text.compare(QLatin1String("default"), Qt::CaseInsensitive) == 0) {
            continue; // the calendar's own week
        }
        QString error;
        const std::optional<Engine::DayHours> hours = parseHours(text, &error);
        if (!hours) {
            return fail(error);
        }
        week.week[size_t(d)] = *hours;
    }
    return apply(QStringLiteral("Change Working Time"),
                 [&](Engine::Project &p) { return errorOf(Engine::setWorkWeek(p, calendarUid, index, week)); });
}

QVariantMap PlanService::deleteWorkWeek(int calendarUid, int index)
{
    return apply(QStringLiteral("Change Working Time"),
                 [&](Engine::Project &p) { return errorOf(Engine::removeWorkWeek(p, calendarUid, index)); });
}

} // namespace QindaOffice::Apps::Plan
