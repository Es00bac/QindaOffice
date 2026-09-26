// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The Gantt chart on paper (File ▸ Print / Print Preview / Export PDF in
// QindaPlan's Gantt view): the task sheet's columns on the left, a
// calendar-time chart on the right, fitted to a chosen number of pages
// across and paginated by rows.
//
// AGENT-CONTRACT (page order and scale, docs/plan.md "Printing"): the
// date range [from, to] (default: the project's start to finish, padded
// to whole Monday–Sunday weeks) spans EXACTLY `pagesWide` pages
// horizontally — the chart part of the first page plus every later page's
// full width. Rows paginate top to bottom; pages are numbered down the
// rows of the first horizontal page, then down the second, …. The sheet
// columns print on the first horizontal page only; every page repeats the
// header and the timescale; the legend prints on the last page.

#include "plan_page_output.h"
#include "plan_types.h"

#include <QDate>
#include <QList>
#include <QLocale>
#include <QStringList>

namespace QindaOffice::Plan {

struct GanttPrintOptions {
    QDate from; // empty = project start, back to its Monday
    QDate to;   // empty = project finish, on to its Sunday
    int pagesWide = 1;
    QStringList columns{QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("duration"), QStringLiteral("start"),
                        QStringLiteral("finish")};
    bool showCritical = true;
    int baseline = 0; // -1 = no baseline bars
    bool showBarText = true;
    Minutes today = kNoDate;
    Minutes statusDate = kNoDate;
    QList<int> hiddenTaskUids; // collapsed subtasks the window hides
    QLocale locale;
};

// The layout itself (for rasterPages() and previews).
[[nodiscard]] PageLayout ganttLayout(const Project &project, const GanttPrintOptions &options);
[[nodiscard]] int ganttPageCount(const Project &project, const GanttPrintOptions &options, const QSizeF &pagePoints);
[[nodiscard]] PagePainter ganttPainter(const Project &project, const GanttPrintOptions &options);

} // namespace QindaOffice::Plan
