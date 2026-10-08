// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The QindaPlan project model (M51, D-051): plain value types a Project is
// made of. Everything here is Qt Core; the scheduler (scheduler.h), the
// editing operations (plan_edit.h), the file format (plan_file.h) and the
// MSPDI bridge (mspdi.h) are free functions over these values.
//
// AGENT-CONTRACT (time, shared by every file of the engine and by the app):
//  * An INSTANT is `Minutes` since 1970-01-01T00:00 of the project's wall
//    clock. There are no time zones: a project date is what the calendar on
//    the wall says (Microsoft Project works the same way). The app hands
//    instants to Tk.GanttView as UTC milliseconds (minutes × 60000) with a
//    UTC GanttCalendar, so no zone conversion ever happens.
//  * A SPAN (duration, work, lag, slack) is WORKING minutes on a calendar;
//    "1 day" is ProjectInfo::minutesPerDay (480 by default).
//  * kNoDate marks an unset instant.
//
// AGENT-CONTRACT (money): integer minor units of ProjectInfo::currency
// (cents for USD). Rates are minor units per hour (work resources) or per
// unit (material resources). No floating point touches an amount.
//
// AGENT-NOTE (identity): Task::uid / Resource::uid / Assignment::uid /
// Calendar::uid are stable integers (they are MSPDI's UID). Task::guid is
// the UUID the toolkit's SelectionModel, GanttTaskModel and EditableGrid
// rows key on. A task's row number (ID) is its position + 1 and changes
// whenever tasks move — never persist it.
//
// Fields under "computed" are written by schedule() and never by editing;
// the file format does not store them (it recomputes on load).

#include <QDate>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUuid>

#include <array>
#include <limits>
#include <optional>

namespace QindaOffice::Plan {

using Minutes = qint64;
inline constexpr Minutes kNoDate = std::numeric_limits<qint64>::min();
inline constexpr int kBaselineCount = 3;

// MSPDI numbering order (Type 0..3 there is FF, FS, SF, SS — mspdi.cpp maps).
enum class LinkType { FinishToStart, StartToStart, FinishToFinish, StartToFinish };
// Same order as MSPDI ConstraintType 0..7.
enum class ConstraintType {
    AsSoonAsPossible,
    AsLateAsPossible,
    MustStartOn,
    MustFinishOn,
    StartNoEarlierThan,
    StartNoLaterThan,
    FinishNoEarlierThan,
    FinishNoLaterThan,
};
enum class DurationUnit { Minutes, Hours, Days, Weeks, Months };
enum class ResourceType { Work, Material };

// Working time within one day, minutes from midnight: 0 <= from < to <= 1440.
struct WorkingTime {
    int from = 0;
    int to = 0;
    friend bool operator==(const WorkingTime &, const WorkingTime &) = default;
};
using DayHours = QList<WorkingTime>;

// A date range that works differently from the week (a holiday: no times;
// a Saturday workday: its times).
struct CalendarException {
    QDate from;
    QDate to;
    QString name;
    DayHours times; // empty = non-working
    friend bool operator==(const CalendarException &, const CalendarException &) = default;
};

struct Calendar {
    int uid = 0;
    QString name;
    // A resource calendar derives from a base calendar (usually the
    // project's) and only overrides what differs.
    int baseUid = -1;
    // Index 0 = Monday … 6 = Sunday. nullopt = inherit from the base (a
    // base calendar without a value for a day treats it as non-working).
    std::array<std::optional<DayHours>, 7> week;
    QList<CalendarException> exceptions;
    QJsonObject extra; // unknown fields, written back unchanged
};

struct Link {
    int predecessorUid = 0;
    LinkType type = LinkType::FinishToStart;
    // Working minutes (negative = lead), unless lagPercent is set: then the
    // lag is that percentage (may be negative) of the predecessor's duration.
    Minutes lag = 0;
    std::optional<int> lagPercent;
    QJsonObject extra;
    friend bool operator==(const Link &a, const Link &b)
    {
        return a.predecessorUid == b.predecessorUid && a.type == b.type && a.lag == b.lag
            && a.lagPercent == b.lagPercent;
    }
};

struct BaselineData {
    bool set = false;
    Minutes start = kNoDate;
    Minutes finish = kNoDate;
    Minutes duration = 0;
    Minutes work = 0;
    qint64 cost = 0;
};

struct Task {
    int uid = 0;
    QUuid guid;
    QString name;
    int outlineLevel = 1; // 1 = top level; the order of Project::tasks is the outline order
    Minutes duration = 0;
    DurationUnit durationUnit = DurationUnit::Days; // display unit only
    bool estimated = false;
    bool milestone = false; // also implied by a zero duration
    bool manual = false;    // manually scheduled: dates are the user's
    bool active = true;
    Minutes manualStart = kNoDate;
    Minutes manualFinish = kNoDate;
    ConstraintType constraint = ConstraintType::AsSoonAsPossible;
    Minutes constraintDate = kNoDate;
    // M63: Project's Priority (0–1000, 500 = medium; leveling's tie-break
    // in Project, kept and shown here) and Deadline (a date the task should
    // finish by; it does not move the schedule, a finish after it is
    // flagged as in Project).
    int priority = 500;
    Minutes deadline = kNoDate;
    QList<Link> predecessors;
    int percentComplete = 0;
    Minutes actualStart = kNoDate;
    Minutes actualFinish = kNoDate;
    int calendarUid = -1; // -1 = the resource's or the project's
    qint64 fixedCost = 0;
    // Set by resource leveling (leveling.h): the task starts no earlier.
    Minutes levelingFloor = kNoDate;
    QString notes;
    std::array<BaselineData, kBaselineCount> baselines;
    QJsonObject extra;

