// SPDX-License-Identifier: GPL-3.0-or-later
// Project's recurring calendar exceptions and alternative work weeks (M79,
// D-088). The MSPDI elements stay the truth — kept in the calendar's extra
// and written back as the file had them (D-071) — and the scheduler's
// Calendar::recurring / Calendar::workWeeks are derived from them here,
// both when Microsoft Project XML is read and when a .qplan is loaded.
//
// AGENT-NOTE (source of truth for the codes): MSPDI's Exception — Type 1
// daily (a plain date range, modelled as CalendarException), 2 yearly by
// day of the month, 3 yearly by position, 4 monthly by day of the month,
// 5 monthly by position, 6 weekly, 7 every N days, 8 by weekday count (not
// read); Period = every N; DaysOfWeek a mask with Sunday = 1 … Saturday =
// 64; MonthItem 0 day, 1 weekday, 2 weekend day, 3 Sunday … 9 Saturday;
// MonthPosition 0 first … 3 fourth, 4 last; Month 0 January … 11 December;
// MonthDay 1–31 (as MPXJ's MSPDI reader maps them).
#include "mspdi_keep.h"
#include "mspdi_p.h"

#include "plan_time.h"

#include <QJsonArray>
#include <QXmlStreamReader>

namespace QindaOffice::Plan::Mspdi {

namespace {

const DayHours kDefaultDay{{8 * 60, 12 * 60}, {13 * 60, 17 * 60}};

DayHours readTimes(QXmlStreamReader &in)
{
    DayHours hours;
    while (in.readNextStartElement()) {
        if (in.name() != u"WorkingTime") {
            in.skipCurrentElement();
            continue;
        }
        std::optional<int> from;
        std::optional<int> to;
        while (in.readNextStartElement()) {
            const QString text = in.readElementText(QXmlStreamReader::SkipChildElements);
            if (in.name() == u"FromTime") {
                from = parseTimeOfDay(text, false);
            } else if (in.name() == u"ToTime") {
                to = parseTimeOfDay(text, true);
            }
        }
        if (from && to && *to > *from) {
            hours.append({*from, *to});
        }
    }
    return hours;
}

void readPeriod(QXmlStreamReader &in, QDate *from, QDate *to)
{
    while (in.readNextStartElement()) {
        const QString text = in.readElementText(QXmlStreamReader::SkipChildElements);
        if (in.name() == u"FromDate") {
            *from = dateOf(parseWireDateTime(text));
        } else if (in.name() == u"ToDate") {
            *to = dateOf(parseWireDateTime(text));
        }
    }
}

int toInt(const QString &text, int fallback)
{
    bool ok = false;
    const int v = text.trimmed().toInt(&ok);
    return ok ? v : fallback;
}

} // namespace

std::optional<RecurringException> parseRecurringException(const QString &fragment, QString *problem)
{
    QXmlStreamReader in(fragment);
    if (!in.readNextStartElement()) {
        return std::nullopt;
    }
    RecurringException rx;
    int type = 1;
    int occurrences = 0;
    int mask = 0;
    int item = -1;
    bool working = false;
    bool hasTimes = false;
    DayHours hours;
    while (in.readNextStartElement()) {
        const QStringView n = in.name();
        if (n == u"TimePeriod") {
            readPeriod(in, &rx.from, &rx.to);
        } else if (n == u"WorkingTimes") {
            hours = readTimes(in);
            hasTimes = true;
        } else {
            const QString text = in.readElementText(QXmlStreamReader::SkipChildElements);
            if (n == u"Name") {
                rx.name = text.trimmed();
            } else if (n == u"Type") {
                type = toInt(text, 1);
            } else if (n == u"Occurrences") {
                occurrences = toInt(text, 0);
            } else if (n == u"Period") {
                rx.every = std::max(1, toInt(text, 1));
            } else if (n == u"DaysOfWeek") {
                mask = toInt(text, 0);
            } else if (n == u"MonthItem") {
                item = toInt(text, -1);
            } else if (n == u"MonthPosition") {
                rx.position = std::clamp(toInt(text, 0), 0, 4);
            } else if (n == u"Month") {
                rx.month = std::clamp(toInt(text, 0), 0, 11) + 1;
            } else if (n == u"MonthDay") {
                rx.day = std::clamp(toInt(text, 1), 1, 31);
            } else if (n == u"DayWorking") {
                working = text.trimmed() == QLatin1String("1") || text.trimmed().compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
            }
        }
    }
    rx.times = working ? (hasTimes && !hours.isEmpty() ? hours : kDefaultDay) : DayHours{};
    const auto unsupported = [&](const QString &why) {
        if (problem != nullptr) {
            *problem = why;
        }
        return std::optional<RecurringException>();
    };
    const bool byPosition = type == 3 || type == 5;
    switch (type) {
    case 2: rx.kind = RecurringException::Kind::YearlyByDay; break;
    case 3: rx.kind = RecurringException::Kind::YearlyByPosition; break;
    case 4: rx.kind = RecurringException::Kind::MonthlyByDay; break;
    case 5: rx.kind = RecurringException::Kind::MonthlyByPosition; break;
    case 6:
        rx.kind = RecurringException::Kind::Weekly;
        rx.daysOfWeek = ((mask >> 1) & 0x3F) | ((mask & 1) << 6); // Sunday-first → Monday-first
        break;
    case 7: rx.kind = RecurringException::Kind::EveryDays; break;
    default: return unsupported(QStringLiteral("its kind of recurrence (type %1)").arg(type));
    }
    if (byPosition) {
        if (item == 0) { // "the first day of …": a day of the month (last → its last day)
            rx.kind = type == 3 ? RecurringException::Kind::YearlyByDay : RecurringException::Kind::MonthlyByDay;
            rx.day = rx.position >= 4 ? 31 : rx.position + 1;
        } else if (item >= 3 && item <= 9) {
            rx.weekday = item == 3 ? 7 : item - 3;
        } else {
            return unsupported(QStringLiteral("a recurrence on the nth weekday or weekend day"));
        }
    }
    if (!rx.from.isValid()) {
        return unsupported(QStringLiteral("a recurrence without a start"));
    }
    if (!rx.to.isValid() && occurrences > 0) {
        // Project writes the last day; a writer that does not gets one
        // counted out (bounded).
        int seen = 0;
        for (QDate d = rx.from; seen < occurrences && rx.from.daysTo(d) < 36600; d = d.addDays(1)) {
            if (rx.fallsOn(d) && ++seen == occurrences) {
                rx.to = d;
            }
        }
    }
    if (!rx.to.isValid()) {
        rx.to = rx.from.addYears(100);
    }
    return rx;
}

QList<WorkWeek> parseWorkWeeks(const QString &fragment)
{
    QList<WorkWeek> out;
    QXmlStreamReader in(fragment);
    if (!in.readNextStartElement()) {
        return out;
    }
    while (in.readNextStartElement()) {
        if (in.name() != u"WorkWeek") {
            in.skipCurrentElement();
            continue;
        }
        WorkWeek ww;
        while (in.readNextStartElement()) {
            if (in.name() == u"TimePeriod") {
                readPeriod(in, &ww.from, &ww.to);
            } else if (in.name() == u"Name") {
                ww.name = in.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
            } else if (in.name() == u"WeekDays") {
                while (in.readNextStartElement()) {
                    if (in.name() != u"WeekDay") {
                        in.skipCurrentElement();
                        continue;
                    }
                    int dayType = -1;
                    bool working = false;
                    bool hasTimes = false;
                    DayHours hours;
                    while (in.readNextStartElement()) {
                        if (in.name() == u"WorkingTimes") {
                            hours = readTimes(in);
                            hasTimes = true;
                            continue;
                        }
                        const QString text = in.readElementText(QXmlStreamReader::SkipChildElements);
                        if (in.name() == u"DayType") {
                            dayType = toInt(text, -1);
                        } else if (in.name() == u"DayWorking") {
                            working = text.trimmed() == QLatin1String("1");
                        }
                    }
                    if (dayType >= 1 && dayType <= 7) {
                        ww.week[size_t(dayType == 1 ? 6 : dayType - 2)] =
                            working ? (hasTimes && !hours.isEmpty() ? hours : kDefaultDay) : DayHours{};
                    }
                }
            } else {
                in.skipCurrentElement();
            }
        }
        if (ww.from.isValid()) {
            out.append(ww);
        }
    }
    return out;
}

QStringList deriveRecurrences(Calendar &calendar)
{
    QStringList problems;
    calendar.recurring.clear();
    calendar.workWeeks.clear();
    for (const QJsonValue &v : calendar.extra.value(kExceptionsKey).toArray()) {
        QString why;
        const std::optional<RecurringException> rx = parseRecurringException(v.toString(), &why);
        if (rx) {
            calendar.recurring.append(*rx);
        } else if (!why.isEmpty()) {
            problems.append(why);
        }
    }
    for (const Kept &k : keptFromJson(calendar.extra.value(kKeptKey))) {
        if (k.name == QLatin1String("WorkWeeks")) {
            calendar.workWeeks += parseWorkWeeks(k.xml);
        }
    }
    return problems;
}

} // namespace QindaOffice::Plan::Mspdi
