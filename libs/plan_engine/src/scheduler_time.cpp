// SPDX-License-Identifier: GPL-3.0-or-later
// The scheduler's clocks (M83, D-092): a task's length with its split gaps,
// elapsed durations on the wall clock and lags in working or elapsed time
// (scheduler_p.h declares them; scheduler.cpp and scheduler_rollup.cpp use
// them for every date the passes compute).
//
// AGENT-NOTE (Project's semantics): an elapsed duration ("3ed") runs around
// the clock from its start, through nights and weekends, so its start is not
// moved to working time; a split gap is working time on the task's calendar
// (wall-clock for an elapsed task) and a split moves with its task; an
// elapsed lag ("2ed", MSPDI LagFormat 4–12 even) is wall-clock time and a
// working lag is project working time, as before M83.
#include "scheduler_p.h"

#include <algorithm>

namespace QindaOffice::Plan::Detail {

Minutes SchedulePass::lagged(const LinkRef &ref, Minutes at, bool back) const
{
    const Minutes lag = lagOf(ref) * (back ? -1 : 1);
    if (lag == 0) {
        return at;
    }
    const Link &link = m_project.tasks.at(ref.successor).predecessors.at(ref.link);
    return link.lagElapsed ? at + lag : m_projectCal.add(at, lag);
}

Minutes SchedulePass::length(int taskIndex) const
{
    const Task &t = m_project.tasks.at(taskIndex);
    Minutes total = std::max<Minutes>(0, t.duration);
    for (const TaskSplit &s : t.splits) {
        if (s.at > 0 && s.at < t.duration && s.gap > 0) {
            total += s.gap;
        }
    }
    return total;
}

Minutes SchedulePass::after(int taskIndex, Minutes start, Minutes length) const
{
    if (length <= 0) {
        return start;
    }
    return m_project.tasks.at(taskIndex).durationElapsed ? start + length : cal(taskIndex).add(start, length);
}

Minutes SchedulePass::before(int taskIndex, Minutes finish, Minutes length) const
{
    if (length <= 0) {
        return finish;
    }
    return m_project.tasks.at(taskIndex).durationElapsed ? finish - length : cal(taskIndex).subtract(finish, length);
}

Minutes SchedulePass::snap(int taskIndex, Minutes instant) const
{
    return m_project.tasks.at(taskIndex).durationElapsed ? instant : cal(taskIndex).snapForward(instant);
}

} // namespace QindaOffice::Plan::Detail
