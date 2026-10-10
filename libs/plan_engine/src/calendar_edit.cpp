// SPDX-License-Identifier: GPL-3.0-or-later
// Recurring exceptions and work weeks made and changed in Change Working
// Time (calendar_edit.h): each edit writes Project's XML element and the
// calendar derives its recurrences again (mspdi_recurrence.cpp).
#include "calendar_edit.h"

#include "mspdi_keep.h"
#include "mspdi_p.h"
#include "plan_time.h"

#include <QJsonArray>
#include <QXmlStreamWriter>

#include <algorithm>

namespace QindaOffice::Plan {

namespace {

using Kind = RecurringException::Kind;

void writeTimes(QXmlStreamWriter &w, const DayHours &hours)
{
    if (hours.isEmpty()) {
        return;
    }
    w.writeStartElement(QStringLiteral("WorkingTimes"));
    for (const WorkingTime &wt : hours) {
        w.writeStartElement(QStringLiteral("WorkingTime"));
        w.writeTextElement(QStringLiteral("FromTime"), Mspdi::formatTimeOfDay(wt.from));
        w.writeTextElement(QStringLiteral("ToTime"), Mspdi::formatTimeOfDay(wt.to));
        w.writeEndElement();
    }
    w.writeEndElement();
}

void writePeriod(QXmlStreamWriter &w, const QDate &from, const QDate &to)
{
    w.writeStartElement(QStringLiteral("TimePeriod"));
    w.writeTextElement(QStringLiteral("FromDate"), Mspdi::wireDateTime(toMinutes(from, 0)));
    w.writeTextElement(QStringLiteral("ToDate"), Mspdi::wireDateTime(toMinutes(to.isValid() ? to : from, 23 * 60 + 59)));
    w.writeEndElement();
}

int typeCode(Kind kind)
{
    switch (kind) {
    case Kind::YearlyByDay: return 2;
    case Kind::YearlyByPosition: return 3;
    case Kind::MonthlyByDay: return 4;
    case Kind::MonthlyByPosition: return 5;
    case Kind::Weekly: return 6;
    case Kind::EveryDays: return 7;
    }
    return 7;
}

Calendar *calendarOf(Project &project, int uid)
{
    const int c = project.calendarIndex(uid);
    return c >= 0 ? &project.calendars[c] : nullptr;
}

// Where each Calendar::recurring entry came from in "mspdiExceptions" (the
// kinds QindaPlan cannot expand are not in the model).
QList<int> fragmentIndices(const Calendar &calendar)
{
    QList<int> out;
    const QJsonArray fragments = calendar.extra.value(Mspdi::kExceptionsKey).toArray();
    for (int k = 0; k < fragments.size(); ++k) {
        if (Mspdi::parseRecurringException(fragments.at(k).toString(), nullptr)) {
            out.append(k);
        }
    }
    return out;
}

QString workWeeksXml(const QList<WorkWeek> &weeks)
{
    QString out;
    QXmlStreamWriter w(&out);
    w.writeStartElement(QStringLiteral("WorkWeeks"));
    for (const WorkWeek &ww : weeks) {
        w.writeStartElement(QStringLiteral("WorkWeek"));
        writePeriod(w, ww.from, ww.to);
        if (!ww.name.isEmpty()) {
            w.writeTextElement(QStringLiteral("Name"), ww.name);
        }
        w.writeStartElement(QStringLiteral("WeekDays"));
        for (int dayType = 1; dayType <= 7; ++dayType) {
            const std::optional<DayHours> &day = ww.week[size_t(dayType == 1 ? 6 : dayType - 2)];
            if (!day) {
                continue;
            }
            w.writeStartElement(QStringLiteral("WeekDay"));
            w.writeTextElement(QStringLiteral("DayType"), QString::number(dayType));
            w.writeTextElement(QStringLiteral("DayWorking"), day->isEmpty() ? QStringLiteral("0") : QStringLiteral("1"));
            writeTimes(w, *day);
            w.writeEndElement();
        }
        w.writeEndElement(); // WeekDays
        w.writeEndElement(); // WorkWeek
    }
    w.writeEndElement();
    return out;
}

// Replaces the calendar's kept <WorkWeeks> (adds one, removes it when empty).
void storeWorkWeeks(Calendar &calendar, const QList<WorkWeek> &weeks)
{
    QList<Mspdi::Kept> kept = Mspdi::keptFromJson(calendar.extra.value(Mspdi::kKeptKey));
    kept.removeIf([](const Mspdi::Kept &k) { return k.name == QLatin1String("WorkWeeks"); });
    if (!weeks.isEmpty()) {
        kept.append({QStringLiteral("WorkWeeks"), QStringLiteral("Exceptions"), workWeeksXml(weeks), false});
    }
    if (kept.isEmpty()) {
        calendar.extra.remove(Mspdi::kKeptKey);
    } else {
        calendar.extra.insert(Mspdi::kKeptKey, Mspdi::keptToJson(kept));
    }
    (void)Mspdi::deriveRecurrences(calendar);
}

} // namespace

QString recurringExceptionXml(const RecurringException &rx, int occurrences)
{
    QString out;
    QXmlStreamWriter w(&out);
    w.writeStartElement(QStringLiteral("Exception"));
    w.writeTextElement(QStringLiteral("EnteredByOccurrences"), occurrences > 0 ? QStringLiteral("1") : QStringLiteral("0"));
    writePeriod(w, rx.from, rx.to);
    if (occurrences > 0) {
        w.writeTextElement(QStringLiteral("Occurrences"), QString::number(occurrences));
    }
    if (!rx.name.isEmpty()) {
        w.writeTextElement(QStringLiteral("Name"), rx.name);
    }
    w.writeTextElement(QStringLiteral("Type"), QString::number(typeCode(rx.kind)));
    const bool yearly = rx.kind == Kind::YearlyByDay || rx.kind == Kind::YearlyByPosition;
    if (!yearly) {
        w.writeTextElement(QStringLiteral("Period"), QString::number(std::max(1, rx.every)));
    }
    if (rx.kind == Kind::Weekly) {
        // Project's mask is Sunday = 1 … Saturday = 64; ours Monday-first.
        const int mask = ((rx.daysOfWeek & 0x3F) << 1) | ((rx.daysOfWeek >> 6) & 1);
        w.writeTextElement(QStringLiteral("DaysOfWeek"), QString::number(mask));
    }
    if (rx.kind == Kind::MonthlyByPosition || rx.kind == Kind::YearlyByPosition) {
        w.writeTextElement(QStringLiteral("MonthItem"), QString::number(rx.weekday == 7 ? 3 : rx.weekday + 3));
        w.writeTextElement(QStringLiteral("MonthPosition"), QString::number(std::clamp(rx.position, 0, 4)));
    }
    if (yearly) {
        w.writeTextElement(QStringLiteral("Month"), QString::number(std::clamp(rx.month, 1, 12) - 1));
    }
    if (rx.kind == Kind::MonthlyByDay || rx.kind == Kind::YearlyByDay) {
        w.writeTextElement(QStringLiteral("MonthDay"), QString::number(std::clamp(rx.day, 1, 31)));
    }
    w.writeTextElement(QStringLiteral("DayWorking"), rx.times.isEmpty() ? QStringLiteral("0") : QStringLiteral("1"));
    writeTimes(w, rx.times);
    w.writeEndElement();
    return out;
}

EditResult setRecurringException(Project &project, int calendarUid, int index, RecurringException rx, int occurrences)
{
    Calendar *calendar = calendarOf(project, calendarUid);
    if (calendar == nullptr) {
        return EditResult::fail(QStringLiteral("There is no such calendar."));
    }
    if (!rx.from.isValid()) {
        return EditResult::fail(QStringLiteral("Give the exception a first day."));
    }
    if (rx.kind == Kind::Weekly && (rx.daysOfWeek & 0x7F) == 0) {
        return EditResult::fail(QStringLiteral("Pick the days of the week it falls on."));
    }
    rx.name = rx.name.trimmed();
    if (occurrences > 0) {
        rx.to = {};
        int seen = 0;
        for (QDate d = rx.from; seen < occurrences && rx.from.daysTo(d) < 36600; d = d.addDays(1)) {
            RecurringException open = rx;
            open.to = rx.from.addYears(100);
            if (open.fallsOn(d) && ++seen == occurrences) {
                rx.to = d;
            }
        }
    }
    if (!rx.to.isValid() || rx.to < rx.from) {
        return EditResult::fail(QStringLiteral("Give the exception a last day on or after its first."));
    }
    bool falls = false;
    for (QDate d = rx.from; d <= rx.to && !falls && rx.from.daysTo(d) < 36600; d = d.addDays(1)) {
        falls = rx.fallsOn(d);
    }
    if (!falls) {
        return EditResult::fail(QStringLiteral("The pattern never falls between those dates."));
    }
    QJsonArray fragments = calendar->extra.value(Mspdi::kExceptionsKey).toArray();
    const QList<int> where = fragmentIndices(*calendar);
    if (index >= where.size()) {
        return EditResult::fail(QStringLiteral("There is no such exception."));
    }
    const QString xml = recurringExceptionXml(rx, occurrences);
    if (index < 0) {
        fragments.append(xml);
    } else {
        fragments.replace(where.at(index), xml);
    }
    calendar->extra.insert(Mspdi::kExceptionsKey, fragments);
    (void)Mspdi::deriveRecurrences(*calendar);
    return {};
}

EditResult removeRecurringException(Project &project, int calendarUid, int index)
{
    Calendar *calendar = calendarOf(project, calendarUid);
    const QList<int> where = calendar != nullptr ? fragmentIndices(*calendar) : QList<int>();
    if (calendar == nullptr || index < 0 || index >= where.size()) {
        return EditResult::fail(QStringLiteral("There is no such exception."));
    }
    QJsonArray fragments = calendar->extra.value(Mspdi::kExceptionsKey).toArray();
    fragments.removeAt(where.at(index));
    if (fragments.isEmpty()) {
        calendar->extra.remove(Mspdi::kExceptionsKey);
    } else {
        calendar->extra.insert(Mspdi::kExceptionsKey, fragments);
    }
    (void)Mspdi::deriveRecurrences(*calendar);
    return {};
}

EditResult setWorkWeek(Project &project, int calendarUid, int index, const WorkWeek &week)
{
    Calendar *calendar = calendarOf(project, calendarUid);
    if (calendar == nullptr) {
        return EditResult::fail(QStringLiteral("There is no such calendar."));
    }
    if (!week.from.isValid() || !week.to.isValid() || week.to < week.from) {
        return EditResult::fail(QStringLiteral("Give the work week a first day and a last day on or after it."));
    }
    QList<WorkWeek> weeks = calendar->workWeeks;
    if (index >= weeks.size()) {
        return EditResult::fail(QStringLiteral("There is no such work week."));
    }
    WorkWeek clean = week;
    clean.name = clean.name.trimmed();
    if (index < 0) {
        weeks.append(clean);
    } else {
        weeks[index] = clean;
    }
    storeWorkWeeks(*calendar, weeks);
    return {};
}

EditResult removeWorkWeek(Project &project, int calendarUid, int index)
{
    Calendar *calendar = calendarOf(project, calendarUid);
    if (calendar == nullptr || index < 0 || index >= calendar->workWeeks.size()) {
        return EditResult::fail(QStringLiteral("There is no such work week."));
    }
    QList<WorkWeek> weeks = calendar->workWeeks;
    weeks.removeAt(index);
    storeWorkWeeks(*calendar, weeks);
    return {};
}

} // namespace QindaOffice::Plan
