// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Project's recurring tasks (M83, D-092): Insert ▸ Recurring Task makes a
// summary task flagged recurring (Task::recurring, MSPDI Recurring) whose
// subtasks are the occurrences — "Staff meeting 1", "Staff meeting 2", … —
// each Start No Earlier Than its date at the pattern's time, as Project
// makes them. The pattern (Task::recurrence) is kept in the .qplan so
// Recurring Task Information can change it; Microsoft Project XML does not
// carry a pattern, so a recurring task read from it keeps its occurrences
// and the flag only.
//
// AGENT-NOTE: an occurrence whose date is not a working day starts at the
// next working time (its Start No Earlier Than is the date; the scheduler
// moves it), so no occurrence is lost; Project asks the same question and
// offers that as one answer.

#include "plan_edit.h"

namespace QindaOffice::Plan {

// The occurrence dates of a pattern: daily (every N days, or every N
// working days of the project calendar), weekly on days, monthly or yearly
// by day or position (RecurringException's kinds), from `pattern.from`
// through `pattern.to`, or `count` of them; at most 999, as in Project.
[[nodiscard]] QList<QDate> recurrenceDates(const Project &project, const TaskRecurrence &recurrence);
// "Every 2 weeks on Monday, Wednesday from 2 Mar 2026 · 8 occurrences".
[[nodiscard]] QString recurrenceText(const TaskRecurrence &recurrence, const QLocale &locale = QLocale());
// Inserts the recurring task at row `at`; returns its index, or -1 with
// the reason in `error` (a pattern with no occurrence).
int insertRecurringTask(Project &project, int at, const QString &name, const TaskRecurrence &recurrence,
                        QString *error = nullptr);
// Recurring Task Information: a new pattern makes the occurrences again.
// Refused when an occurrence has progress or the task is not recurring.
EditResult setRecurrence(Project &project, int index, const TaskRecurrence &recurrence);

} // namespace QindaOffice::Plan
