// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The Network Diagram (M79, D-088): plan_engine's layout (network_layout.h)
// painted as Project draws it — a box per task (name; start and ID; finish
// and duration; resources), critical boxes and links red, summaries with an
// ink band, milestones as hexagons, a finished task crossed out and one in
// progress struck once — for the window (PlanNetworkItem), paper and PDF.
// One painter, so the screen and the printout cannot differ (D-051 rule 6).
//
// AGENT-CONTRACT (printing): the diagram is scaled to fit `pagesWide`
// pages across but never below kMinNetworkScale (55 %) — a wider diagram
// takes more pages — nor above 100 %; when it is too tall for one row of
// pages but would fit one at 55 % or more, it shrinks to fit. A page ends
// in the gap between two columns (rows) of boxes, so no box is cut. Rows of
// pages run down; pages are numbered down the first column of pages, then
// the next (as the Gantt's). Every page repeats the header; the legend
// prints on the last.

#include "network_layout.h"
#include "plan_page_output.h"

#include <QLocale>

namespace QindaOffice::Plan {

inline constexpr qreal kMinNetworkScale = 0.55;

struct NetworkPaintOptions {
    QLocale locale;
    bool showCritical = true;
    int selectedTaskUid = 0; // the window's current task: an accent frame
};

// Paints the whole layout in layout units (points) on the painter's current
// transform; the caller scales and translates.
void paintNetwork(QPainter *painter, const Project &project, const NetworkLayout &layout,
                  const NetworkPaintOptions &options);

struct NetworkPrintOptions {
    QList<int> rows; // the view's rows; empty = every task
    int pagesWide = 1;
    bool showCritical = true;
    Minutes statusDate = kNoDate;
    QLocale locale;
};

[[nodiscard]] PageLayout networkPages(const Project &project, const NetworkPrintOptions &options);
[[nodiscard]] int networkPageCount(const Project &project, const NetworkPrintOptions &options, const QSizeF &pagePoints);
[[nodiscard]] PagePainter networkPainter(const Project &project, const NetworkPrintOptions &options);

} // namespace QindaOffice::Plan
