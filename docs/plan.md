<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# QindaPlan: `plan_engine`, `plan_output` and the app

Project scheduling for small teams — tasks in an outline, links, working
calendars, the critical path, resources and leveling, baselines, tracking,
reports and Microsoft Project XML (M51; decision
[D-051](decisions.md#d-051-qindaplan-a-deterministic-scheduling-engine-behind-a-project-shaped-window)).
M79 ([D-088](decisions.md#d-088-qindaplan-projects-scheduling-tracking-and-views))
brought Project's scheduling, tracking and views: deadlines and the slack
options in the critical path, Project's leveling delay and options,
Baseline … Baseline 10, the Tracking and Leveling Gantt, the Network
Diagram, Task Usage, filters and sorts; what is still missing is ranked in
[office-parity.md](plans/office-parity.md#qindaplan-against-microsoft-project-survey-of-2026-10-09-m79).
Microsoft Project is the reference for *scope and scheduling semantics*,
not for look or file format. Native `.mpp` files are out of scope: Project
saves Microsoft Project XML (File ▸ Save As ▸ XML), which QindaPlan reads
and writes.

| Target | Depends on | What it is |
| --- | --- | --- |
| `plan_engine` | Qt Core | The model, calendars, scheduler, leveling, baselines, reports, editing, `.qplan`, MSPDI, CSV; filters, sorts and the Network Diagram's layout (M79). No Gui. |
| `plan_output` | `plan_engine`, Qt Gui, `calc_io` (M63) | One page painter each for the printed Gantt (in the Gantt, Tracking and Leveling styles, M79), the Network Diagram (M79), the reports and the Timeline (PDF, paper, PNG); the task sheet as xlsx. |
| `apps/qindaplan` | `office_quick`, both libraries, QindaTK | `PlanHost`, `PlanDocument`, `PlanService`, the QML window. |

## Model (`plan_types.h`)

- **Instants** are minutes of the project's wall clock since 1970 — no
  time zones, as in Project. The app hands them to `Tk.GanttView` as UTC
  milliseconds with a UTC `GanttCalendar`, so nothing converts.
- **Spans** (duration, work, lag, slack) are *working* minutes; a day is
  `ProjectInfo::minutesPerDay` (480).
- **Money** is integer minor units of the project currency; rates are per
  hour (work) or per unit (material).
- **Identity**: `uid` (stable integers, MSPDI's UID) and a task `guid` (the
  row id of the Gantt, the sheet and the selection). A task's ID (row
  number) is display only.
- `Project::tasks` is in outline order; `outlineLevel` places each task and
  a task is a summary when the next one is deeper (`outline.h`).
- **Baselines** (M79): eleven per task and per assignment — Baseline,
  Baseline 1 … Baseline 10 — each start, finish, duration, work, cost.
- **Leveling** (M79): `Task::levelingDelay` is Project's Leveling Delay,
  elapsed minutes (or working minutes when `levelingDelayElapsed` is off)
  from the task's unleveled early start; `preLeveledStart/Finish` are the
  dates before the last leveling. The project carries the leveling order,
  "within slack only" and "clear first", and the critical-path options
  (`criticalSlackDays`, `multipleCriticalPaths`).

## Working time (`work_calendar.h`)

A calendar is a week (per weekday: none = inherit, empty = non-working, or
working intervals) and exceptions (date ranges with their own hours or
none); since M79 also Project's recurring exceptions and alternative work
weeks (read from Microsoft Project XML). Resolution for a date: the
calendar's own exception (dated, then recurring), its bases' exceptions (a
project holiday applies to every resource), a work week covering the date
(own, then the bases'), its own weekday, its bases' weekday. A calendar with no working time at all falls back to
the standard week (flagged) so no loop can run away; every day walk is
bounded (~100 years). `add(s, w)` ends a full day at 17:00 the same day,
not 08:00 the next.

The standard calendar is Monday–Friday 08:00–12:00 and 13:00–17:00.

## Scheduling (`scheduler.h`)

One deterministic pass per change:

1. Structure: IDs, WBS, summaries, inherited inactivity.
2. Forward pass in topological order over a graph where a summary is an
   **entry** (its links and Start No Earlier Than become a floor every
   child inherits) and an **exit** (the roll-up successors link from).
   FS/SS/FF/SF with lag, lead or a percentage of the predecessor's
   duration (lag is project working time); ASAP/ALAP, Must Start/Finish On,
   Start/Finish No Earlier/Later Than — hard constraints win over links and
   the conflict is reported on the task (Project's default); the leveling
   delay (M79: added to the unleveled early start, elapsed or working time;
   never on a Must Start/Finish On task); actual start/finish; manual tasks
   keep their dates.
3. Backward pass: late dates, total slack (working time between early and
   late finish), free slack, critical = active, unfinished, total slack ≤
   the critical slack limit (0 days unless set, M79). A **deadline** is a
   late finish (M79): it moves no date, but slack is measured to it, so a
   task — or a summary's subtasks — finishing after its deadline has
   negative slack and is critical, and so are its drivers. With **multiple
   critical paths** a task that drives nothing (no successor of its own or
   of a summary above it) has its early finish as its late finish.
4. ALAP tasks move by their free slack.
5. Roll-ups (summary dates, duration, % complete weighted by duration),
   work and cost, per-day overallocation.

A task runs on its own calendar, else its first work resource's, else the
project's. Links the scheduler must ignore — to a missing task, onto the
task's own summary or subtask, FF/SF onto a summary, or part of a cycle
from a file (every link inside a strongly connected component) — are
listed in `Project::issues`, on `Task::warning` and in `Task::ignoredLinks`
(views don't draw them). The editing operations refuse to create them
(`linkProblem`). A 2,000-task plan schedules in ~30 ms (`tst_plan_schedule`).

## Resources and leveling (`resources.h`, `leveling.h`)

Resources are work (people and equipment, by the hour), material (by the
unit) or — since M79, as in Project — **cost** (airfare, fees: no rate and
no units; each assignment carries the amount it costs, typed as
`Airfare[$450]` in Resource Names or in Assign Resources).

A work resource is **overallocated** on a day when the sum over its
assignments of (the task's working minutes that day × units) exceeds (its
calendar's working minutes that day × max units) — Project's "day by day"
basis. Material resources are never overallocated.

**Leveling** is a documented heuristic, not an optimiser. It only delays
auto-scheduled, not-started, not Must-Start/Finish-On leaf tasks whose
priority is below 1000 (Project's "Do Not Level"), by setting Project's
**Leveling Delay** (M79; until M78 an absolute floor, which a `.qplan` of
that time converts on load). It first clears the delays of the tasks it may
move (unless "clear leveling values before leveling" is off) and records
every task's pre-leveled start and finish. For each overallocated day of a
resource inside the leveling range: the task first in **leveling order**
keeps its dates (a task that cannot move always does) — *Standard*: least
total slack, then earlier start, higher priority, lower ID; *Priority,
Standard*: higher priority first; *ID Only*: lower ID — and another is
delayed until the kept one finishes (at least the next working day): first
one that can move that far **within its slack** (Standard: the most slack;
Priority, Standard: the lowest priority, then the most slack; ID Only: the
highest ID), and only when none can — and "level only within available
slack" is off — the same choice even though the finish moves. The delay is
the elapsed time from the task's unleveled early start to where it must
start, so Project's own scheduler gives the same dates from the saved
file. It repeats until the resource fits, a day cannot be fixed (recorded
and skipped) or 2,000 steps. **Level Selection** lets only the selected
tasks (and their subtasks) move; Clear Leveling removes every delay, or the
selection's; undo takes a leveling back in one step.

## Tracking (`baselines.h`, `plan_reports.h`)

Eleven baselines (M79: Baseline, Baseline 1 … Baseline 10; three before)
store start, finish, duration, work and cost per task and, for its
assignments, their work and cost; Set and Clear Baseline work on the whole
project or the selected tasks. Variances are positive when later, longer or
dearer, in project working time. The status date (or today) drives
**late** (the work that should be done by then exceeds % complete —
Project's "Late" status), Update Project (marks each task's share of
elapsed working time complete) and **Mark on Track** (the same for the
selected tasks, M79). A task has **slipped** when its finish is past its
baseline finish. Typing % complete records an actual start (and at 100% an
actual finish); **Update Tasks** (M79) types % complete, actual and
remaining duration and the actual dates for the selection — an actual
duration at or past the duration finishes the task, a remaining duration
makes the duration actual + remaining. % complete stays the stored truth
(an integer, as in Project's XML); the actual duration shown is the
roundest value that rounds to it (whole days, then half days, tenths of a
day, whole hours), else the exact share.

## Editing (`plan_edit.h`)

The sheets edit through field ids (`name`, `duration`, `start`,
`predecessors`, `resources`, …) as text in both directions, with Project's
semantics: a typed start on an auto task sets Start No Earlier Than, a
typed finish Finish No Earlier Than, a constraint date on an ASAP task
makes it SNET; `3, 4SS+2d, 5FF-1 day, 6FS+50%` for predecessors;
`Ana[50%], Card stock[200 sheets]` for resource names (unknown names become
resources). Durations accept `5d`, `3 days`, `4h`, `2w`, `1 mon`, `3?`
(estimated). Tables: Entry, Tracking (Act. Start, Act. Finish, % Comp.,
Act. Dur., Rem. Dur., Act. Cost, Act. Work — M79, as Project's), Variance,
Cost, Schedule (which also shows Deadline and Priority, M63), and (M79)
Work, Baseline and Delay (Leveling Delay typed as `2 edays`, `36 ehrs` or a
working `2d`). `priority` takes 0–1000 (500 = medium,
as in Project); `deadline` takes a date or `NA` — it never moves the
schedule, and a task that finishes after it shows the missed-deadline flag
(`missed-deadline` in the indicators text, a red flag in the sheet).

## Filters, sorts and the views they shape (`plan_views.h`, M79)

View ▸ Filter offers Project's built-ins: All Tasks, Critical, Incomplete,
Completed, Late, Slipping, Milestones, Summary Tasks, Tasks With
Deadlines, Using Resource… and Date Range… (tasks working on any day of a
range); with **Show Related Summary Rows** (on by default, as in Project)
each match keeps its summaries. View ▸ Sort orders by ID, start, finish,
priority (descending) or cost (descending), siblings under their summary
("keep outline structure"). Filters and sorts are view state (not undoable,
not saved). The rows they leave — without what a collapsed summary hides —
are the rows of the task sheet, the Gantt views, the Network Diagram, Task
Usage and the printout; the Calendar takes the filter.

## Network Diagram (`network_layout.h`, `network_print.h`, M79)

A box per task — name; start and ID; finish and duration; resources —
critical boxes and links red, summaries with an ink band, milestones as
hexagons, a finished task crossed out and one in progress struck once, the
link type written on SS/FF/SF links. Columns follow the longest chain of
links (a summary sits left of its subtasks); a box takes the mean row of
what leads to it, the first free one at or below; links leave a box's right
side and enter the next one's left side through the gaps between columns
and rows, so they never cross a box. The window paints it (paper, D-004)
with the same painter as print and PDF; a click selects a task, a double
click opens Task Information, View ▸ Zoom scales it. Printed, it is fitted
to the page setup's pages across, never below 55 % or above 100 %, and a
diagram a little too tall for one row of pages shrinks to fit it.

## File format (`plan_file.h`)

`.qplan` is UTF-8 JSON: `{"format": "qindaplan", "version": 1, "project",
"calendars", "tasks", "resources", "assignments", "view"}`, written with
`QSaveFile`. Only inputs are stored (computed fields are recalculated on
load). M79 added keys within version 1: a task's `levelingDelay`
(`levelingDelayWorking` when not elapsed), `preLeveledStart/Finish`,
baselines numbered to 10, an assignment's `baselines`, and the project's
`criticalSlackDays`, `multipleCriticalPaths`, `levelingOrder`,
`levelWithinSlack`, `levelClearFirst`. Older files load as before: a
leveling floor becomes the equivalent delay, and baselines 3–10 kept as
Project XML elements (M63–M78) join the model. Every object keeps keys it does not know and writes them back; a
file with a higher `version` is refused with a message; duplicate ids are
renumbered and dangling assignments dropped, each with a warning. The task
clipboard (`application/x-qindaplan-tasks`) is the same task JSON; pasting
remaps uids, keeps the links inside the copied block and finds or creates
resources by name. CSV export writes the current table (RFC 4180).

## Microsoft Project XML (`mspdi.h`)

Import and export of the MSPDI schema Project 2003–2021, Project Online,
ProjectLibre and MPXJ use: project properties, base and resource calendars
(week days, both exception forms — Project 2007's `<Exceptions>` is
written), tasks with outline, durations, milestones, manual and inactive
tasks, constraints, % complete and actuals, notes, fixed cost, links of
all four types with lag (LinkLag in tenths of a minute; LagFormat 19/20 is a
percentage), priority and deadline (M63), work and material resources and
assignments; since M79 cost resources (Type 2, with each assignment's
`Cost` as the amount entered), baselines 0–10 of tasks, assignments and
the project summary task,
`LevelingDelay`/`LevelingDelayFormat` (tenths of a minute; format 8 =
elapsed days, which leveling writes, 7 = working days; the file's own
format kept while it fits), `PreLeveledStart`/`PreLeveledFinish`,
`CriticalSlackLimit` and `MultipleCriticalPaths`. Costs are decimal cents on the wire (MPXJ's reading), rates per
hour, units decimals. The project summary task (UID 0) is skipped on import
and written on export. Whatever cannot be represented is reported as a
warning, never silently dropped.

**What QindaPlan does not model is kept** (M63, D-071; `mspdi_keep.h`).
Every child of Project, Calendar, Task, Resource and Assignment the reader
does not turn into the model — custom fields (`ExtendedAttribute`) and the
project's definitions of them (`ExtendedAttributes`, `OutlineCodes`), task
type, effort driven, contact, hyperlinks, cost rate tables, availability,
resource notes, work contours, the summary task's fields, views, tables,
filters, and elements of Project versions not yet written — is captured
verbatim into the object's `extra` (`"mspdi"`: name, the element before it,
the XML) and written back. Recurring calendar exceptions travel inside
`<Exceptions>` (`"mspdiExceptions"`); alternative work weeks as a calendar
child. Both are kept as written and, since M79, **scheduled**: every read
of Project XML and every `.qplan` load derives the calendar's recurring
exceptions (yearly or monthly on a day or a position, weekly on days, every
N days, within their range) and work weeks from those elements
(`mspdi_recurrence.cpp`); only a kind QindaPlan cannot expand (the nth
weekday or weekend day of a month, by weekday count) is reported as not
scheduled.
Because `extra` round-trips through `.qplan`, a Project file saved as
`.qplan` and back keeps all of it.

- **Project's order.** MSPDI children are ordered sequences. The writer
  emits each object's children — modelled and kept alike — in the order
  Microsoft Project 2019 writes them (`mspdi_order.cpp`: the lists come from
  Project's own files, with the schema's positions for elements those files
  do not use); an element the lists do not name goes right after the one it
  followed in the source. (This also fixed the export's own order: `Active`
  and `Manual` now follow `Name`, as Project writes them.)
- **The file's values, not ours.** Where QindaPlan used to write a constant —
  task `Type` (0), `FixedCostAccrual` (3), `CurrencySymbol`,
  `HonorConstraints`, `DefaultTaskType` and the other project defaults,
  `StandardRateFormat`, `IsBaselineCalendar` — the file's own value is kept
  and the constant written only when the file had none. Priority is no
  longer written back as 500.
- **Baselines go back as Project wrote them** (M79). Each Baseline
  element is read into the model and kept as written; while the model's
  baseline equals what it says, the element is replayed byte for byte
  (Project's Interim, EstimatedDuration, BCWS and BCWP included); a
  baseline set or cleared in QindaPlan is written from the model (or
  dropped). The project summary task's are replayed while no task's
  baseline of that number changed, and rolled up from the top-level tasks
  once one has.
- **Derived values follow the schedule.** Timephased data, actual and
  remaining work and duration, variances, earned-value fields, leveling
  delay and the like are what Project computed from the dates and progress
  QindaPlan edits. They are kept with a stamp of the object's UID, dates,
  duration, work and progress as read and written back only while the
  object still writes the same values; a rescheduled task (or a pasted copy,
  which has a new UID) drops them and Project recomputes. A resource's cost
  rate tables follow its rate alone.

## Output (`plan_output`)

`ganttPainter` prints the Gantt over a date range fitted to N pages wide:
rows paginate down, the timescale across; the sheet columns print on the
first horizontal page; bars, critical bars, summary brackets, milestones,
baselines, links with arrowheads, today and status lines and a legend. M79:
the view's rows (filter and sort); three styles — *Gantt* (a progress band,
resource names after the bar), *Tracking* (the done part solid, the rest
light, the chosen baseline beneath, % complete after the bar) and
*Leveling* (the pre-leveled bar beneath, the delay as a line before the bar
and as text after it); Detail Gantt slack bars (finish to late finish) when
Slack is on; deadline arrows (red once missed); a legend listing exactly
what the style draws. `networkPainter` prints the Network Diagram (above).
`reportPainter` prints a report (KPI boxes, tables with repeated headers).
`paintTimeline`/`exportTimelinePng` draw the Timeline strip (phases as bars
in lanes, milestone callouts above and below that never overlap — at a
crowded edge a label is shortened or dropped). One painter serves paper,
PDF and the window (D-004: paper colours live in `PlanPaperStyle`).
`writeTaskSheetXlsx` (M63) writes the table on screen as an Excel workbook
through calc_io: the fields' titles as a bold frozen header; costs as numbers
under the project currency's format; dates as date-times under
`yyyy-mm-dd hh:mm`; durations, slack and variances as numbers of working
days (`0.## d`), work as hours (`0.## h`); % complete as a fraction under
`0%`; ID and priority as whole numbers; names indented by outline level.
*File ▸ Import and Export ▸ Export Task Sheet as Excel…*
(`planExportXlsxAction`) exports the same columns as the CSV.

## QindaPlan (app)

- **One project per window** (rule 1). `PlanDocument : Core::Document`:
  `.qplan` native, `.xml` read and saved as MSPDI; every edit is
  `apply(label, operation)` on a copy that is scheduled and swapped in by a
  snapshot `QUndoCommand` (100 steps); dirty = the undo stack is not clean;
  the recovery journal stores `.qplan` JSON.
- **`PlanService`** (the host's `plan`) is the QML API: task/resource
  field edits, ribbon operations, assignments, leveling, calendars,
  baselines, status date, reports, exports, clipboard, find/replace — and
  the view models the toolkit controls draw: `ganttModel` (Tk.GanttTaskModel
  with a `PlanGanttAdapter` that previews a bar drag through the engine and
  commits it as Start No Earlier Than), `sheetModel`/`resourceSheetModel`
  (Tk.EditableGrid domain sources committing to the engine; the blank last
  row adds a task or resource), `usageModel` (Tk.ResourcePlanModel of hours
  per day or week, read-only), `calendarEvents` (Tk.CalendarGridView rows)
  and one `selection`.
- **Views** (radio group, Ctrl+1…6 for the first six): Gantt Chart (the
  Timeline strip over a split of the task sheet and the calendar-scale Gantt
  with synchronized rows, critical path, baselines, today and status lines),
  Task Sheet, Resource Sheet, Resource Usage (overallocation banner and red
  cells), Calendar (month/week of tasks), Reports (Project Overview,
  Critical Tasks, Resource Overview, Late Tasks, and — M79 — Cost Overview
  and Work Overview), and since M79 the **Tracking Gantt** (the Tracking
  table; the chosen baseline under every bar; % complete after it), the
  **Leveling Gantt** (the Delay table; pre-leveled bars; the delay after
  the bar), the **Network Diagram** and **Task Usage** (hours per task and
  per assignment, by day or week). Each sheet view keeps its own table, as
  in Project. QindaTK's GanttView draws no slack bars or deadline markers:
  with Slack on, the window writes the total slack after the bar; the
  printout draws both.
- **Ribbon**: File · Task · Resource · Report · Project · View and the
  contextual Gantt Chart Format tab (for the three Gantt views).
- **Printing**: Print, Print Preview and Export PDF render the Gantt
  (landscape; Gantt Page Setup sets the date range and pages wide) in the
  style of the Gantt view shown (M79: Tracking and Leveling too), the
  Network Diagram in that view, or, in the Reports view, the report on
  screen.
- **Commands** (`plan_command_table.h`, ids never change): views
  `planView{Gantt,TaskSheet,ResourceSheet,ResourceUsage,Calendar,Reports}Action`;
  task `planInsert{Task,Milestone,Summary}Action`, `planDeleteTaskAction`,
  `planIndentAction`, `planOutdentAction`, `planMove{Up,Down}Action`,
  `planLinkAction`, `planUnlinkAction`, `planMark{0,25,50,75,100}Action`,
  `planRespectLinksAction`, `planInactivateAction`,
  `planManualScheduleAction`, `planAutoScheduleAction`,
  `planTaskInfoAction`, `planTaskNotesAction`, `planScrollToTaskAction`,
  `planToggleSubtasksAction`, `planExpandAllAction`; resource
  `planAssignResourcesAction`, `planAdd{Resource,Material}Action`,
  `planDeleteResourceAction`, `planResourceInfoAction`,
  `planResourceCalendarAction`, `planLevelResourceAction`,
  `planLevelAllAction`, `planClearLevelingAction`,
  `planNextOverallocationAction`; project `planProjectInfoAction`,
  `planWorkingTimeAction`, `planSetBaselineAction`, `planClearBaselineAction`,
  `planStatusDateAction`, `planUpdateProjectAction`, `planRecalculateAction`;
  report `planReport{Overview,Critical,Resources,Late}Action`; files
  `planExport{Pdf,Csv,Xlsx,Mspdi,Timeline}Action` (Xlsx: M63), `planImportMspdiAction`,
  `planPageSetupAction`; view `planTable{Entry,Tracking,Variance,Cost,Schedule}Action`,
  `planTimescale{Days,Weeks,Months}Action`, `planGoToTodayAction`,
  `planShow{Timeline,Critical,Baseline,BarText}Action`, `planUsageWeeksAction`;
  M79: views `planView{TrackingGantt,LevelingGantt,Network,TaskUsage}Action`;
  tables `planTable{Work,Baseline,Delay}Action`; filters
  `planFilter{All,Critical,Incomplete,Completed,Late,Slipping,Milestones,Summary,Deadlines,Resource,DateRange}Action`,
  `planFilterRelatedSummariesAction`; sorts
  `planSort{Id,Start,Finish,Priority,Cost}Action`; the baseline drawn
  `planBaselineShown0Action` … `planBaselineShown10Action`;
  `planShowSlackAction`; leveling `planLevelingOptionsAction`,
  `planLevelSelectionAction`, `planClearLevelingSelectedAction`; tracking
  `planUpdateTasksAction`, `planMarkOnTrackAction`; reports
  `planReport{Cost,Work}Action`; resources `planAddCostResourceAction`.
