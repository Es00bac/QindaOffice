// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// WorkCalendar: a Calendar resolved against its base chain, answering the
// scheduler's working-time questions. Cheap to copy (the day cache is
// shared); not thread-safe (the cache is mutable) — one per thread.
//
// AGENT-CONTRACT (resolution order for a date, documented in docs/plan.md):
//   the calendar's own exception, then its bases' exceptions (a project
//   holiday is a holiday for every resource), then the calendar's own week
//   day, then its bases' week day; nothing = non-working.
//
// AGENT-CONTRACT (instant arithmetic):
//   add(s, w)       the earliest t with working minutes in [s, t) == w, so a
//                   day's work that starts at 08:00 ends at 17:00 the same
//                   day, never at 08:00 the next working day;
//   subtract(f, w)  the latest s with working minutes in [s, f) == w;
//   snapForward(t)  t when t is inside a working interval [from, to),
//                   else the next interval's start;
//   snapBackward(t) t when t is inside (from, to], else the previous end;
//   workBetween(a,b) signed working minutes between the instants.
// A calendar with no working time at all (every day off) is replaced by the
// standard week so no loop can run forever; `degenerate()` reports it.

#include "plan_types.h"

#include <QHash>

#include <memory>

namespace QindaOffice::Plan {

class WorkCalendar {
public:
    // The standard week (Mon–Fri 08–12, 13–17), no exceptions.
    WorkCalendar();
    // `uid` resolved in `project`; an unknown uid resolves the project
    // calendar, and an unknown project calendar the standard week.
    WorkCalendar(const Project &project, int uid);
    explicit WorkCalendar(const QList<Calendar> &chain); // chain[0] is the calendar, then its bases

    [[nodiscard]] const DayHours &hours(const QDate &date) const;
    [[nodiscard]] Minutes minutesOn(const QDate &date) const;
    [[nodiscard]] bool isWorkingDay(const QDate &date) const { return minutesOn(date) > 0; }
    [[nodiscard]] Minutes snapForward(Minutes t) const;
    [[nodiscard]] Minutes snapBackward(Minutes t) const;
    [[nodiscard]] Minutes add(Minutes start, Minutes work) const;
    [[nodiscard]] Minutes subtract(Minutes finish, Minutes work) const;
    [[nodiscard]] Minutes workBetween(Minutes a, Minutes b) const;
    // The first working instant of `date` (or of the next working day).
    [[nodiscard]] Minutes dayStart(const QDate &date) const;
    // The last working instant of `date` (or of the previous working day).
    [[nodiscard]] Minutes dayFinish(const QDate &date) const;
    [[nodiscard]] bool degenerate() const { return m_degenerate; }
    // Working minutes of the resolved week day (0 = Monday), exceptions aside.
    [[nodiscard]] Minutes weekMinutes(int dayOfWeek) const;

private:
    void resolveWeek();
    [[nodiscard]] const DayHours &hoursForDay(qint64 day) const;

    QList<Calendar> m_chain;
    std::array<DayHours, 7> m_week; // resolved week (no exceptions)
    bool m_degenerate = false;
    std::shared_ptr<QHash<qint64, DayHours>> m_cache;
};

} // namespace QindaOffice::Plan
