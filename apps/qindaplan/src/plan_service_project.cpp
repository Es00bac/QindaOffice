// SPDX-License-Identifier: GPL-3.0-or-later
// PlanService: project information, calendars, baselines and status
// (plan_service.h). Reports, files and the clipboard are in
// plan_service_output.cpp.
#include "baselines.h"
#include "plan_document.h"
#include "plan_edit.h"
#include "plan_json.h"
#include "plan_service.h"
#include "plan_time.h"
#include "work_calendar.h"

#include <QRegularExpression>

namespace QindaOffice::Apps::Plan {

namespace {

const char *const kDayNames[7] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};

QString hoursText(const Engine::DayHours &hours)
{
    if (hours.isEmpty()) {
        return QStringLiteral("Nonworking");
    }
    QStringList parts;
    for (const Engine::WorkingTime &wt : hours) {
        parts.append(Engine::Json::clock(wt.from) + QStringLiteral("–") + Engine::Json::clock(wt.to));
    }
    return parts.join(QStringLiteral(", "));
}

// "08:00-12:00, 13:00-17:00" | "nonworking" | "" (non-working).
std::optional<Engine::DayHours> parseHours(const QString &text, QString *error)
{
    const QString t = text.trimmed();
    if (t.isEmpty() || t.compare(QLatin1String("nonworking"), Qt::CaseInsensitive) == 0
        || t.compare(QLatin1String("non-working"), Qt::CaseInsensitive) == 0) {
        return Engine::DayHours{};
    }
    static const QRegularExpression range(QStringLiteral("^\\s*(\\d{1,2}:\\d{2})\\s*[-–]\\s*(\\d{1,2}:\\d{2})\\s*$"));
    Engine::DayHours out;
    for (const QString &part : t.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QRegularExpressionMatch m = range.match(part);
        const int from = m.hasMatch() ? Engine::Json::parseClock(m.captured(1)) : -1;
        const int to = m.hasMatch() ? Engine::Json::parseClock(m.captured(2)) : -1;
        if (from < 0 || to <= from || (!out.isEmpty() && from < out.last().to)) {
            *error = QStringLiteral("Write working times like 08:00–12:00, 13:00–17:00 (in order, not overlapping).");
            return std::nullopt;
        }
        out.append({from, to});
    }
    return out;
}

} // namespace

QVariantMap PlanService::projectInfo() const
{
    const Engine::Project &p = project();
    const int c = p.calendarIndex(p.info.calendarUid);
    return {{QStringLiteral("title"), p.info.title},
            {QStringLiteral("manager"), p.info.manager},
            {QStringLiteral("company"), p.info.company},
            {QStringLiteral("notes"), p.info.notes},
            {QStringLiteral("start"), Engine::formatDate(p.info.start, m_locale)},
            {QStringLiteral("finish"), Engine::formatDate(p.finish, m_locale)},
            {QStringLiteral("statusDate"), Engine::formatDate(p.info.statusDate, m_locale)},
            {QStringLiteral("currency"), p.info.currency},
            {QStringLiteral("calendar"), c >= 0 ? p.calendars.at(c).name : QString()},
            {QStringLiteral("hoursPerDay"), m_locale.toString(double(p.info.minutesPerDay) / 60.0, 'g', 4)},
            // M79: Project's schedule options for the critical path.
            {QStringLiteral("criticalSlackDays"), p.info.criticalSlackDays},
            {QStringLiteral("multipleCriticalPaths"), p.info.multipleCriticalPaths}};
}

