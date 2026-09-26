// SPDX-License-Identifier: GPL-3.0-or-later
#include "resources.h"

#include "plan_time.h"
#include "scheduler.h"
#include "work_calendar.h"

#include <QSet>

#include <algorithm>

namespace QindaOffice::Plan {

namespace {

const QDate kEpoch(1970, 1, 1);

class Calendars {
public:
    explicit Calendars(const Project &project)
        : m_project(project)
    {
    }
    const WorkCalendar &get(int uid)
    {
        auto it = m_cache.find(uid);
        if (it == m_cache.end()) {
            it = m_cache.insert(uid, WorkCalendar(m_project, uid));
        }
        return *it;
    }
    const WorkCalendar &forResource(const Resource &r)
    {
        return get(r.calendarUid >= 0 && m_project.calendarIndex(r.calendarUid) >= 0 ? r.calendarUid
                                                                                       : m_project.info.calendarUid);
    }

private:
    const Project &m_project;
    QHash<int, WorkCalendar> m_cache;
};

// Working minutes per day of one assignment (not yet × units).
QMap<qint64, Minutes> assignmentDays(const Project &project, const Assignment &a, Calendars &calendars,
                                     const QHash<int, int> &taskIndex)
{
    QMap<qint64, Minutes> out;
    const int ti = taskIndex.value(a.taskUid, -1);
    if (ti < 0) {
        return out;
    }
    const Task &t = project.tasks.at(ti);
    if (!t.effectiveActive || t.summary || t.start == kNoDate || t.finish <= t.start) {
        return out;
    }
    const WorkCalendar &c = calendars.get(effectiveCalendarUid(project, ti));
    const qint64 first = kEpoch.daysTo(dateOf(t.start));
    const qint64 last = kEpoch.daysTo(dateOf(t.finish));
    for (qint64 day = first; day <= last; ++day) {
        const Minutes lo = std::max(t.start, day * 1440);
        const Minutes hi = std::min(t.finish, (day + 1) * 1440);
        const Minutes m = hi > lo ? c.workBetween(lo, hi) : 0;
        if (m > 0) {
            out.insert(day, m);
        }
    }
    return out;
}

QHash<int, int> taskIndexByUid(const Project &project)
{
    QHash<int, int> out;
    for (int i = 0; i < project.tasks.size(); ++i) {
        out.insert(project.tasks.at(i).uid, i);
    }
    return out;
}

bool isWork(const Project &project, int resourceUid)
{
    const int r = project.resourceIndex(resourceUid);
    return r >= 0 && project.resources.at(r).type == ResourceType::Work;
}

} // namespace

DailyLoad dailyLoad(const Project &project)
{
    DailyLoad load;
    Calendars calendars(project);
    const QHash<int, int> index = taskIndexByUid(project);
    for (const Assignment &a : project.assignments) {
        if (!isWork(project, a.resourceUid)) {
            continue;
        }
        const QMap<qint64, Minutes> days = assignmentDays(project, a, calendars, index);
        QMap<qint64, qint64> &into = load[a.resourceUid];
        for (auto it = days.cbegin(); it != days.cend(); ++it) {
            into[it.key()] += it.value() * a.units;
        }
    }
    return load;
}

namespace {
QSet<qint64> overDays(const Project &project, const Resource &r, const QMap<qint64, qint64> &load, Calendars &calendars)
{
    QSet<qint64> over;
    const WorkCalendar &c = calendars.forResource(r);
    for (auto it = load.cbegin(); it != load.cend(); ++it) {
        const qint64 capacity = c.minutesOn(kEpoch.addDays(it.key())) * qint64(r.maxUnits);
        if (it.value() > capacity) {
            over.insert(it.key());
        }
    }
    Q_UNUSED(project)
    return over;
}
} // namespace

void computeOverallocation(Project &project)
{
    const DailyLoad load = dailyLoad(project);
    Calendars calendars(project);
    const QHash<int, int> index = taskIndexByUid(project);
    for (Task &t : project.tasks) {
        t.overallocated = false;
    }
    for (Resource &r : project.resources) {
        r.overallocated = false;
        if (r.type != ResourceType::Work || !load.contains(r.uid)) {
            continue;
        }
        const QSet<qint64> over = overDays(project, r, load.value(r.uid), calendars);
        r.overallocated = !over.isEmpty();
        if (over.isEmpty()) {
            continue;
        }
        for (const Assignment &a : std::as_const(project.assignments)) {
            if (a.resourceUid != r.uid) {
                continue;
            }
            const QMap<qint64, Minutes> days = assignmentDays(project, a, calendars, index);
            for (auto it = days.cbegin(); it != days.cend(); ++it) {
                if (over.contains(it.key())) {
                    project.tasks[index.value(a.taskUid)].overallocated = true;
                    break;
                }
            }
        }
    }
}

QDate firstOverallocatedDay(const Project &project, int resourceUid, const QDate &from)
{
    const int ri = project.resourceIndex(resourceUid);
    if (ri < 0 || project.resources.at(ri).type != ResourceType::Work) {
        return {};
    }
    const DailyLoad load = dailyLoad(project);
    Calendars calendars(project);
    const QSet<qint64> over = overDays(project, project.resources.at(ri), load.value(resourceUid), calendars);
    bool found = false;
    qint64 best = 0;
    const qint64 floor = from.isValid() ? kEpoch.daysTo(from) : std::numeric_limits<qint64>::min();
    for (qint64 day : over) {
        if (day >= floor && (!found || day < best)) {
            best = day;
            found = true;
        }
    }
    return found ? kEpoch.addDays(best) : QDate();
}

UsageTable usageTable(const Project &project, const QDate &from, int count, bool weeks)
{
    UsageTable table;
    count = std::clamp(count, 1, 256);
    table.bucketDays = weeks ? 7 : 1;
    QDate start = from;
    if (weeks) {
        start = start.addDays(1 - start.dayOfWeek());
    }
    for (int b = 0; b < count; ++b) {
        table.bucketStarts.append(start.addDays(qint64(b) * table.bucketDays));
    }
    const qint64 firstDay = kEpoch.daysTo(start);
    const qint64 endDay = firstDay + qint64(count) * table.bucketDays;
    const auto bucketOf = [&](qint64 day) { return int((day - firstDay) / table.bucketDays); };

    Calendars calendars(project);
    const QHash<int, int> index = taskIndexByUid(project);
    const DailyLoad load = dailyLoad(project);
    for (const Resource &r : project.resources) {
        if (r.type != ResourceType::Work) {
            continue;
        }
        UsageRow row;
        row.id = QStringLiteral("r:%1").arg(r.uid);
        row.resourceUid = r.uid;
        row.name = r.name;
        row.work = QList<Minutes>(count, 0);
        row.capacity = QList<Minutes>(count, 0);
        row.over = QList<bool>(count, false);
        const WorkCalendar &c = calendars.forResource(r);
        const QMap<qint64, qint64> days = load.value(r.uid);
        const QSet<qint64> over = overDays(project, r, days, calendars);
        QHash<Minutes, int> capacityVotes;
        for (qint64 day = firstDay; day < endDay; ++day) {
            row.capacity[bucketOf(day)] += divRound(c.minutesOn(kEpoch.addDays(day)) * r.maxUnits, 100);
        }
        for (Minutes cap : std::as_const(row.capacity)) {
            ++capacityVotes[cap];
        }
        int best = -1;
        for (auto it = capacityVotes.cbegin(); it != capacityVotes.cend(); ++it) {
            if (it.value() > best || (it.value() == best && it.key() > row.capacityPerBucket)) {
                best = it.value();
                row.capacityPerBucket = it.key();
            }
        }
        for (auto it = days.cbegin(); it != days.cend(); ++it) {
            row.total += divRound(it.value(), 100);
            if (it.key() < firstDay || it.key() >= endDay) {
                continue;
            }
            row.work[bucketOf(it.key())] += divRound(it.value(), 100);
            if (over.contains(it.key())) {
                row.over[bucketOf(it.key())] = true;
            }
        }
        table.rows.append(row);
        for (const Assignment &a : project.assignments) {
            if (a.resourceUid != r.uid) {
                continue;
            }
            const int ti = index.value(a.taskUid, -1);
            if (ti < 0) {
                continue;
            }
            UsageRow ar;
            ar.id = QStringLiteral("a:%1").arg(a.uid);
            ar.resourceUid = r.uid;
            ar.taskUid = a.taskUid;
            ar.assignment = true;
            ar.name = project.tasks.at(ti).name;
            ar.capacityPerBucket = row.capacityPerBucket;
            ar.work = QList<Minutes>(count, 0);
            ar.capacity = row.capacity;
            ar.over = QList<bool>(count, false);
            const QMap<qint64, Minutes> adays = assignmentDays(project, a, calendars, index);
            for (auto it = adays.cbegin(); it != adays.cend(); ++it) {
                const Minutes m = divRound(it.value() * a.units, 100);
                ar.total += m;
                if (it.key() >= firstDay && it.key() < endDay) {
                    ar.work[bucketOf(it.key())] += m;
                }
            }
            table.rows.append(ar);
        }
    }
    return table;
}

} // namespace QindaOffice::Plan
