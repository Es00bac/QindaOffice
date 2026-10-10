// SPDX-License-Identifier: GPL-3.0-or-later
// Split tasks in Microsoft Project XML (mspdi_timephased.h).
#include "mspdi_timephased.h"

#include "plan_time.h"
#include "scheduler.h"
#include "task_segments.h"
#include "task_types.h"
#include "work_calendar.h"

#include <QXmlStreamReader>

#include <algorithm>
#include <functional>

namespace QindaOffice::Plan::Mspdi {

namespace {

struct Period {
    Minutes from = kNoDate;
    Minutes to = kNoDate;
};

// The stretches with work (actual or remaining, Types 2 and 1), by start.
QList<Period> workPeriods(const QStringList &fragments)
{
    QList<Period> out;
    for (const QString &fragment : fragments) {
        QXmlStreamReader in(fragment);
        if (!in.readNextStartElement()) {
            continue;
        }
        int type = 0;
        Period p;
        Minutes value = 0;
        while (in.readNextStartElement()) {
            const QStringView name = in.name();
            const QString text = in.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
            if (name == u"Type") {
                type = text.toInt();
            } else if (name == u"Start") {
                p.from = parseWireDateTime(text);
            } else if (name == u"Finish") {
                p.to = parseWireDateTime(text);
            } else if (name == u"Value") {
                value = parseWireDuration(text).value_or(0);
            }
        }
        if ((type == 1 || type == 2) && value > 0 && p.from != kNoDate && p.to > p.from) {
            out.append(p);
        }
    }
    std::sort(out.begin(), out.end(), [](const Period &a, const Period &b) { return a.from < b.from; });
    return out;
}

void writePeriod(QXmlStreamWriter &xml, int uid, Minutes from, Minutes to, Minutes value)
{
    xml.writeStartElement(QStringLiteral("TimephasedData"));
    xml.writeTextElement(QStringLiteral("Type"), QStringLiteral("1"));
    xml.writeTextElement(QStringLiteral("UID"), QString::number(uid));
    xml.writeTextElement(QStringLiteral("Start"), wireDateTime(from));
    xml.writeTextElement(QStringLiteral("Finish"), wireDateTime(to));
    xml.writeTextElement(QStringLiteral("Unit"), QStringLiteral("2"));
    xml.writeTextElement(QStringLiteral("Value"), wireDuration(value));
    xml.writeEndElement();
}

// One period per interval with its share of `total` (cumulative rounding),
// and an empty one for each gap between them.
void writeIntervals(const std::function<void(Minutes, Minutes, Minutes)> &put, const Task &task,
                    const WorkCalendar &calendar, const QList<TimeInterval> &intervals, Minutes total)
{
    Minutes length = 0;
    for (const TimeInterval &iv : intervals) {
        length += taskMinutesBetween(task, calendar, iv.from, iv.to);
    }
    Minutes seen = 0;
    Minutes given = 0;
    for (int k = 0; k < intervals.size(); ++k) {
        const TimeInterval &iv = intervals.at(k);
        if (k > 0 && intervals.at(k - 1).to < iv.from) {
            put(intervals.at(k - 1).to, iv.from, 0);
        }
        seen += taskMinutesBetween(task, calendar, iv.from, iv.to);
        const Minutes upTo = length > 0 ? divRound(total * seen, length) : 0;
        put(iv.from, iv.to, upTo - given);
        given = upTo;
    }
}

bool hasWorkAssignment(const Project &project, int taskUid)
{
    for (const Assignment &a : project.assignments) {
        if (a.taskUid == taskUid && isWorkAssignment(project, a)) {
            return true;
        }
    }
    return false;
}

} // namespace

QStringList keptTimephased(const QJsonObject &extra)
{
    QStringList out;
    for (const Kept &k : keptFromJson(extra.value(kKeptKey))) {
        if (k.name == QLatin1String("TimephasedData")) {
            out.append(k.xml);
        }
    }
    return out;
}

QList<TaskSplit> splitsFromTimephased(const QStringList &fragments, const Task &task, const WorkCalendar &calendar)
{
    QList<TaskSplit> out;
    Minutes worked = 0;
    Minutes lastEnd = kNoDate;
    for (const Period &p : workPeriods(fragments)) {
        if (lastEnd != kNoDate && p.from > lastEnd) {
            const Minutes gap = taskMinutesBetween(task, calendar, lastEnd, p.from);
            if (gap > 0) {
                out.append({worked, gap});
            }
        }
        const Minutes from = lastEnd == kNoDate ? p.from : std::max(p.from, lastEnd);
        worked += taskMinutesBetween(task, calendar, from, p.to);
        lastEnd = lastEnd == kNoDate ? p.to : std::max(lastEnd, p.to);
    }
    return out;
}

void deriveSplits(Project &project, const QHash<int, QStringList> &placeholders)
{
    for (int i = 0; i < project.tasks.size(); ++i) {
        Task &t = project.tasks[i];
        if (t.summary || t.duration <= 0) {
            continue;
        }
        QStringList best;
        Minutes bestEnd = kNoDate;
        for (int ai : project.assignmentsOfTask(t.uid)) {
            const QStringList fragments = keptTimephased(project.assignments.at(ai).extra);
            const QList<Period> periods = workPeriods(fragments);
            if (!periods.isEmpty() && (bestEnd == kNoDate || periods.last().to > bestEnd)) {
                best = fragments;
                bestEnd = periods.last().to;
            }
        }
        if (best.isEmpty()) {
            best = placeholders.value(t.uid);
        }
        if (best.isEmpty()) {
            continue;
        }
        const WorkCalendar calendar(project, effectiveCalendarUid(project, i));
        QList<TaskSplit> splits = splitsFromTimephased(best, t, calendar);
        splits.removeIf([&](const TaskSplit &s) { return s.at <= 0 || s.at >= t.duration; });
        t.splits = splits;
    }
}

Minutes assignmentFinish(const Project &project, const Assignment &assignment)
{
    const int ti = project.taskIndex(assignment.taskUid);
    if (ti < 0) {
        return kNoDate;
    }
    const Task &t = project.tasks.at(ti);
    if (assignment.workSpan < 0 || t.summary || t.start == kNoDate) {
        return t.finish;
    }
    const WorkCalendar calendar(project, effectiveCalendarUid(project, ti));
    const QList<TimeInterval> window = workingWindow(t, calendar, assignmentSpan(t, assignment));
    return window.isEmpty() ? t.start : window.last().to;
}

void writeSplitTimephased(Children &c, QXmlStreamWriter &xml, const Project &project, const Assignment &assignment)
{
    const int ti = project.taskIndex(assignment.taskUid);
    if (ti < 0 || project.tasks.at(ti).summary || !isWorkAssignment(project, assignment)) {
        return;
    }
    const Task &t = project.tasks.at(ti);
    const WorkCalendar calendar(project, effectiveCalendarUid(project, ti));
    const Minutes span = assignmentSpan(t, assignment);
    // AGENT-GUARD: the stamp holds the assignment's dates and work, not
    // where the work stops: a split moved inside the same dates would
    // replay Project's periods for the old one. They go back only while
    // they split this assignment's work where the task now does.
    const QStringList kept = keptTimephased(assignment.extra);
    if (!kept.isEmpty()) {
        QList<TaskSplit> theirs = splitsFromTimephased(kept, t, calendar);
        theirs.removeIf([&](const TaskSplit &s) { return s.at <= 0 || s.at >= t.duration; });
        QList<TaskSplit> ours = t.splits;
        ours.removeIf([&](const TaskSplit &s) { return s.at <= 0 || s.at >= std::min(span, t.duration); });
        if (theirs != ours) {
            c.drop(QStringLiteral("TimephasedData"));
        }
    }
    if (t.splits.isEmpty() || c.replays(QStringLiteral("TimephasedData"))) {
        return;
    }
    const QList<TimeInterval> window = workingWindow(t, calendar, span);
    const int uid = assignment.uid;
    writeIntervals([&](Minutes from, Minutes to, Minutes value) {
        c.add(QStringLiteral("TimephasedData"), [&xml, uid, from, to, value] { writePeriod(xml, uid, from, to, value); });
    }, t, calendar, window, assignmentWork(t, assignment));
}

void writeSplitPlaceholders(QXmlStreamWriter &xml, const Project &project)
{
    int uid = project.nextAssignmentUid();
    for (int i = 0; i < project.tasks.size(); ++i) {
        const Task &t = project.tasks.at(i);
        if (t.summary || t.splits.isEmpty() || t.start == kNoDate || hasWorkAssignment(project, t.uid)) {
            continue;
        }
        // As Project writes a task without resources: a placeholder whose
        // timephased "work" is the task's working time.
        const WorkCalendar calendar(project, effectiveCalendarUid(project, i));
        const QList<TimeInterval> segments = taskSegments(t, calendar);
        xml.writeStartElement(QStringLiteral("Assignment"));
        xml.writeTextElement(QStringLiteral("UID"), QString::number(uid));
        xml.writeTextElement(QStringLiteral("TaskUID"), QString::number(t.uid));
        xml.writeTextElement(QStringLiteral("ResourceUID"), QString::number(kUnassignedResource));
        xml.writeTextElement(QStringLiteral("Finish"), wireDateTime(t.finish));
        xml.writeTextElement(QStringLiteral("Start"), wireDateTime(t.start));
        xml.writeTextElement(QStringLiteral("Units"), QStringLiteral("1"));
        xml.writeTextElement(QStringLiteral("Work"), wireDuration(0));
        const int placeholder = uid++;
        writeIntervals([&](Minutes from, Minutes to, Minutes value) { writePeriod(xml, placeholder, from, to, value); },
                       t, calendar, segments, t.duration);
        xml.writeEndElement();
    }
}

} // namespace QindaOffice::Plan::Mspdi
