// SPDX-License-Identifier: GPL-3.0-or-later
#include "baselines.h"

#include "work_calendar.h"

namespace QindaOffice::Plan {

namespace {
bool validNumber(int number)
{
    return number >= 0 && number < kBaselineCount;
}
} // namespace

int setBaseline(Project &project, int number, const QList<int> &taskUids)
{
    if (!validNumber(number)) {
        return 0;
    }
    int saved = 0;
    for (Task &t : project.tasks) {
        if (!taskUids.isEmpty() && !taskUids.contains(t.uid)) {
            continue;
        }
        BaselineData &b = t.baselines[size_t(number)];
        b.set = true;
        b.start = t.start;
        b.finish = t.finish;
        b.duration = t.duration;
        b.work = t.work;
        b.cost = t.cost;
        ++saved;
        for (Assignment &a : project.assignments) {
            if (a.taskUid != t.uid) {
                continue;
            }
            BaselineData &ab = a.baselines[size_t(number)];
            ab.set = true;
            ab.start = t.start;
            ab.finish = t.finish;
            ab.duration = t.duration;
            ab.work = a.work;
            ab.cost = a.cost;
        }
    }
    return saved;
}

int clearBaseline(Project &project, int number, const QList<int> &taskUids)
{
    if (!validNumber(number)) {
        return 0;
    }
    int cleared = 0;
    for (Task &t : project.tasks) {
        if (!taskUids.isEmpty() && !taskUids.contains(t.uid)) {
            continue;
        }
        for (Assignment &a : project.assignments) {
            if (a.taskUid == t.uid) {
                a.baselines[size_t(number)] = BaselineData{};
            }
        }
        if (!t.baselines[size_t(number)].set) {
            continue;
        }
        t.baselines[size_t(number)] = BaselineData{};
        ++cleared;
    }
    return cleared;
}

QString baselineName(int number)
{
    return number <= 0 ? QStringLiteral("Baseline") : QStringLiteral("Baseline %1").arg(number);
}

bool hasBaseline(const Project &project, int number)
{
    if (!validNumber(number)) {
        return false;
    }
    for (const Task &t : project.tasks) {
        if (t.baselines[size_t(number)].set) {
            return true;
        }
    }
    return false;
}

Variance variance(const Project &project, const Task &task, int number)
{
    Variance v;
    if (!validNumber(number) || !task.baselines[size_t(number)].set) {
        return v;
    }
    const BaselineData &b = task.baselines[size_t(number)];
    const WorkCalendar cal(project, project.info.calendarUid);
    v.valid = true;
    v.start = cal.workBetween(b.start, task.start);
    v.finish = cal.workBetween(b.finish, task.finish);
    v.duration = task.duration - b.duration;
    v.work = task.work - b.work;
    v.cost = task.cost - b.cost;
    return v;
}

} // namespace QindaOffice::Plan
