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

// M79: the chart's style, as Project's Gantt views draw it.
//  "gantt"    task bars with a thin progress band, resource names after;
//  "tracking" Tracking Gantt — the done part solid, the rest light, the
//             chosen baseline beneath, % complete after the bar;
//  "leveling" Leveling Gantt — the pre-leveled bar beneath, the leveling
//             delay as a thin line before the bar and as text after it.
struct GanttPrintOptions {
    QDate from; // empty = project start, back to its Monday
    QDate to;   // empty = project finish, on to its Sunday
    int pagesWide = 1;
    QStringList columns{QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("duration"), QStringLiteral("start"),
                        QStringLiteral("finish")};
    QString style = QStringLiteral("gantt");
    bool showCritical = true;
    int baseline = 0; // 0–10; -1 = no baseline bars
    bool showBarText = true;
    bool showSlack = false;    // M79: total slack as a thin bar after the task (Detail Gantt)
    bool showDeadlines = true; // M79: a deadline arrow on the task's row
    Minutes today = kNoDate;
    Minutes statusDate = kNoDate;
    QList<int> hiddenTaskUids; // collapsed subtasks the window hides
    // M79: when set, exactly these task indices in this order (the view's
    // filter and sort); hiddenTaskUids is then not consulted.
    QList<int> rows;
    QLocale locale;
};

// The layout itself (for rasterPages() and previews).
[[nodiscard]] PageLayout ganttLayout(const Project &project, const GanttPrintOptions &options);
[[nodiscard]] int ganttPageCount(const Project &project, const GanttPrintOptions &options, const QSizeF &pagePoints);
[[nodiscard]] PagePainter ganttPainter(const Project &project, const GanttPrintOptions &options);

} // namespace QindaOffice::Plan
