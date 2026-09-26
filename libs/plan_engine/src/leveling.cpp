// SPDX-License-Identifier: GPL-3.0-or-later
#include "leveling.h"

#include "plan_time.h"
#include "resources.h"
#include "scheduler.h"
#include "work_calendar.h"

#include <QSet>

#include <algorithm>

namespace QindaOffice::Plan {

namespace {

constexpr int kMaxSteps = 2000;

// Leaf tasks the resource works on during `day`.
QList<int> tasksWorkingOn(const Project &project, int resourceUid, const QDate &day)
{
    QList<int> out;
    const Minutes dayStart = toMinutes(day, 0);
    const Minutes dayEnd = toMinutes(day.addDays(1), 0);
    for (const Assignment &a : project.assignments) {
        if (a.resourceUid != resourceUid) {
            continue;
        }
        const int ti = project.taskIndex(a.taskUid);
        if (ti < 0 || out.contains(ti)) {
            continue;
        }
        const Task &t = project.tasks.at(ti);
        if (!t.effectiveActive || t.summary || t.start == kNoDate || t.finish <= dayStart || t.start >= dayEnd) {
            continue;
        }
        out.append(ti);
    }
    return out;
}

bool movable(const Task &t)
{
    return !t.manual && t.actualStart == kNoDate && t.percentComplete == 0
        && t.constraint != ConstraintType::MustStartOn && t.constraint != ConstraintType::MustFinishOn;
}

// true: the resource was leveled (or nothing could be done on a day that was
// then recorded and skipped); the loop stops at kMaxSteps.
void levelOne(Project &project, int resourceUid, LevelingResult &result, QSet<int> &delayed)
{
    QDate from;
    while (result.steps < kMaxSteps) {
        const QDate day = firstOverallocatedDay(project, resourceUid, from);
        if (!day.isValid()) {
            return;
        }
        QList<int> working = tasksWorkingOn(project, resourceUid, day);
        const auto keepOrder = [&](int a, int b) {
            const Task &ta = project.tasks.at(a);
            const Task &tb = project.tasks.at(b);
            if (ta.totalSlack != tb.totalSlack) {
                return ta.totalSlack < tb.totalSlack;
            }
            if (ta.start != tb.start) {
                return ta.start < tb.start;
            }
            return ta.id < tb.id;
        };
        std::sort(working.begin(), working.end(), keepOrder);
        QList<int> candidates;
        for (int k = 1; k < working.size(); ++k) {
            if (movable(project.tasks.at(working.at(k)))) {
                candidates.append(working.at(k));
            }
        }
        if (working.size() < 2 || candidates.isEmpty()) {
            result.unresolvedDays.append(day.toString(Qt::ISODate));
            from = day.addDays(1);
            continue;
        }
        const Task &keep = project.tasks.at(working.first());
        // The delayed task starts once the kept one is done, and at least
        // the next working day after D.
        const auto floorFor = [&](int ti) {
            const WorkCalendar c(project, effectiveCalendarUid(project, ti));
            return std::max(c.snapForward(toMinutes(day.addDays(1), 0)), c.snapForward(keep.finish));
        };
        const auto shiftFor = [&](int ti) {
            const WorkCalendar c(project, effectiveCalendarUid(project, ti));
            return c.workBetween(project.tasks.at(ti).start, floorFor(ti));
        };
        int chosen = -1;
        for (int ti : std::as_const(candidates)) { // pass 1: within slack
            if (project.tasks.at(ti).totalSlack >= shiftFor(ti)
                && (chosen < 0 || project.tasks.at(ti).totalSlack > project.tasks.at(chosen).totalSlack)) {
                chosen = ti;
            }
        }
        if (chosen < 0) { // pass 2: the most slack, the finish moves
            for (int ti : std::as_const(candidates)) {
                if (chosen < 0 || project.tasks.at(ti).totalSlack > project.tasks.at(chosen).totalSlack) {
                    chosen = ti;
                }
            }
            result.finishMoved = true;
        }
        Task &t = project.tasks[chosen];
        const Minutes floor = floorFor(chosen);
        if (t.levelingFloor != kNoDate && t.levelingFloor >= floor) {
            // Already delayed at least this far and still overlapping: give
            // up on this day rather than loop.
            result.unresolvedDays.append(day.toString(Qt::ISODate));
            from = day.addDays(1);
            continue;
        }
        t.levelingFloor = floor;
        delayed.insert(t.uid);
        ++result.steps;
        schedule(project);
    }
}

} // namespace

LevelingResult levelResources(Project &project, int resourceUid)
{
    schedule(project);
    LevelingResult result;
    QSet<int> delayed;
    QList<int> targets;
    for (const Resource &r : std::as_const(project.resources)) {
        if (r.type == ResourceType::Work && (resourceUid <= 0 || r.uid == resourceUid)) {
            targets.append(r.uid);
        }
    }
    for (int uid : std::as_const(targets)) {
        levelOne(project, uid, result, delayed);
    }
    result.delayedTasks = int(delayed.size());
    result.resolved = true;
    for (int uid : std::as_const(targets)) {
        const int ri = project.resourceIndex(uid);
        result.resolved = result.resolved && ri >= 0 && !project.resources.at(ri).overallocated;
    }
    return result;
}

int clearLeveling(Project &project, const QList<int> &taskUids)
{
    int cleared = 0;
    for (Task &t : project.tasks) {
        if (t.levelingFloor != kNoDate && (taskUids.isEmpty() || taskUids.contains(t.uid))) {
            t.levelingFloor = kNoDate;
            ++cleared;
        }
    }
    schedule(project);
    return cleared;
}

} // namespace QindaOffice::Plan
