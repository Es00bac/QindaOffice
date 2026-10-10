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
#include <QMap>
#include <QString>
#include <QUuid>

#include <array>
#include <limits>
#include <optional>

namespace QindaOffice::Plan {

using Minutes = qint64;
inline constexpr Minutes kNoDate = std::numeric_limits<qint64>::min();
// Baseline, Baseline 1 … Baseline 10, as in Project (M79; three before).
inline constexpr int kBaselineCount = 11;

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
// M79: Cost resources are Project's (MSPDI Type 2): no rate, no units; each
// assignment carries the amount it costs (Assignment::costInput).
enum class ResourceType { Work, Material, Cost };
// Project's leveling order (Resource Leveling dialog, M79): which task keeps
// its dates on an overbooked day (leveling.h).
enum class LevelingOrder { IdOnly, Standard, PriorityStandard };
// M83: Project's task types, in MSPDI Type order 0..2 — which of duration,
// units and work stays put when another of them changes (task_types.h).
enum class TaskType { FixedUnits, FixedDuration, FixedWork };
// M83: an assignment whose work no task-type rule has set yet works its
// units for the task's whole duration (every assignment before M83).
inline constexpr Minutes kWorkFollowsDuration = -1;

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

// M79: an exception that recurs within its range — Project's yearly,
// monthly, weekly and every-N-days exceptions (MSPDI Exception Type 2–7).
// Read from Microsoft Project XML, scheduled by WorkCalendar, and written
// back as the file had it (mspdi_recurrence.cpp).
struct RecurringException {
    enum class Kind { EveryDays, Weekly, MonthlyByDay, MonthlyByPosition, YearlyByDay, YearlyByPosition };
    Kind kind = Kind::Weekly;
    QString name;
    QDate from;
    QDate to;            // the last day it can fall on
    int every = 1;       // every N days, weeks or months (MSPDI Period)
    int daysOfWeek = 0;  // Weekly: bit 0 = Monday … bit 6 = Sunday
    int day = 1;         // ByDay: day of the month (past its end: its last day)
    int position = 0;    // ByPosition: 0 first … 3 fourth, 4 last
    int weekday = 1;     // ByPosition: 1 Monday … 7 Sunday
    int month = 1;       // Yearly: 1 January … 12 December
    DayHours times;      // empty = non-working
    [[nodiscard]] bool fallsOn(const QDate &date) const; // work_calendar.cpp
    friend bool operator==(const RecurringException &, const RecurringException &) = default;
};

// M79: an alternative work week (MSPDI WorkWeeks): inside its range its
// days replace the calendar's week; a day it leaves out keeps the week's.
struct WorkWeek {
    QString name;
    QDate from;
    QDate to;
    std::array<std::optional<DayHours>, 7> week; // 0 = Monday
    friend bool operator==(const WorkWeek &, const WorkWeek &) = default;
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
    // M79: derived from the Microsoft Project XML the calendar came from —
    // its kept Exception and WorkWeeks elements stay the truth (extra) and
    // these are rebuilt from them on every load (mspdi_recurrence.cpp).
    QList<RecurringException> recurring;
    QList<WorkWeek> workWeeks;
    QJsonObject extra; // unknown fields, written back unchanged
};

struct Link {
    int predecessorUid = 0;
    LinkType type = LinkType::FinishToStart;
    // Working minutes (negative = lead), unless lagPercent is set: then the
    // lag is that percentage (may be negative) of the predecessor's duration.
    Minutes lag = 0;
    std::optional<int> lagPercent;
    // M83: an elapsed lag ("2ed", MSPDI LagFormat 4–12 even) runs on the
    // wall clock, around nights and weekends.
    bool lagElapsed = false;
    QJsonObject extra;
    friend bool operator==(const Link &a, const Link &b)
    {
        return a.predecessorUid == b.predecessorUid && a.type == b.type && a.lag == b.lag
            && a.lagPercent == b.lagPercent && a.lagElapsed == b.lagElapsed;
    }
};

// M83: a split in a task (Project's Split Task): after `at` minutes of the
// task's duration the work stops for `gap` minutes, then resumes. Both are
// working minutes on the task's calendar (wall-clock minutes for an elapsed
// duration), so a split moves with its task.
struct TaskSplit {
    Minutes at = 0;
    Minutes gap = 0;
    friend bool operator==(const TaskSplit &, const TaskSplit &) = default;
};

// M83: Project's recurring task (Insert ▸ Recurring Task): a summary whose
// subtasks are the occurrences. The pattern is a RecurringException's
// (daily every N days, weekly on days, monthly or yearly by day or
// position) within [from, to], or `count` occurrences from `from`.
struct TaskRecurrence {
    RecurringException pattern; // kind, every, daysOfWeek, day, position, weekday, month, from, to
    bool workdays = false;      // daily: every N working days of the project calendar
    int count = 0;              // > 0: end after this many occurrences (else at pattern.to)
    Minutes duration = 480;     // each occurrence's duration
    int startMinute = 8 * 60;   // each occurrence starts at this time of day
    friend bool operator==(const TaskRecurrence &, const TaskRecurrence &) = default;
};

// M83: a custom field's definition (Project ▸ Custom Fields; MSPDI
// ExtendedAttributes): the field it renames (Text1 … Text30, Number1 … 20,
// Date1 … 10, Flag1 … 20, Cost1 … 10, Duration1 … 10, Start1 … 10,
// Finish1 … 10), its alias, a lookup table and a formula (custom_fields.h).
struct CustomLookupEntry {
    QString value; // in the stored spelling of a value (custom_fields.h)
    QString description;
    friend bool operator==(const CustomLookupEntry &, const CustomLookupEntry &) = default;
};
struct CustomFieldDef {
    QString field; // "Text1"
    QString alias; // "Department"; empty = not renamed
    QList<CustomLookupEntry> lookup;
    bool restrictToList = false; // only lookup values may be typed
    QString formula;             // Project's formula; empty = typed values
    QJsonObject extra;           // the definition's MSPDI parts QindaPlan does not model
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
    // M79: Project's Leveling Delay — how long leveling holds the task back
    // from its early start (leveling.h sets it; it can be typed; MSPDI
    // LevelingDelay). Wall-clock minutes when levelingDelayElapsed (Project's
    // default "edays"), else working minutes on the task's calendar. It is
    // RELATIVE: a task whose predecessors move keeps its delay, as in Project.
    Minutes levelingDelay = 0;
    bool levelingDelayElapsed = true;
    // M79: the start and finish before the last leveling (Project's
    // Pre-Leveled Start/Finish; the Leveling Gantt draws them).
    Minutes preLeveledStart = kNoDate;
    Minutes preLeveledFinish = kNoDate;
    // AGENT-NOTE (legacy, M51–M78): leveling used to store an absolute
    // "starts no earlier" floor. readPlanJson converts a floor from an old
    // .qplan into levelingDelay (plan_file.cpp); schedule() still honours a
    // floor only so that conversion is exact. Nothing else sets it.
    Minutes levelingFloor = kNoDate;
    QString notes;
    std::array<BaselineData, kBaselineCount> baselines;
    // ---- M83 (D-092) ----
    // Project's task type and effort-driven flag (task_types.h): which of
    // duration, units and work a change recalculates; whether added or
    // removed resources share the task's work. Fixed Work is always effort
    // driven, as in Project.
    TaskType type = TaskType::FixedUnits;
    bool effortDriven = false;
    // Work typed on a task without work assignments (Project keeps it; the
    // first resources assigned take it over).
    Minutes unassignedWork = 0;
    // Duration in wall-clock minutes ("3ed", MSPDI DurationFormat 4–12 even):
    // the task runs around the clock, ignoring its calendar.
    bool durationElapsed = false;
    QList<TaskSplit> splits; // in order of `at`; the duration excludes the gaps
    // A recurring summary task: the pattern its occurrences (its subtasks)
    // were made from; `recurring` without a pattern = Project's flag read
    // from Microsoft Project XML (which does not carry the pattern).
    bool recurring = false;
    std::optional<TaskRecurrence> recurrence;
    // Custom field values (Text1 … Finish10) in Microsoft Project XML's own
    // spelling — text, decimal numbers, "1"/"0", dates "yyyy-MM-ddTHH:mm:ss",
    // cost in cents, durations "PT8H0M0S" (+ "|<DurationFormat>") —
    // custom_fields.h converts. A formula field stores nothing here.
    QMap<QString, QString> custom;
    // Actual Cost typed while ProjectInfo::editableActualCosts is on (minor
    // units); -1 = Project calculates it from % complete.
    qint64 actualCostInput = -1;
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
    // M79: the early start before the leveling delay (leveling measures the
    // delay it needs from here; Leveling Delay = start − this, elapsed).
    Minutes unleveledStart = kNoDate;
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
    // Material resources: quantity × 100 (250 = 2.5 units). Cost
    // resources: 100 (Project writes 1).
    int units = 100;
    // M79: what the assignment of a cost resource costs (minor units; Project
    // types it on the assignment). Unused for work and material.
    qint64 costInput = 0;
    // M83: a work resource's work as Project stores it (MSPDI Work), set by
    // the task-type rules (task_types.h) — or kWorkFollowsDuration: the
    // assignment works its units for the task's whole duration.
    Minutes workInput = kWorkFollowsDuration;
    // M83: the working minutes of the task it works, from the task's start
    // (-1 = the whole duration): shorter only when its units or work were
    // typed apart from the others' on a Fixed Units or Fixed Work task.
    Minutes workSpan = -1;
    // M79: what Set Baseline saved for the assignment (Project keeps the
    // assignment's baseline work and cost next to the task's).
    std::array<BaselineData, kBaselineCount> baselines;
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
    // M79 (MSPDI CriticalSlackLimit / MultipleCriticalPaths): a task is
    // critical when its total slack is at most this many days; with multiple
    // critical paths every task without successors ends a critical path.
    int criticalSlackDays = 0;
    bool multipleCriticalPaths = false;
    // M79: Project's Resource Leveling options (.qplan only; MSPDI has no
    // place for them). Leveling range and Level Selection are per run.
    LevelingOrder levelingOrder = LevelingOrder::Standard;
    bool levelWithinSlack = false;
    bool levelClearFirst = true;
    // M83 (MSPDI DefaultTaskType / NewTasksEffortDriven / EditableActualCosts):
    // what a new task is, and whether Actual Cost can be typed (Project's
    // "Actual costs are always calculated by Project" turned off).
    TaskType defaultTaskType = TaskType::FixedUnits;
    bool newTasksEffortDriven = false;
    bool editableActualCosts = false;
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
    // M83: the task custom fields renamed, given a lookup table or a formula
    // (one entry per field; a field without one is plain "Text1").
    QList<CustomFieldDef> customFields;
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