QVariantMap PlanService::setProjectInfo(const QVariantMap &fields)
{
    const QLocale locale = m_locale;
    return apply(QStringLiteral("Project Information"), [&](Engine::Project &p) {
        Engine::ProjectInfo &info = p.info;
        for (const char *key : {"title", "manager", "company", "notes"}) {
            const QString k = QLatin1String(key);
            if (!fields.contains(k)) {
                continue;
            }
            const QString v = fields.value(k).toString();
            QString &target = k == QLatin1String("title") ? info.title : k == QLatin1String("manager") ? info.manager
                            : k == QLatin1String("company") ? info.company : info.notes;
            target = k == QLatin1String("notes") ? v : v.trimmed();
        }
        if (fields.contains(QStringLiteral("start"))) {
            const std::optional<Engine::Minutes> d =
                Engine::parseDate(fields.value(QStringLiteral("start")).toString(), info.defaultStartMinute, locale);
            if (!d) {
                return QStringLiteral("The start date is not a date.");
            }
            info.start = *d;
        }
        if (fields.contains(QStringLiteral("statusDate"))) {
            const QString text = fields.value(QStringLiteral("statusDate")).toString().trimmed();
            if (text.isEmpty() || text.compare(QLatin1String("NA"), Qt::CaseInsensitive) == 0) {
                info.statusDate = Engine::kNoDate;
            } else {
                const std::optional<Engine::Minutes> d = Engine::parseDate(text, info.defaultFinishMinute, locale);
                if (!d) {
                    return QStringLiteral("The status date is not a date.");
                }
                info.statusDate = *d;
            }
        }
        if (fields.contains(QStringLiteral("currency"))) {
            const QString cur = fields.value(QStringLiteral("currency")).toString().trimmed().toUpper();
            if (!QRegularExpression(QStringLiteral("^[A-Z]{3}$")).match(cur).hasMatch()) {
                return QStringLiteral("The currency is a three-letter code such as USD or EUR.");
            }
            info.currency = cur;
        }
        if (fields.contains(QStringLiteral("calendar"))) {
            const QString name = fields.value(QStringLiteral("calendar")).toString();
            for (const Engine::Calendar &c : std::as_const(p.calendars)) {
                if (c.name == name) {
                    info.calendarUid = c.uid;
                }
            }
        }
        if (fields.contains(QStringLiteral("criticalSlackDays"))) {
            bool okNumber = false;
            const int days = fields.value(QStringLiteral("criticalSlackDays")).toString().trimmed().toInt(&okNumber);
            if (!okNumber || days < 0 || days > 999) {
                return QStringLiteral("Tasks are critical if their slack is at most a whole number of days (0 or more).");
            }
            info.criticalSlackDays = days;
        }
        if (fields.contains(QStringLiteral("multipleCriticalPaths"))) {
            info.multipleCriticalPaths = fields.value(QStringLiteral("multipleCriticalPaths")).toBool();
        }
        if (fields.contains(QStringLiteral("hoursPerDay"))) {
            bool okNumber = false;
            const double hours = locale.toDouble(fields.value(QStringLiteral("hoursPerDay")).toString(), &okNumber);
            if (!okNumber || hours <= 0 || hours > 24) {
                return QStringLiteral("Hours per day is a number from 1 to 24.");
            }
            info.minutesPerDay = int(hours * 60.0);
            info.minutesPerWeek = info.minutesPerDay * 5;
        }
        return QString();
    });
}

QVariantList PlanService::calendars() const
{
    QVariantList out;
    const Engine::Project &p = project();
    for (const Engine::Calendar &c : p.calendars) {
        QStringList users;
        for (const Engine::Resource &r : p.resources) {
            if (r.calendarUid == c.uid) {
                users.append(r.name);
            }
        }
        const int base = p.calendarIndex(c.baseUid);
        out.append(QVariantMap{{QStringLiteral("uid"), c.uid},
                               {QStringLiteral("name"), c.name},
                               {QStringLiteral("base"), base >= 0 ? p.calendars.at(base).name : QString()},
                               {QStringLiteral("isProject"), c.uid == p.info.calendarUid},
                               {QStringLiteral("resources"), users.join(QStringLiteral(", "))},
                               {QStringLiteral("exceptionCount"), int(c.exceptions.size())}});
    }
    return out;
}

