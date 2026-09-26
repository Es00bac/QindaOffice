<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# QindaPlan: `plan_engine`, `plan_output` and the app

Project scheduling for small teams — tasks in an outline, links, working
calendars, the critical path, resources and leveling, baselines, tracking,
reports and Microsoft Project XML (M51; decision
[D-051](decisions.md#d-051-qindaplan-a-deterministic-scheduling-engine-behind-a-project-shaped-window)).
Microsoft Project is the reference for *scope and scheduling semantics*,
not for look or file format. Native `.mpp` files are out of scope: Project
saves Microsoft Project XML (File ▸ Save As ▸ XML), which QindaPlan reads
and writes.

| Target | Depends on | What it is |
| --- | --- | --- |
| `plan_engine` | Qt Core | The model, calendars, scheduler, leveling, baselines, reports, editing, `.qplan`, MSPDI, CSV. No Gui. |
| `plan_output` | `plan_engine`, Qt Gui | One page painter each for the printed Gantt, the reports and the Timeline (PDF, paper, PNG). |
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

## Working time (`work_calendar.h`)

A calendar is a week (per weekday: none = inherit, empty = non-working, or
working intervals) and exceptions (date ranges with their own hours or
none). Resolution for a date: the calendar's own exception, its bases'
exceptions (a project holiday applies to every resource), its own weekday,
its bases' weekday. A calendar with no working time at all falls back to
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
   the conflict is reported on the task (Project's default); leveling
   delay; actual start/finish; manual tasks keep their dates.
3. Backward pass: late dates, total slack (working time between early and
   late finish), free slack, critical = active, unfinished, total slack ≤ 0.
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

A work resource is **overallocated** on a day when the sum over its
assignments of (the task's working minutes that day × units) exceeds (its
calendar's working minutes that day × max units) — Project's "day by day"
basis. Material resources are never overallocated.

**Leveling** is a documented heuristic, not an optimiser. It only delays
auto-scheduled, not-started, not Must-Start/Finish-On leaf tasks, via
`Task::levelingFloor`. For each overallocated day of a resource: keep the
task with the least total slack (ties: earlier start, lower ID) and delay
another until the kept one finishes (at least the next working day) —
first one that can move that far **within its slack** (the most slack
wins), and only when none can, the one with the most slack even though the
finish moves. It repeats until the resource fits, a day cannot be fixed
(recorded and skipped) or 2,000 steps. Clear Leveling removes every delay;
undo takes a leveling back in one step.

## Tracking (`baselines.h`, `plan_reports.h`)

Three baselines (Baseline, Baseline 1, Baseline 2) store start, finish,
duration, work and cost per task. Variances are positive when later,
longer or dearer, in project working time. The status date (or today)
drives **late** (the work that should be done by then exceeds % complete —
Project's "Late" status) and Update Project (marks each task's share of
elapsed working time complete). A task has **slipped** when its finish is
past its baseline finish. Typing % complete records an actual start (and at
100% an actual finish).

## Editing (`plan_edit.h`)

The sheets edit through field ids (`name`, `duration`, `start`,
`predecessors`, `resources`, …) as text in both directions, with Project's
semantics: a typed start on an auto task sets Start No Earlier Than, a
typed finish Finish No Earlier Than, a constraint date on an ASAP task
makes it SNET; `3, 4SS+2d, 5FF-1 day, 6FS+50%` for predecessors;
`Ana[50%], Card stock[200 sheets]` for resource names (unknown names become
resources). Durations accept `5d`, `3 days`, `4h`, `2w`, `1 mon`, `3?`
(estimated). Tables: Entry, Tracking, Variance, Cost, Schedule.

## File format (`plan_file.h`)

`.qplan` is UTF-8 JSON: `{"format": "qindaplan", "version": 1, "project",
"calendars", "tasks", "resources", "assignments", "view"}`, written with
`QSaveFile`. Only inputs are stored (computed fields are recalculated on
load). Every object keeps keys it does not know and writes them back; a
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
percentage), baselines 0–2, work and material resources (cost resources
import as material, with a warning) and assignments. Costs are decimal
cents on the wire (MPXJ's reading), rates per hour, units decimals. The
project summary task (UID 0) is skipped on import and written on export.
Whatever cannot be represented is reported as a warning, never silently
dropped.

## Output (`plan_output`)

`ganttPainter` prints the Gantt over a date range fitted to N pages wide:
rows paginate down, the timescale across; the sheet columns print on the
first horizontal page; bars, critical bars, summary brackets, milestones,
baselines, links with arrowheads, today and status lines and a legend.
`reportPainter` prints a report (KPI boxes, tables with repeated headers).
`paintTimeline`/`exportTimelinePng` draw the Timeline strip (phases as bars
in lanes, milestone callouts above and below that never overlap — at a
crowded edge a label is shortened or dropped). One painter serves paper,
PDF and the window (D-004: paper colours live in `PlanPaperStyle`).

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
- **Views** (radio group, Ctrl+1…6): Gantt Chart (the Timeline strip over a
  split of the task sheet and the calendar-scale Gantt with synchronized
  rows, critical path, baselines, today and status lines), Task Sheet,
  Resource Sheet, Resource Usage (overallocation banner and red cells),
  Calendar (month/week of tasks) and Reports (Project Overview, Critical
  Tasks, Resource Overview, Late Tasks). The network diagram is not built:
  a readable automatic layout of a real plan is a project of its own, and
  the Gantt's links carry the same information.
- **Ribbon**: File · Task · Resource · Report · Project · View and the
  contextual Gantt Chart Format tab.
- **Printing**: Print, Print Preview and Export PDF render the Gantt
  (landscape; Gantt Page Setup sets the date range and pages wide) or, in
  the Reports view, the report on screen.
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
  `planExport{Pdf,Csv,Mspdi,Timeline}Action`, `planImportMspdiAction`,
  `planPageSetupAction`; view `planTable{Entry,Tracking,Variance,Cost,Schedule}Action`,
  `planTimescale{Days,Weeks,Months}Action`, `planGoToTodayAction`,
  `planShow{Timeline,Critical,Baseline,BarText}Action`, `planUsageWeeksAction`.
