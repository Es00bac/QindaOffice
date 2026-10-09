// SPDX-License-Identifier: GPL-3.0-or-later
#include "leveling.h"

#include "plan_time.h"
#include "resources.h"
#include "scheduler.h"
#include "work_calendar.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace QindaOffice::Plan {

namespace {

constexpr int kMaxSteps = 2000;
constexpr int kDoNotLevel = 1000; // Project: priority 1000 is never leveled

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

bool movable(const Task &t, const QSet<int> &selection)
{
    return !t.manual && t.actualStart == kNoDate && t.percentComplete == 0
        && t.constraint != ConstraintType::MustStartOn && t.constraint != ConstraintType::MustFinishOn
        && t.priority < kDoNotLevel && (selection.isEmpty() || selection.contains(t.uid));
}

// Which task keeps its dates first (leveling.h has the orders).
bool keepsBefore(const Task &a, const Task &b, LevelingOrder order)
{
    if (order == LevelingOrder::IdOnly) {
        return a.id < b.id;
    }
    if (order == LevelingOrder::PriorityStandard && a.priority != b.priority) {
        return a.priority > b.priority;
    }
    if (a.totalSlack != b.totalSlack) {
        return a.totalSlack < b.totalSlack;
    }
    if (a.start != b.start) {
        return a.start < b.start;
    }
    if (a.priority != b.priority) {
        return a.priority > b.priority;
    }
    return a.id < b.id;
}

// Is `a` a better task to delay than `b`? Equal ones keep the first found
// (the earlier in keeping order), as leveling always has (M51).
bool delaysBefore(const Task &a, const Task &b, LevelingOrder order)
{
    if (order == LevelingOrder::IdOnly) {
        return a.id > b.id;
    }
    if (order == LevelingOrder::PriorityStandard && a.priority != b.priority) {
        return a.priority < b.priority;
    }
    if (a.totalSlack != b.totalSlack) {
        return a.totalSlack > b.totalSlack;
    }
    return a.priority < b.priority;
}

struct Run {
    Project &project;
    const LevelingOptions &options;
    QSet<int> selection;
    LevelingResult result;
    QSet<int> delayed;
};

// One overbooked day: delays one task, or records the day as unresolved.
// Returns false when the day could not be helped.
bool levelDay(Run &run, int resourceUid, const QDate &day)
{
    Project &project = run.project;
    const LevelingOrder order = run.options.order;
    QList<int> working = tasksWorkingOn(project, resourceUid, day);
    std::sort(working.begin(), working.end(),
              [&](int a, int b) { return keepsBefore(project.tasks.at(a), project.tasks.at(b), order); });
    // A task that cannot move keeps its dates whatever the order says.
    int kept = -1;
    for (int ti : std::as_const(working)) {
        if (!movable(project.tasks.at(ti), run.selection)) {
            kept = ti;
            break;
        }
    }
    if (kept < 0 && !working.isEmpty()) {
        kept = working.first();
    }
    QList<int> candidates;
    for (int ti : std::as_const(working)) {
        if (ti != kept && movable(project.tasks.at(ti), run.selection)) {
            candidates.append(ti);
        }
    }
    if (working.size() < 2 || candidates.isEmpty()) {
        return false;
    }
    const Task &keep = project.tasks.at(kept);
    // The delayed task starts once the kept one is done, and at least the
    // next working day after D.
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
            && (chosen < 0 || delaysBefore(project.tasks.at(ti), project.tasks.at(chosen), order))) {
            chosen = ti;
        }
    }
    if (chosen < 0 && !run.options.withinSlackOnly) { // pass 2: the finish moves
        for (int ti : std::as_const(candidates)) {
            if (chosen < 0 || delaysBefore(project.tasks.at(ti), project.tasks.at(chosen), order)) {
                chosen = ti;
            }
        }
        if (chosen >= 0) {
            run.result.finishMoved = true;
        }
    }
    if (chosen < 0) {
        return false;
    }
    Task &t = project.tasks[chosen];
    const Minutes floor = floorFor(chosen);
    if (t.start >= floor || t.unleveledStart == kNoDate) {
        return false; // already that late and still overlapping: give up on D
    }
    // AGENT-NOTE: the delay is Project's — elapsed time from the task's
    // unleveled early start (scheduler.cpp) to where it now has to start,
    // so MSPDI's LevelingDelay (edays) reproduces the same start in Project.
    t.levelingDelay = std::max<Minutes>(0, floor - t.unleveledStart);
    t.levelingDelayElapsed = true;
    run.delayed.insert(t.uid);
    ++run.result.steps;
    schedule(project);
    return true;
}

void levelOne(Run &run, int resourceUid)
{
    QDate from = run.options.from;
    while (run.result.steps < kMaxSteps) {
        const QDate day = firstOverallocatedDay(run.project, resourceUid, from);
        if (!day.isValid() || (run.options.to.isValid() && day > run.options.to)) {
            return;
        }
        if (!levelDay(run, resourceUid, day)) {
            run.result.unresolvedDays.append(day.toString(Qt::ISODate));
            from = day.addDays(1);
        }
    }
}

} // namespace