QVariantList PlanService::calendarWeek(int calendarUid) const
{
    QVariantList out;
    const Engine::Project &p = project();
    const int ci = p.calendarIndex(calendarUid);
    if (ci < 0) {
        return out;
    }
    const Engine::Calendar &c = p.calendars.at(ci);
    const Engine::WorkCalendar resolved(p, calendarUid);
    const QDate monday(2026, 1, 5); // any Monday: only the week is read
    for (int d = 0; d < 7; ++d) {
        const bool own = c.week[size_t(d)].has_value();
        Engine::DayHours hours;
        if (own) {
            hours = *c.week[size_t(d)];
        } else if (resolved.weekMinutes(d) > 0) {
            // Inherited: show the base's hours for that weekday.
            Engine::Calendar probe = c;
            probe.exceptions.clear();
            hours = Engine::WorkCalendar(QList<Engine::Calendar>{probe}).hours(monday.addDays(d));
            const int bi = p.calendarIndex(c.baseUid);
            if (bi >= 0) {
                Engine::Calendar base = p.calendars.at(bi);
                base.exceptions.clear();
                hours = Engine::WorkCalendar(QList<Engine::Calendar>{base}).hours(monday.addDays(d));
            }
        }
        out.append(QVariantMap{{QStringLiteral("day"), d},
                               {QStringLiteral("name"), QString::fromLatin1(kDayNames[d])},
                               {QStringLiteral("inherited"), !own && c.baseUid >= 0},
                               {QStringLiteral("working"), !hours.isEmpty()},
                               {QStringLiteral("hours"), hoursText(hours)}});
    }
    return out;
}

QVariantMap PlanService::setCalendarDay(int calendarUid, int day, const QString &hoursText)
{
    if (day < 0 || day > 6) {
        return fail(QStringLiteral("There is no such day."));
    }
    QString error;
    const bool inherit = hoursText.trimmed().compare(QLatin1String("default"), Qt::CaseInsensitive) == 0;
    const std::optional<Engine::DayHours> hours = inherit ? std::optional<Engine::DayHours>(Engine::DayHours{})
                                                          : parseHours(hoursText, &error);
    if (!hours) {
        return fail(error);
    }
    return apply(QStringLiteral("Change Working Time"), [&](Engine::Project &p) {
        const int ci = p.calendarIndex(calendarUid);
        if (ci < 0) {
            return QStringLiteral("There is no such calendar.");
        }
        Engine::Calendar &c = p.calendars[ci];
        if (inherit && c.baseUid >= 0) {
            c.week[size_t(day)].reset();
        } else {
            c.week[size_t(day)] = *hours;
        }
        return QString();
    });
}

namespace {

// M79: how Project's Change Working Time describes a recurrence.
QString describe(const Engine::RecurringException &rx, const QLocale &locale)
{
    using Kind = Engine::RecurringException::Kind;
    static const char *const kPositions[] = {"first", "second", "third", "fourth", "last"};
    const QString weekday = locale.dayName(rx.weekday, QLocale::LongFormat);
    const QString position = QString::fromLatin1(kPositions[std::clamp(rx.position, 0, 4)]);
    switch (rx.kind) {
    case Kind::EveryDays:
        return rx.every == 1 ? QStringLiteral("Every day") : QStringLiteral("Every %1 days").arg(rx.every);
    case Kind::Weekly: {
        QStringList days;
        for (int d = 0; d < 7; ++d) {
            if ((rx.daysOfWeek >> d) & 1) {
                days.append(locale.dayName(d + 1, QLocale::ShortFormat));
            }
        }
        const QString list = days.join(QStringLiteral(", "));
        return rx.every == 1 ? QStringLiteral("Every week on %1").arg(list)
                             : QStringLiteral("Every %1 weeks on %2").arg(rx.every).arg(list);
    }
    case Kind::MonthlyByDay:
        return QStringLiteral("Day %1 of every %2").arg(rx.day).arg(rx.every == 1 ? QStringLiteral("month")
                                                                                  : QStringLiteral("%1 months").arg(rx.every));
    case Kind::MonthlyByPosition:
        return QStringLiteral("The %1 %2 of every %3").arg(position, weekday,
                                                          rx.every == 1 ? QStringLiteral("month") : QStringLiteral("%1 months").arg(rx.every));
    case Kind::YearlyByDay:
        return QStringLiteral("Every year on %1 %2").arg(rx.day).arg(locale.monthName(rx.month, QLocale::LongFormat));
    case Kind::YearlyByPosition:
        return QStringLiteral("Every year on the %1 %2 of %3").arg(position, weekday, locale.monthName(rx.month, QLocale::LongFormat));
    }
    return {};
}

} // namespace