    // ---- computed (schedule()) ----
    int id = 0; // row number, 1-based
    int parentIndex = -1;
    bool summary = false;
    bool effectiveActive = true; // false when this task or an ancestor is inactive
    QString wbs;
    Minutes start = kNoDate;
    Minutes finish = kNoDate;
    Minutes earlyStart = kNoDate;
    Minutes earlyFinish = kNoDate;
    Minutes lateStart = kNoDate;
    Minutes lateFinish = kNoDate;
    Minutes totalSlack = 0;
    Minutes freeSlack = 0;
    Minutes work = 0;
    qint64 cost = 0;
    bool critical = false;
    bool overallocated = false;
    bool constraintConflict = false;
    QString warning; // human-readable scheduling problem, empty when none
    // Indices into `predecessors` the scheduler ignored (missing task,
    // circular, onto its own summary …); views must not draw them.
    QList<int> ignoredLinks;

    [[nodiscard]] bool isMilestone() const { return milestone || (!summary && duration == 0); }
};

struct Resource {
    int uid = 0;
    QString name;
    QString initials;
    QString group;
    QString email;
    QString materialLabel; // "sheets", "boxes" — material resources only
    ResourceType type = ResourceType::Work;
    int maxUnits = 100;     // percent of one full-time person
    qint64 standardRate = 0; // per hour (work) or per unit (material)
    qint64 costPerUse = 0;
    int calendarUid = -1;   // -1 = the project calendar
    QJsonObject extra;

    // ---- computed ----
    Minutes work = 0; // work: minutes; material: units × 100
    qint64 cost = 0;
    bool overallocated = false;
};

struct Assignment {
    int uid = 0;
    int taskUid = 0;
    int resourceUid = 0;
    // Work resources: percent of the resource's time (100 = full-time).
    // Material resources: quantity × 100 (250 = 2.5 units).
    int units = 100;
    QJsonObject extra;

    // ---- computed ----
    Minutes work = 0;
    qint64 cost = 0;
};

struct ProjectInfo {
    QString title;
    QString manager;
    QString company;
    QString notes;
    Minutes start = kNoDate;
    Minutes statusDate = kNoDate;
    QString currency = QStringLiteral("USD");
    int calendarUid = 1;
    int minutesPerDay = 480;
    int minutesPerWeek = 2400;
    int daysPerMonth = 20;
    int defaultStartMinute = 8 * 60;
    int defaultFinishMinute = 17 * 60;
    QJsonObject extra;
};

// One scheduling problem the user should see (a cycle, a constraint the
// links cannot meet, a link to a missing task).
struct ScheduleIssue {
    int taskUid = 0;
    QString message;
};

struct Project {
    ProjectInfo info;
    QList<Calendar> calendars;
    QList<Task> tasks;
    QList<Resource> resources;
    QList<Assignment> assignments;
    QJsonObject view;  // window/view state the app persists (tables, zoom, …)
    QJsonObject extra; // unknown top-level fields

    // ---- computed ----
    Minutes finish = kNoDate;
    QList<ScheduleIssue> issues;

    // Lookups (linear; the scheduler builds its own hashes).
    [[nodiscard]] int taskIndex(int uid) const;
    [[nodiscard]] int taskIndexByGuid(const QUuid &guid) const;
    [[nodiscard]] int resourceIndex(int uid) const;
    [[nodiscard]] int resourceIndexByName(const QString &name) const;
    [[nodiscard]] int calendarIndex(int uid) const;
    [[nodiscard]] int assignmentIndex(int taskUid, int resourceUid) const;
    [[nodiscard]] QList<int> assignmentsOfTask(int taskUid) const;
    [[nodiscard]] int nextTaskUid() const;
    [[nodiscard]] int nextResourceUid() const;
    [[nodiscard]] int nextAssignmentUid() const;
    [[nodiscard]] int nextCalendarUid() const;
};

// "Standard": Monday–Friday 08:00–12:00 and 13:00–17:00, like Project's.
[[nodiscard]] Calendar standardCalendar(int uid = 1);
// An empty project starting on `start` (snapped to the default start time)
// with the standard calendar.
[[nodiscard]] Project newProject(const QDate &start, const QString &title = {});

[[nodiscard]] QString linkTypeCode(LinkType type); // "FS", "SS", "FF", "SF"
[[nodiscard]] std::optional<LinkType> linkTypeFromCode(const QString &code);
[[nodiscard]] QString constraintCode(ConstraintType type); // "ASAP", "SNET", …
[[nodiscard]] std::optional<ConstraintType> constraintFromCode(const QString &code);
[[nodiscard]] bool constraintNeedsDate(ConstraintType type);

} // namespace QindaOffice::Plan
