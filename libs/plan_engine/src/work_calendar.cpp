// SPDX-License-Identifier: GPL-3.0-or-later
#include "work_calendar.h"

#include "plan_time.h"

#include <algorithm>

namespace QindaOffice::Plan {

namespace {

// AGENT-GUARD: every day loop is bounded by this horizon (~100 years), so a
// calendar whose only working time is one exception day can never spin.
constexpr qint64 kHorizonDays = 36600;
const QDate kEpoch(1970, 1, 1);

qint64 dayOf(Minutes t)
{
    qint64 q = t / 1440;
    if (t % 1440 != 0 && t < 0) {
        --q;
    }
    return q;
}

DayHours normalized(DayHours hours)
{
    DayHours out;
    std::sort(hours.begin(), hours.end(), [](const WorkingTime &a, const WorkingTime &b) { return a.from < b.from; });
    for (WorkingTime wt : hours) {
        wt.from = std::clamp(wt.from, 0, 1440);
        wt.to = std::clamp(wt.to, 0, 1440);
        if (wt.to <= wt.from) {
            continue;
        }
        if (!out.isEmpty() && wt.from <= out.last().to) {
            out.last().to = std::max(out.last().to, wt.to);
        } else {
            out.append(wt);
        }
    }
    return out;
}

Minutes total(const DayHours &hours)
{
    Minutes sum = 0;
    for (const WorkingTime &wt : hours) {
        sum += wt.to - wt.from;
    }
    return sum;
}

} // namespace

WorkCalendar::WorkCalendar()
    : WorkCalendar(QList<Calendar>{standardCalendar(1)})
{
}

WorkCalendar::WorkCalendar(const Project &project, int uid)
{
    int index = project.calendarIndex(uid);
    if (index < 0) {
        index = project.calendarIndex(project.info.calendarUid);
    }
    if (index < 0) {
        m_chain.append(standardCalendar(1));
    } else {
        QList<int> seen;
        while (index >= 0 && !seen.contains(index) && seen.size() < 8) {
            seen.append(index);
            m_chain.append(project.calendars.at(index));
            const int base = project.calendars.at(index).baseUid;
            index = base >= 0 ? project.calendarIndex(base) : -1;
        }
    }
    resolveWeek();
}

WorkCalendar::WorkCalendar(const QList<Calendar> &chain)
    : m_chain(chain)
{
    if (m_chain.isEmpty()) {
        m_chain.append(standardCalendar(1));
    }
    resolveWeek();
}

void WorkCalendar::resolveWeek()
{
    m_cache = std::make_shared<QHash<qint64, DayHours>>();
    Minutes weekTotal = 0;
    for (int dow = 0; dow < 7; ++dow) {
        m_week[size_t(dow)] = {};
        for (const Calendar &cal : std::as_const(m_chain)) {
            if (cal.week[size_t(dow)].has_value()) {
                m_week[size_t(dow)] = normalized(*cal.week[size_t(dow)]);
                break;
            }
        }
        weekTotal += total(m_week[size_t(dow)]);
    }
    bool exceptionWork = false;
    for (const Calendar &cal : std::as_const(m_chain)) {
        for (const CalendarException &ex : cal.exceptions) {
            exceptionWork = exceptionWork || total(normalized(ex.times)) > 0;
        }
    }
    m_degenerate = weekTotal == 0 && !exceptionWork;
    if (m_degenerate) {
        const Calendar standard = standardCalendar(1);
        for (int dow = 0; dow < 7; ++dow) {
            m_week[size_t(dow)] = *standard.week[size_t(dow)];
        }
    }
}

const DayHours &WorkCalendar::hoursForDay(qint64 day) const
{
    const auto it = m_cache->constFind(day);
    if (it != m_cache->constEnd()) {
        return *it;
    }
    const QDate date = kEpoch.addDays(day);
    DayHours result = m_week[size_t(date.dayOfWeek() - 1)];
    if (!m_degenerate) {
        bool found = false;
        for (const Calendar &cal : std::as_const(m_chain)) {
            for (const CalendarException &ex : cal.exceptions) {
                if (ex.from.isValid() && date >= ex.from && date <= (ex.to.isValid() ? ex.to : ex.from)) {
                    result = normalized(ex.times);
                    found = true;
                    break;
                }
            }
            if (found) {
                break;
            }
        }
    }
    return *m_cache->insert(day, result);
}

const DayHours &WorkCalendar::hours(const QDate &date) const
{
    return hoursForDay(kEpoch.daysTo(date));
}

Minutes WorkCalendar::minutesOn(const QDate &date) const
{
    return total(hours(date));
}

Minutes WorkCalendar::weekMinutes(int dayOfWeek) const
{
    return dayOfWeek >= 0 && dayOfWeek < 7 ? total(m_week[size_t(dayOfWeek)]) : 0;
}

Minutes WorkCalendar::snapForward(Minutes t) const
{
    if (t == kNoDate) {
        return t;
    }
    const qint64 first = dayOf(t);
    const int m = int(t - first * 1440);
    for (qint64 day = first; day < first + kHorizonDays; ++day) {
        for (const WorkingTime &iv : hoursForDay(day)) {
            if (day == first) {
                if (m >= iv.from && m < iv.to) {
                    return t;
                }
                if (m < iv.from) {
                    return day * 1440 + iv.from;
                }
            } else {
                return day * 1440 + iv.from;
            }
        }
    }
    return t;
}

Minutes WorkCalendar::snapBackward(Minutes t) const
{
    if (t == kNoDate) {
        return t;
    }
    const qint64 first = dayOf(t);
    const int m = int(t - first * 1440);
    for (qint64 day = first; day > first - kHorizonDays; --day) {
        const DayHours &hours = hoursForDay(day);
        for (auto it = hours.crbegin(); it != hours.crend(); ++it) {
            if (day == first) {
                if (m > it->from && m <= it->to) {
                    return t;
                }
                if (m > it->to) {
                    return day * 1440 + it->to;
                }
            } else {
                return day * 1440 + it->to;
            }
        }
    }
    return t;
}

Minutes WorkCalendar::add(Minutes start, Minutes work) const
{
    if (start == kNoDate || work == 0) {
        return start;
    }
    if (work < 0) {
        return subtract(start, -work);
    }
    const qint64 first = dayOf(start);
    const int m = int(start - first * 1440);
    Minutes remaining = work;
    for (qint64 day = first; day < first + kHorizonDays; ++day) {
        for (const WorkingTime &iv : hoursForDay(day)) {
            const int from = day == first ? std::max(iv.from, m) : iv.from;
            if (from >= iv.to) {
                continue;
            }
            const Minutes available = iv.to - from;
            if (remaining <= available) {
                return day * 1440 + from + remaining;
            }
            remaining -= available;
        }
    }
    return start;
}

Minutes WorkCalendar::subtract(Minutes finish, Minutes work) const
{
    if (finish == kNoDate || work == 0) {
        return finish;
    }
    if (work < 0) {
        return add(finish, -work);
    }
    qint64 first = dayOf(finish);
    int m = int(finish - first * 1440);
    if (m == 0) {
        // Midnight belongs to the end of the previous day (24:00).
        --first;
        m = 1440;
    }
    Minutes remaining = work;
    for (qint64 day = first; day > first - kHorizonDays; --day) {
        const DayHours &hours = hoursForDay(day);
        for (auto it = hours.crbegin(); it != hours.crend(); ++it) {
            const int to = day == first ? std::min(it->to, m) : it->to;
            if (to <= it->from) {
                continue;
            }
            const Minutes available = to - it->from;
            if (remaining <= available) {
                return day * 1440 + to - remaining;
            }
            remaining -= available;
        }
    }
    return finish;
}

Minutes WorkCalendar::workBetween(Minutes a, Minutes b) const
{
    if (a == kNoDate || b == kNoDate || a == b) {
        return 0;
    }
    if (a > b) {
        return -workBetween(b, a);
    }
    const qint64 firstDay = dayOf(a);
    const qint64 lastDay = dayOf(b);
    const int ma = int(a - firstDay * 1440);
    const int mb = int(b - lastDay * 1440);
    Minutes sum = 0;
    for (qint64 day = firstDay; day <= lastDay && day < firstDay + kHorizonDays; ++day) {
        const int lo = day == firstDay ? ma : 0;
        const int hi = day == lastDay ? mb : 1440;
        for (const WorkingTime &iv : hoursForDay(day)) {
            const int from = std::max(iv.from, lo);
            const int to = std::min(iv.to, hi);
            if (to > from) {
                sum += to - from;
            }
        }
    }
    return sum;
}

Minutes WorkCalendar::dayStart(const QDate &date) const
{
    return snapForward(toMinutes(date, 0));
}

Minutes WorkCalendar::dayFinish(const QDate &date) const
{
    return snapBackward(toMinutes(date.addDays(1), 0));
}

} // namespace QindaOffice::Plan
