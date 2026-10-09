// SPDX-License-Identifier: GPL-3.0-or-later
// MSPDI import: calendars (mspdi_reader_p.h). Week days and daily exceptions
// (both of Project's spellings) are modelled; recurring exceptions and
// alternative work weeks are kept verbatim for the file and (M79) derived
// into the calendar for the scheduler (mspdi_recurrence.cpp); a kind of
// recurrence QindaPlan cannot schedule is reported.
#include "mspdi_reader_p.h"

#include "plan_time.h"

namespace QindaOffice::Plan::Mspdi {

namespace {

const DayHours kDefaultDay{{8 * 60, 12 * 60}, {13 * 60, 17 * 60}};

void addException(Calendar &calendar, const CalendarException &ex)
{
    if (!ex.from.isValid()) {
        return;
    }
    // Project writes the legacy (DayType 0, no name) and the 2007 form of
    // the same exception; keep one, with the name.
    for (CalendarException &existing : calendar.exceptions) {
        if (existing.from == ex.from && existing.to == ex.to && existing.times == ex.times) {
            if (existing.name.isEmpty()) {
                existing.name = ex.name;
            }
            return;
        }
    }
    calendar.exceptions.append(ex);
}

} // namespace

void Reader::readCalendar()
{
    Calendar cal;
    bool isBase = true;
    int baseUid = -1;
    QList<Kept> kept;
    QString previous;
    QJsonArray unsupported;
    while (in().readNextStartElement()) {
        const QString n = in().name().toString();
        if (n == QLatin1String("UID")) {
            cal.uid = integer(0);
        } else if (n == QLatin1String("Name")) {
            cal.name = text();
        } else if (n == QLatin1String("IsBaseCalendar")) {
            isBase = boolean();
        } else if (n == QLatin1String("BaseCalendarUID")) {
            baseUid = integer(-1);
        } else if (n == QLatin1String("WeekDays")) {
            while (in().readNextStartElement()) {
                in().name() == u"WeekDay" ? readWeekDay(cal) : in().skipCurrentElement();
            }
        } else if (n == QLatin1String("Exceptions")) {
            while (in().readNextStartElement()) {
                if (in().name() != u"Exception") {
                    in().skipCurrentElement();
                    continue;
                }
                // Captured first, parsed second: a kind the model lacks
                // travels on as written.
                const QString fragment = capture(in());
                bool modelled = true;
                reparse(fragment, [&] { modelled = readException(cal); });
                if (!modelled) {
                    unsupported.append(fragment);
                }
            }
        } else {
            keep(kept, Kind::Calendar, previous);
        }
        previous = n;
    }
    cal.baseUid = isBase ? -1 : baseUid;
    if (!kept.isEmpty()) {
        cal.extra.insert(kKeptKey, keptToJson(kept));
    }
    if (!unsupported.isEmpty()) {
        cal.extra.insert(kExceptionsKey, unsupported);
    }
    // M79: the kept recurrences and work weeks, for the scheduler.
    for (const QString &problem : deriveRecurrences(cal)) {
        warn(QStringLiteral("A recurring exception of calendar “%1” is kept in the file but not scheduled: %2.")
                 .arg(cal.name, problem));
    }
    m_calendars.append(cal);
}

DayHours Reader::readWorkingTimes()
{
    DayHours hours;
    while (in().readNextStartElement()) {
        if (in().name() != u"WorkingTime") {
            in().skipCurrentElement();
            continue;
        }
        std::optional<int> from;
        std::optional<int> to;
        while (in().readNextStartElement()) {
            if (in().name() == u"FromTime") {
                from = parseTimeOfDay(text(), false);
            } else if (in().name() == u"ToTime") {
                to = parseTimeOfDay(text(), true);
            } else {
                in().skipCurrentElement();
            }
        }
        if (from && to && *to > *from) {
            hours.append({*from, *to});
        }
    }
    return hours;
}

bool Reader::readTimePeriod(QDate *from, QDate *to)
{
    while (in().readNextStartElement()) {
        if (in().name() == u"FromDate") {
            *from = dateOf(parseWireDateTime(text()));
        } else if (in().name() == u"ToDate") {
            *to = dateOf(parseWireDateTime(text()));
        } else {
            in().skipCurrentElement();
        }
    }
    return from->isValid();
}

void Reader::readWeekDay(Calendar &calendar)
{
    int dayType = -1;
    bool working = false;
    bool hasTimes = false;
    DayHours hours;
    QDate from;
    QDate to;
    while (in().readNextStartElement()) {
        const QStringView n = in().name();
        if (n == u"DayType") {
            dayType = integer(-1);
        } else if (n == u"DayWorking") {
            working = boolean();
        } else if (n == u"WorkingTimes") {
            hours = readWorkingTimes();
            hasTimes = true;
        } else if (n == u"TimePeriod") {
            readTimePeriod(&from, &to);
        } else {
            in().skipCurrentElement();
        }
    }
    // AGENT-NOTE: a working day without times gets the standard 08–12,
    // 13–17 (Project always writes the times; other writers do not).
    const DayHours day = working ? (hasTimes && !hours.isEmpty() ? hours : kDefaultDay) : DayHours{};
    if (dayType == 0) {
        addException(calendar, {from, to.isValid() ? to : from, QString(), day});
    } else if (dayType >= 1 && dayType <= 7) {
        calendar.week[size_t(dayType == 1 ? 6 : dayType - 2)] = day;
    }
}

// Reads one <Exception> (the current element). A daily exception becomes a
// CalendarException (or merges with its legacy twin); any other kind
// leaves the calendar as it was and returns false — the caller keeps it.
bool Reader::readException(Calendar &calendar)
{
    CalendarException ex;
    int type = 1;
    bool working = false;
    bool hasTimes = false;
    DayHours hours;
    while (in().readNextStartElement()) {
        const QStringView n = in().name();
        if (n == u"TimePeriod") {
            readTimePeriod(&ex.from, &ex.to);
        } else if (n == u"Name") {
            ex.name = text();
        } else if (n == u"Type") {
            type = integer(1);
        } else if (n == u"DayWorking") {
            working = boolean();
        } else if (n == u"WorkingTimes") {
            hours = readWorkingTimes();
            hasTimes = true;
        } else {
            in().skipCurrentElement();
        }
    }
    if (type != 1) {
        // AGENT-NOTE: only daily ranges map onto CalendarException; the
        // other kinds stay kept (M63) and are scheduled from the kept
        // element (M79, deriveRecurrences in readCalendar).
        return false;
    }
    ex.times = working ? (hasTimes && !hours.isEmpty() ? hours : kDefaultDay) : DayHours{};
    if (!ex.to.isValid()) {
        ex.to = ex.from;
    }
    addException(calendar, ex);
    return true;
}

} // namespace QindaOffice::Plan::Mspdi
