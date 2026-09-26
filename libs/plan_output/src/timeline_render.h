// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The Timeline strip (Microsoft Project's Timeline view): the project from
// start to finish as one band, top-level summary tasks as labelled bars
// inside it, milestones as diamonds with name/date callouts above and
// below, month ticks and a today marker. One painter serves the window's
// strip (painted into an item), File ▸ Export ▸ Timeline as PNG and any
// print use, so they cannot differ.
//
// AGENT-NOTE (callout placement): milestones are placed left to right in
// lanes tried in the order above-1, below-1, above-2, below-2, …; a lane
// takes a label when its last label ends at least a gap before this one
// starts. When no lane is free the label goes into the lane that frees up
// first, pushed right with an elbow leader — labels never overlap. Lane
// count follows the strip's height, so a short strip stacks less.

#include "plan_types.h"

#include <QImage>
#include <QLocale>
#include <QRectF>

class QPainter;

namespace QindaOffice::Plan {

struct TimelineOptions {
    Minutes today = kNoDate;
    QLocale locale;
    bool paper = true; // fill the rectangle with paper white first
};

void paintTimeline(QPainter *painter, const QRectF &rect, const Project &project, const TimelineOptions &options);
[[nodiscard]] QImage renderTimeline(const Project &project, const QSize &size, const TimelineOptions &options,
                                    qreal devicePixelRatio = 1.0);
bool exportTimelinePng(const Project &project, const QString &path, const QSize &size, const TimelineOptions &options,
                       QString *error = nullptr);

} // namespace QindaOffice::Plan
