// SPDX-License-Identifier: GPL-3.0-or-later
// The printed Gantt's M83 marks (D-092): a split task's gaps — paper
// between its segments, joined by a dotted line, as Project draws them —
// and Project's progress line at the status date (tracking.h computes its
// points; gantt_print_p.h declares both painters).
#include "gantt_print_p.h"

#include "scheduler.h"
#include "task_segments.h"
#include "tracking.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace QindaOffice::Plan::GanttPrint {

bool anySplit(const Geometry &g)
{
    return std::any_of(g.rows.cbegin(), g.rows.cend(), [&g](int i) {
        const Task &t = g.project.tasks.at(i);
        return !t.summary && !t.splits.isEmpty();
    });
}

void paintSplitGaps(QPainter *painter, const Geometry &g, int h, int index, const QRectF &bar)
{
    const Task &t = g.project.tasks.at(index);
    if (t.splits.isEmpty() || t.summary) {
        return;
    }
    const WorkCalendar calendar(g.project, effectiveCalendarUid(g.project, index));
    const QList<TimeInterval> segments = taskSegments(t, calendar);
    for (int k = 1; k < segments.size(); ++k) {
        const qreal x0 = g.xOf(segments.at(k - 1).to, h);
        const qreal x1 = g.xOf(segments.at(k).from, h);
        if (x1 <= x0) {
            continue;
        }
        painter->fillRect(QRectF(x0, bar.top() - 0.4, x1 - x0, bar.height() + 0.8), g.style.paper);
        QPen pen(g.style.muted, 0.7);
        pen.setDashPattern({1.2, 1.6});
        painter->setPen(pen);
        painter->drawLine(QPointF(x0, bar.center().y()), QPointF(x1, bar.center().y()));
    }
}

void paintProgressLine(QPainter *painter, const Geometry &g, int h, int firstRow, int lastRow)
{
    const Minutes status = g.options.statusDate;
    if (!g.options.showProgressLine || status == kNoDate || lastRow < firstRow) {
        return;
    }
    const QList<int> rows = g.rows.mid(firstRow, lastRow - firstRow + 1);
    const QList<ProgressPoint> points = progressLine(g.project, rows, status);
    const qreal statusX = g.xOf(status, h);
    QPainterPath path(QPointF(statusX, g.rowsTop()));
    for (int r = 0; r < points.size(); ++r) {
        const qreal top = g.rowsTop() + r * g.rowHeight;
        if (points.at(r).bent) {
            path.lineTo(g.xOf(points.at(r).at, h), top + g.rowHeight / 2);
        }
        path.lineTo(statusX, top + g.rowHeight);
    }
    painter->setPen(QPen(g.style.warning, 1.0));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path);
    for (const ProgressPoint &p : points) {
        if (p.bent) {
            const int r = int(&p - points.constData());
            painter->setPen(Qt::NoPen);
            painter->setBrush(g.style.warning);
            painter->drawEllipse(QPointF(g.xOf(p.at, h), g.rowsTop() + r * g.rowHeight + g.rowHeight / 2), 1.6, 1.6);
        }
    }
}

} // namespace QindaOffice::Plan::GanttPrint