QVariantList PlanService::calendarExceptions(int calendarUid) const
{
    QVariantList out;
    const int ci = project().calendarIndex(calendarUid);
    if (ci < 0) {
        return out;
    }
    // M79: Project's recurring exceptions and alternative work weeks, read
    // from its XML — shown, scheduled and written back, not edited here.
    const Engine::Calendar &cal = project().calendars.at(ci);
    for (const Engine::RecurringException &rx : cal.recurring) {
        out.append(QVariantMap{{QStringLiteral("index"), -1},
                               {QStringLiteral("name"), rx.name},
                               {QStringLiteral("from"), Engine::formatDate(Engine::toMinutes(rx.from), m_locale)},
                               {QStringLiteral("to"), Engine::formatDate(Engine::toMinutes(rx.to), m_locale)},
                               {QStringLiteral("hours"), hoursText(rx.times)},
                               {QStringLiteral("working"), !rx.times.isEmpty()},
                               {QStringLiteral("readOnly"), true},
                               {QStringLiteral("recurrence"), describe(rx, m_locale)}});
    }
    for (const Engine::WorkWeek &ww : cal.workWeeks) {
        out.append(QVariantMap{{QStringLiteral("index"), -1},
                               {QStringLiteral("name"), ww.name},
                               {QStringLiteral("from"), Engine::formatDate(Engine::toMinutes(ww.from), m_locale)},
                               {QStringLiteral("to"), Engine::formatDate(Engine::toMinutes(ww.to), m_locale)},
                               {QStringLiteral("hours"), QStringLiteral("Work week")},
                               {QStringLiteral("working"), true},
                               {QStringLiteral("readOnly"), true},
                               {QStringLiteral("recurrence"), QStringLiteral("Alternative work week")}});
    }
    const QList<Engine::CalendarException> &list = cal.exceptions;
    for (int i = 0; i < list.size(); ++i) {
        const Engine::CalendarException &e = list.at(i);
        out.append(QVariantMap{{QStringLiteral("index"), i},
                               {QStringLiteral("name"), e.name},
                               {QStringLiteral("from"), Engine::formatDate(Engine::toMinutes(e.from), m_locale)},
                               {QStringLiteral("to"), Engine::formatDate(Engine::toMinutes(e.to), m_locale)},
                               {QStringLiteral("fromIso"), e.from.toString(Qt::ISODate)},
                               {QStringLiteral("hours"), hoursText(e.times)},
                               {QStringLiteral("working"), !e.times.isEmpty()}});
    }
    return out;
}

QVariantMap PlanService::addCalendarException(int calendarUid, const QString &name, const QString &from,
                                              const QString &to, const QString &hoursText)
{
    const std::optional<Engine::Minutes> a = Engine::parseDate(from, 0, m_locale);
    const std::optional<Engine::Minutes> b = to.trimmed().isEmpty() ? a : Engine::parseDate(to, 0, m_locale);
    if (!a || !b || *b < *a) {
        return fail(QStringLiteral("Give the exception a start date and an end date on or after it."));
    }
    QString error;
    const std::optional<Engine::DayHours> hours = parseHours(hoursText, &error);
    if (!hours) {
        return fail(error);
    }
    return apply(QStringLiteral("Add Calendar Exception"), [&](Engine::Project &p) {
        const int ci = p.calendarIndex(calendarUid);
        if (ci < 0) {
            return QStringLiteral("There is no such calendar.");
        }
        p.calendars[ci].exceptions.append(
            {Engine::dateOf(*a), Engine::dateOf(*b), name.trimmed().isEmpty() ? QStringLiteral("Exception") : name.trimmed(), *hours});
        std::sort(p.calendars[ci].exceptions.begin(), p.calendars[ci].exceptions.end(),
                  [](const Engine::CalendarException &x, const Engine::CalendarException &y) { return x.from < y.from; });
        return QString();
    });
}

