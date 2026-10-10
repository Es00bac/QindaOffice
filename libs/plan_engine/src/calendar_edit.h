// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Change Working Time, the parts M83 makes editable (D-092): Project's
// recurring calendar exceptions (yearly, monthly, weekly, every N days) and
// alternative work weeks. M79 read, showed and scheduled them; now they are
// made, changed and removed here too.
//
// AGENT-CONTRACT (D-088 kept): the Microsoft Project XML elements stay the
// truth — a calendar's recurring <Exception>s in its extra
// "mspdiExceptions" and its <WorkWeeks> kept element (mspdi_keep.h) — and
// Calendar::recurring / ::workWeeks are derived from them on every load.
// So an edit here writes the element in Project's own shape and derives the
// model again: a .qplan and an MSPDI export carry the edit with no new key,
// and an exception QindaPlan cannot expand (the nth weekday of a month)
// keeps travelling untouched. Indices are those of Calendar::recurring and
// Calendar::workWeeks.

#include "plan_edit.h"

namespace QindaOffice::Plan {

// The <Exception> element of a recurring exception (MSPDI Type 2–7); with
// `occurrences` > 0 it says "end after N occurrences" (and `to` is the
// last one).
[[nodiscard]] QString recurringExceptionXml(const RecurringException &exception, int occurrences = 0);

// index -1 adds; else replaces. `occurrences` > 0 ends it after that many
// (its last day is counted out). Refused for an unknown calendar or index,
// a pattern without a first day, weekly without days, or one that never
// falls inside its range.
EditResult setRecurringException(Project &project, int calendarUid, int index, RecurringException exception,
                                 int occurrences = 0);
EditResult removeRecurringException(Project &project, int calendarUid, int index);

// index -1 adds. A day of the week left unset keeps the calendar's own.
EditResult setWorkWeek(Project &project, int calendarUid, int index, const WorkWeek &week);
EditResult removeWorkWeek(Project &project, int calendarUid, int index);

} // namespace QindaOffice::Plan
