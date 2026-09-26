// SPDX-License-Identifier: GPL-3.0-or-later
// MSPDI import, second half: the raw records the reader collected become a
// validated Project (mspdi_p.h), which is then scheduled.
#include "mspdi_p.h"

#include "outline.h"
#include "plan_time.h"
#include "scheduler.h"

#include <QDateTime>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <climits>

namespace QindaOffice::Plan::Mspdi {

namespace {

void assembleCalendars(Raw &raw, Project &p)
{
    QSet<int> seen;
    for (const Calendar &cal : std::as_const(raw.calendars)) {
        if (seen.contains(cal.uid)) {
            raw.warn(QStringLiteral("Calendar UID %1 appears twice; the second copy was skipped.").arg(cal.uid));
            continue;
        }
        seen.insert(cal.uid);
        p.calendars.append(cal);
    }
    for (Calendar &cal : p.calendars) {
        if (cal.baseUid >= 0 && (cal.baseUid == cal.uid || !seen.contains(cal.baseUid))) {
            raw.warn(QStringLiteral("Calendar “%1” names a base calendar that does not exist.").arg(cal.name));
            cal.baseUid = -1;
        }
    }
    if (p.calendars.isEmpty()) {
        p.calendars.append(standardCalendar(1));
        p.info.calendarUid = 1;
        return;
    }
    if (!raw.hasCalendarUid || p.calendarIndex(p.info.calendarUid) < 0) {
        p.info.calendarUid = p.calendars.first().uid;
        for (const Calendar &cal : std::as_const(p.calendars)) {
            if (cal.baseUid < 0) {
                p.info.calendarUid = cal.uid;
                break;
            }
        }
    }
}

void assembleTasks(Raw &raw, Project &p, int decimals, Minutes *earliest)
{
    // Tasks in ID order (the outline order); document order breaks ties.
    QList<RawTask> tasks;
    for (const RawTask &t : std::as_const(raw.tasks)) {
        if (t.isNull) {
            continue;
        }
        if (t.outlineLevel <= 0) {
            p.info.notes = t.task.notes; // the project summary task (UID 0)
            continue;
        }
        tasks.append(t);
    }
    std::stable_sort(tasks.begin(), tasks.end(), [](const RawTask &a, const RawTask &b) {
        return (a.id < 0 ? INT_MAX : a.id) < (b.id < 0 ? INT_MAX : b.id);
    });
    QSet<int> uids;
    QSet<QUuid> guids;
    QList<const RawTask *> sources; // parallel to p.tasks
    for (const RawTask &rt : std::as_const(tasks)) {
        Task t = rt.task;
        if (t.uid <= 0 || uids.contains(t.uid)) {
            raw.warn(QStringLiteral("Task “%1” has a missing or repeated UID and was skipped.").arg(t.name));
            continue;
        }
        uids.insert(t.uid);
        if (t.guid.isNull() || guids.contains(t.guid)) {
            t.guid = QUuid::createUuid();
        }
        guids.insert(t.guid);
        t.outlineLevel = std::clamp(rt.outlineLevel, 1, 64);
        const DurationCode code = durationCode(rt.durationFormat);
        t.durationUnit = code.percent || !code.known ? DurationUnit::Days : code.unit;
        t.estimated = t.estimated || code.estimated;
        if (code.elapsed) {
            raw.warn(QStringLiteral("Elapsed durations are read as working time (task “%1”).").arg(t.name));
        }
        t.duration = std::max<Minutes>(0, t.duration);
        if (rt.constraintCode >= 0 && rt.constraintCode <= 7) {
            t.constraint = ConstraintType(rt.constraintCode);
        } else {
            raw.warn(QStringLiteral("Task “%1” has an unknown constraint type; it is As Soon As Possible.").arg(t.name));
        }
        if (!constraintNeedsDate(t.constraint)) {
            t.constraintDate = kNoDate;
        } else if (t.constraintDate == kNoDate) {
            raw.warn(QStringLiteral("Task “%1” has a constraint without a date; it is As Soon As Possible.").arg(t.name));
            t.constraint = ConstraintType::AsSoonAsPossible;
        }
        if (t.manual) {
            t.manualStart = rt.manualStart != kNoDate ? rt.manualStart : rt.start;
            t.manualFinish = rt.manualFinish != kNoDate ? rt.manualFinish : rt.finish;
        }
        t.calendarUid = rt.calendarUid;
        if (t.calendarUid >= 0 && p.calendarIndex(t.calendarUid) < 0) {
            raw.warn(QStringLiteral("Task “%1” uses a calendar that does not exist.").arg(t.name));
            t.calendarUid = -1;
        }
        t.fixedCost = centsToMinor(rt.fixedCostCents, decimals);
        for (const RawBaseline &b : std::as_const(rt.baselines)) {
            if (b.number < 0 || b.number >= kBaselineCount) {
                raw.warn(QStringLiteral("Baselines 3–10 are not kept; baseline %1 was skipped.").arg(b.number));
                continue;
            }
            BaselineData data = b.data;
            data.cost = centsToMinor(b.costCents, decimals);
            t.baselines[size_t(b.number)] = data;
        }
        for (Minutes m : {rt.start, t.manualStart, t.actualStart}) {
            if (m != kNoDate && (*earliest == kNoDate || m < *earliest)) {
                *earliest = m;
            }
        }
        p.tasks.append(t);
        sources.append(&rt);
    }
    // Links, now that every UID is known.
    for (int i = 0; i < p.tasks.size(); ++i) {
        const RawTask *source = sources.at(i);
        Task &t = p.tasks[i];
        for (const RawLink &rl : source->links) {
            if (rl.crossProject) {
                raw.warn(QStringLiteral("Links to other projects are not supported (task “%1”).").arg(t.name));
                continue;
            }
            if (!uids.contains(rl.predecessorUid) || rl.predecessorUid == t.uid) {
                raw.warn(QStringLiteral("Task “%1” names a predecessor (UID %2) that does not exist; the link was dropped.")
                             .arg(t.name)
                             .arg(rl.predecessorUid));
                continue;
            }
            Link link;
            link.predecessorUid = rl.predecessorUid;
            const std::optional<LinkType> type = linkTypeFromWire(rl.type);
            if (!type) {
                raw.warn(QStringLiteral("Task “%1” has a link of unknown type; it is finish-to-start.").arg(t.name));
            }
            link.type = type.value_or(LinkType::FinishToStart);
            const DurationCode lag = durationCode(rl.lagFormat);
            if (lag.percent) {
                link.lagPercent = int(divRound(rl.lagTenths, 10));
            } else {
                link.lag = divRound(rl.lagTenths, 10);
                if (lag.elapsed && rl.lagTenths != 0) {
                    raw.warn(QStringLiteral("Elapsed lags are read as working time (task “%1”).").arg(t.name));
                }
            }
            bool duplicate = false;
            for (const Link &existing : std::as_const(t.predecessors)) {
                duplicate = duplicate || existing.predecessorUid == link.predecessorUid;
            }
            if (duplicate) {
                raw.warn(QStringLiteral("Task “%1” is linked twice to the same predecessor; the second link was dropped.").arg(t.name));
                continue;
            }
            t.predecessors.append(link);
        }
    }
    computeStructure(p);
    for (Task &t : p.tasks) {
        if (t.summary) {
            // Project writes a summary's roll-ups; ours are computed.
            t.actualStart = t.actualFinish = kNoDate;
        }
    }
}

void assembleResources(Raw &raw, Project &p, int decimals)
{
    QList<RawResource> kept;
    QSet<int> uids;
    for (const RawResource &rr : std::as_const(raw.resources)) {
        if (!rr.isNull && rr.resource.uid > 0) {
            uids.insert(rr.resource.uid);
        }
    }
    QHash<int, int> remap; // a named UID 0 resource gets a fresh UID
    QSet<int> seen;
    int next = 1;
    for (int uid : std::as_const(uids)) {
        next = std::max(next, uid + 1);
    }
    for (const RawResource &rr : std::as_const(raw.resources)) {
        Resource r = rr.resource;
        if (rr.isNull || r.name.trimmed().isEmpty()) {
            continue; // Project's blank UID 0 resource, or a blank row
        }
        if (r.uid == 0) {
            remap.insert(0, next);
            r.uid = next++;
        }
        if (r.uid < 0 || seen.contains(r.uid)) {
            raw.warn(QStringLiteral("Resource “%1” has a missing or repeated UID and was skipped.").arg(r.name));
            continue;
        }
        seen.insert(r.uid);
        if (rr.type == 2) {
            raw.warn(QStringLiteral("Cost resource “%1” was imported as a material resource.").arg(r.name));
        }
        r.type = rr.type == 1 ? ResourceType::Work : ResourceType::Material;
        r.standardRate = centsToMinor(rr.rateCents, decimals);
        r.costPerUse = centsToMinor(rr.costPerUseCents, decimals);
        if (r.calendarUid >= 0 && p.calendarIndex(r.calendarUid) < 0) {
            raw.warn(QStringLiteral("Resource “%1” uses a calendar that does not exist.").arg(r.name));
            r.calendarUid = -1;
        }
        p.resources.append(r);
    }
    QSet<int> assignmentUids;
    int nextAssignment = 1;
    for (const RawAssignment &ra : std::as_const(raw.assignments)) {
        nextAssignment = std::max(nextAssignment, ra.uid + 1);
    }
    for (const RawAssignment &ra : std::as_const(raw.assignments)) {
        if (ra.resourceUid == kUnassignedResource) {
            continue;
        }
        const int resourceUid = remap.value(ra.resourceUid, ra.resourceUid);
        const int ti = p.taskIndex(ra.taskUid);
        if (ti < 0 || p.resourceIndex(resourceUid) < 0) {
            raw.warn(QStringLiteral("An assignment (UID %1) names a task or resource that does not exist and was dropped.").arg(ra.uid));
            continue;
        }
        if (p.assignmentIndex(ra.taskUid, resourceUid) >= 0) {
            raw.warn(QStringLiteral("Task “%1” is assigned the same resource twice; the second assignment was dropped.")
                         .arg(p.tasks.at(ti).name));
            continue;
        }
        Assignment a;
        a.uid = ra.uid > 0 && !assignmentUids.contains(ra.uid) ? ra.uid : nextAssignment++;
        assignmentUids.insert(a.uid);
        a.taskUid = ra.taskUid;
        a.resourceUid = resourceUid;
        a.units = int(std::clamp<qint64>(ra.units, 0, 100000000));
        p.assignments.append(a);
    }
}

} // namespace

void assemble(Raw &raw, Project *out)
{
    Project p;
    p.info = raw.info;
    if (p.info.title.isEmpty()) {
        QString name = raw.name;
        for (const char *suffix : {".xml", ".mpp", ".XML", ".MPP"}) {
            if (name.endsWith(QLatin1String(suffix))) {
                name.chop(4);
            }
        }
        p.info.title = name;
    }
    const int decimals = currencyDecimals(p.info.currency);
    assembleCalendars(raw, p);
    Minutes earliest = kNoDate;
    assembleTasks(raw, p, decimals, &earliest);
    assembleResources(raw, p, decimals);
    if (p.info.start == kNoDate) {
        p.info.start = earliest != kNoDate
                           ? earliest
                           : toMinutes(QDate::currentDate(), p.info.defaultStartMinute);
    }
    schedule(p);
    *out = p;
}

} // namespace QindaOffice::Plan::Mspdi