LevelingOptions projectLevelingOptions(const Project &project, int resourceUid)
{
    LevelingOptions o;
    o.resourceUid = resourceUid;
    o.order = project.info.levelingOrder;
    o.withinSlackOnly = project.info.levelWithinSlack;
    o.clearFirst = project.info.levelClearFirst;
    return o;
}

LevelingResult levelResources(Project &project, const LevelingOptions &options)
{
    Run run{project, options, {}, {}, {}};
    for (int uid : options.taskUids) {
        run.selection.insert(uid);
    }
    if (options.clearFirst) {
        // Project's "Clear leveling values before leveling": only the tasks
        // this run may move start over.
        for (Task &t : project.tasks) {
            if (run.selection.isEmpty() || run.selection.contains(t.uid)) {
                t.levelingDelay = 0;
                t.levelingFloor = kNoDate;
            }
        }
    }
    schedule(project);
    for (Task &t : project.tasks) {
        if (!t.summary && (run.selection.isEmpty() || run.selection.contains(t.uid))) {
            t.preLeveledStart = t.start;
            t.preLeveledFinish = t.finish;
        }
    }
    QList<int> targets;
    for (const Resource &r : std::as_const(project.resources)) {
        if (r.type == ResourceType::Work && (options.resourceUid <= 0 || r.uid == options.resourceUid)) {
            targets.append(r.uid);
        }
    }
    for (int uid : std::as_const(targets)) {
        levelOne(run, uid);
    }
    run.result.delayedTasks = int(run.delayed.size());
    run.result.resolved = true;
    for (int uid : std::as_const(targets)) {
        const QDate left = firstOverallocatedDay(project, uid, options.from);
        run.result.resolved = run.result.resolved && (!left.isValid() || (options.to.isValid() && left > options.to));
    }
    return run.result;
}

LevelingResult levelResources(Project &project, int resourceUid)
{
    return levelResources(project, projectLevelingOptions(project, resourceUid));
}

int clearLeveling(Project &project, const QList<int> &taskUids)
{
    int cleared = 0;
    for (Task &t : project.tasks) {
        if (!taskUids.isEmpty() && !taskUids.contains(t.uid)) {
            continue;
        }
        if (t.levelingDelay != 0 || t.levelingFloor != kNoDate) {
            ++cleared;
        }
        t.levelingDelay = 0; // its unit stays (MSPDI keeps the file's format)
        t.levelingFloor = kNoDate;
        t.preLeveledStart = kNoDate;
        t.preLeveledFinish = kNoDate;
    }
    schedule(project);
    return cleared;
}

QString formatLevelingDelay(const Task &task, const ProjectInfo &info, const QLocale &locale)
{
    if (task.levelingDelay <= 0) {
        return task.levelingDelayElapsed ? QStringLiteral("0 edays") : QStringLiteral("0 days");
    }
    if (!task.levelingDelayElapsed) {
        return formatSpan(task.levelingDelay, durationFormatOf(info), locale);
    }
    const double days = double(task.levelingDelay) / 1440.0;
    if (task.levelingDelay % 60 == 0 && task.levelingDelay % 1440 != 0 && task.levelingDelay < 1440) {
        const qint64 hours = task.levelingDelay / 60;
        return QStringLiteral("%1 %2").arg(locale.toString(hours), hours == 1 ? QStringLiteral("ehr") : QStringLiteral("ehrs"));
    }
    const QString number = locale.toString(days, 'f', task.levelingDelay % 1440 == 0 ? 0 : 2);
    return QStringLiteral("%1 %2").arg(number, days == 1.0 ? QStringLiteral("eday") : QStringLiteral("edays"));
}

bool parseLevelingDelay(const QString &text, const ProjectInfo &info, Minutes *minutes, bool *elapsed)
{
    const QString t = text.trimmed().toLower();
    if (t.isEmpty() || t == QLatin1String("0") || t == QLatin1String("na")) {
        *minutes = 0;
        *elapsed = true;
        return true;
    }
    static const QRegularExpression re(QStringLiteral("^(\\d+(?:[.,]\\d+)?)\\s*e\\s*(d|day|days|h|hr|hrs|hour|hours|w|wk|wks|week|weeks)$"));
    const QRegularExpressionMatch m = re.match(t);
    if (m.hasMatch()) {
        const double value = QString(m.captured(1)).replace(QLatin1Char(','), QLatin1Char('.')).toDouble();
        const QChar unit = m.captured(2).at(0);
        const double per = unit == QLatin1Char('h') ? 60.0 : unit == QLatin1Char('w') ? 7.0 * 1440.0 : 1440.0;
        *minutes = Minutes(std::llround(value * per));
        *elapsed = true;
        return true;
    }
    const std::optional<ParsedDuration> d = parseDuration(text, durationFormatOf(info));
    if (!d) {
        return false;
    }
    *minutes = d->minutes;
    *elapsed = false;
    return true;
}

} // namespace QindaOffice::Plan