QVariantMap PlanService::removeCalendarException(int calendarUid, int index)
{
    return apply(QStringLiteral("Remove Calendar Exception"), [&](Engine::Project &p) {
        const int ci = p.calendarIndex(calendarUid);
        if (ci < 0 || index < 0 || index >= p.calendars.at(ci).exceptions.size()) {
            return QStringLiteral("There is no such exception.");
        }
        p.calendars[ci].exceptions.removeAt(index);
        return QString();
    });
}

int PlanService::resourceCalendar(int resourceUid)
{
    const int ri = project().resourceIndex(resourceUid);
    if (ri < 0) {
        return -1;
    }
    int uid = -1;
    const Engine::Resource &r = project().resources.at(ri);
    const int existing = project().calendarIndex(r.calendarUid);
    if (existing >= 0 && project().calendars.at(existing).baseUid >= 0) {
        return r.calendarUid;
    }
    (void)apply(QStringLiteral("Resource Calendar"), [&](Engine::Project &p) {
        uid = Engine::ensureResourceCalendar(p, ri);
        return QString();
    });
    return uid;
}

QVariantList PlanService::baselines() const
{
    // M79: Baseline, Baseline 1 … Baseline 10, and how many tasks hold each.
    QVariantList out;
    for (int n = 0; n < Engine::kBaselineCount; ++n) {
        int count = 0;
        for (const Engine::Task &t : project().tasks) {
            count += t.baselines[size_t(n)].set ? 1 : 0;
        }
        out.append(QVariantMap{{QStringLiteral("number"), n},
                               {QStringLiteral("label"), Engine::baselineName(n)},
                               {QStringLiteral("set"), count > 0},
                               {QStringLiteral("tasks"), count}});
    }
    return out;
}

QVariantMap PlanService::setBaseline(int number, bool selectedOnly)
{
    QList<int> uids;
    if (selectedOnly) {
        for (int row : selectedRows()) {
            uids.append(project().tasks.at(row).uid);
        }
        if (uids.isEmpty()) {
            return fail(QStringLiteral("Select the tasks to baseline."));
        }
    }
    return apply(QStringLiteral("Set Baseline"), [&](Engine::Project &p) {
        return Engine::setBaseline(p, number, uids) > 0 ? QString() : QStringLiteral("There is no such baseline.");
    });
}

QVariantMap PlanService::clearBaseline(int number, bool selectedOnly)
{
    QList<int> uids;
    if (selectedOnly) {
        for (int row : selectedRows()) {
            uids.append(project().tasks.at(row).uid);
        }
        if (uids.isEmpty()) {
            return fail(QStringLiteral("Select the tasks to clear."));
        }
    }
    return apply(QStringLiteral("Clear Baseline"), [&](Engine::Project &p) {
        return Engine::clearBaseline(p, number, uids) > 0 ? QString()
                                                          : QStringLiteral("%1 is not set on those tasks.").arg(Engine::baselineName(number));
    });
}

QVariantMap PlanService::setStatusDate(const QString &date)
{
    return setProjectInfo({{QStringLiteral("statusDate"), date}});
}

QVariantMap PlanService::updateProject(const QString &date)
{
    const std::optional<Engine::Minutes> d = Engine::parseDate(date, project().info.defaultFinishMinute, m_locale);
    if (!d) {
        return fail(QStringLiteral("“%1” is not a date.").arg(date.trimmed()));
    }
    return apply(QStringLiteral("Update Project"), [&](Engine::Project &p) {
        p.info.statusDate = *d;
        Engine::updateProgress(p, *d);
        return QString();
    });
}

} // namespace QindaOffice::Apps::Plan
