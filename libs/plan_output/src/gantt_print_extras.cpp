// SPDX-License-Identifier: GPL-3.0-or-later
// The Gantt printout around the bars (M79, D-088; gantt_print_p.h): the
// lower lane each style uses, Detail Gantt slack bars, deadline arrows, the
// Leveling Gantt's delay line, and a legend of exactly what is drawn.
#include "gantt_print_p.h"
#include "baselines.h"
#include "plan_time.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

#include <functional>

namespace QindaOffice::Plan::GanttPrint {

bool lowerBar(const Geometry &g, const Task &t, Minutes *start, Minutes *finish)
{
    if (g.options.style == QLatin1String("leveling")) {
        // Project's Leveling Gantt: the pre-leveled bar, for leaf tasks.
        if (t.summary || t.preLeveledStart == kNoDate || t.preLeveledFinish == kNoDate) {
            return false;
        }
        *start = t.preLeveledStart;
        *finish = t.preLeveledFinish;
        return true;
    }
    const int b = g.options.baseline;
    if (b < 0 || b >= kBaselineCount || !t.baselines[size_t(b)].set) {
        return false;
    }
    const BaselineData &bl = t.baselines[size_t(b)];
    if (bl.start == kNoDate || bl.finish == kNoDate) {
        return false;
    }
    *start = bl.start;
    *finish = bl.finish;
    return true;
}

void paintBarExtras(QPainter *painter, const Geometry &g, int h, int firstRow, int lastRow)
{
    const PlanPaperStyle &s = g.style;
    const bool leveling = g.options.style == QLatin1String("leveling");
    for (int r = firstRow; r <= lastRow; ++r) {
        const Task &t = g.project.tasks.at(g.rows.at(r));
        if (t.start == kNoDate || !t.effectiveActive) {
            continue;
        }
        const qreal y = g.rowsTop() + (r - firstRow) * g.rowHeight;
        const qreal cy = y + g.rowHeight / 2;
        // Detail Gantt: total slack runs from the finish to the late finish.
        if (g.options.showSlack && !t.summary && t.totalSlack > 0 && t.lateFinish > t.finish) {
            const qreal a = g.xOf(t.finish, h);
            const qreal b = g.xOf(t.lateFinish, h);
            painter->setPen(QPen(s.good, 1.1));
            painter->drawLine(QPointF(a, cy + 1), QPointF(b, cy + 1));
            painter->drawLine(QPointF(b, cy - 1.5), QPointF(b, cy + 3.5));
        }
        // Leveling Gantt: the delay as a thin line before the bar.
        if (leveling && !t.summary && t.levelingDelay > 0 && t.unleveledStart != kNoDate && t.unleveledStart < t.start) {
            painter->setPen(QPen(s.warning, 1.1));
            painter->drawLine(QPointF(g.xOf(t.unleveledStart, h), cy - 1), QPointF(g.xOf(t.start, h), cy - 1));
        }
        // A deadline: Project's arrow, red once the finish is past it.
        if (g.options.showDeadlines && t.deadline != kNoDate) {
            const qreal x = g.xOf(t.deadline, h);
            const QColor c = t.finish > t.deadline && t.percentComplete < 100 ? s.critical : s.good;
            painter->setPen(QPen(c, 0.8));
            painter->drawLine(QPointF(x, y + 0.8), QPointF(x, y + 4));
            painter->setPen(Qt::NoPen);
            painter->setBrush(c);
            painter->drawPolygon(QPolygonF({QPointF(x - 2.6, y + 3.6), QPointF(x + 2.6, y + 3.6), QPointF(x, y + 7.2)}));
        }
    }
}

void paintLegend(QPainter *painter, const Geometry &g)
{
    const PlanPaperStyle &s = g.style;
    const GanttPrintOptions &o = g.options;
    const bool tracking = o.style == QLatin1String("tracking");
    const bool leveling = o.style == QLatin1String("leveling");
    const qreal cy = g.area.bottom() - g.legendHeight + 13;
    // Each entry: its swatch (drawn at x, 18pt wide) and its label.
    struct Entry {
        std::function<void(qreal x)> swatch;
        QString label;
    };
    QList<Entry> entries;
    const auto bar = [&](const QColor &c, qreal done, bool solid) {
        return [&s, c, done, solid, painter, cy](qreal x) {
            painter->setPen(QPen(c, 0.6));
            painter->setBrush(s.tint(c, solid ? 0.30 : 0.42));
            painter->drawRect(QRectF(x, cy - 3.5, 18, 7));
            if (done > 0) {
                painter->setPen(Qt::NoPen);
                painter->setBrush(c.darker(solid ? 125 : 115));
                painter->drawRect(solid ? QRectF(x, cy - 3.5, 18 * done, 7) : QRectF(x, cy - 1.6, 18 * done, 3.2));
            }
        };
    };
    const auto line = [&](const QColor &c, qreal width) {
        return [c, width, painter, cy](qreal x) {
            painter->setPen(QPen(c, width));
            painter->drawLine(QPointF(x, cy), QPointF(x + 18, cy));
        };
    };
    entries.append({bar(s.accent, 0, tracking), QStringLiteral("Task")});
    if (o.showCritical) {
        entries.append({bar(s.critical, 0, tracking), QStringLiteral("Critical task")});
    }
    entries.append({bar(s.accent, 0.6, tracking), tracking ? QStringLiteral("Complete") : QStringLiteral("Progress")});
    if (leveling) {
        entries.append({[&s, painter, cy](qreal x) { painter->fillRect(QRectF(x, cy - 1.3, 18, 2.6), s.tint(s.warning, 0.55)); },
                        QStringLiteral("Preleveled task")});
        entries.append({line(s.warning, 1.1), QStringLiteral("Leveling delay")});
    } else if (o.baseline >= 0) {
        entries.append({[&s, painter, cy](qreal x) { painter->fillRect(QRectF(x, cy - 1.3, 18, 2.6), s.baseline); },
                        baselineName(o.baseline)});
    }
    if (o.showSlack) {
        entries.append({line(s.good, 1.1), QStringLiteral("Slack")});
    }
    entries.append({[&s, painter, cy](qreal x) {
                        painter->setPen(Qt::NoPen);
                        painter->setBrush(s.ink);
                        painter->drawRect(QRectF(x, cy - 2, 18, 3.4));
                    },
                    QStringLiteral("Summary")});
    entries.append({[&s, painter, cy](qreal x) {
                        painter->setPen(Qt::NoPen);
                        painter->setBrush(s.ink);
                        painter->drawPolygon(diamond(x + 9, cy, kMilestoneHalf));
                    },
                    QStringLiteral("Milestone")});
    if (o.showDeadlines) {
        entries.append({[&s, painter, cy](qreal x) {
                            painter->setPen(Qt::NoPen);
                            painter->setBrush(s.good);
                            painter->drawPolygon(QPolygonF({QPointF(x + 6.4, cy - 3), QPointF(x + 11.6, cy - 3), QPointF(x + 9, cy + 1)}));
                        },
                        QStringLiteral("Deadline")});
    }
    if (o.today != kNoDate) {
        entries.append({[&s, painter, cy](qreal x) { dashedLine(painter, x + 9, cy - 5, cy + 5, s.accent); }, QStringLiteral("Today")});
    }
    if (o.statusDate != kNoDate) {
        entries.append({[&s, painter, cy](qreal x) { dashedLine(painter, x + 9, cy - 5, cy + 5, s.critical); },
                        QStringLiteral("Status date")});
    }
    entries.append({[&s, painter, cy](qreal x) {
                        painter->setPen(QPen(s.muted, 0.6));
                        painter->setBrush(Qt::NoBrush);
                        QPainterPath link(QPointF(x, cy - 3));
                        link.lineTo(x + 6, cy - 3);
                        link.lineTo(x + 6, cy + 3);
                        link.lineTo(x + 14, cy + 3);
                        painter->drawPath(link);
                        arrowHead(painter, QPointF(x + 16, cy + 3), true, s.muted);
                    },
                    QStringLiteral("Link")});
    // One line; the gaps close up when the page is narrow.
    qreal needed = 0;
    for (const Entry &e : std::as_const(entries)) {
        needed += 22 + textWidth(g.small, e.label);
    }
    const qreal gap = std::clamp((g.area.width() - needed) / std::max<qsizetype>(1, entries.size() - 1), 4.0, 16.0);
    qreal x = g.area.left();
    for (const Entry &e : std::as_const(entries)) {
        e.swatch(x);
        x += 22;
        drawTextAt(painter, x, cy - QFontMetricsF(g.small).height() / 2, e.label, g.small, s.ink);
        x += textWidth(g.small, e.label) + gap;
    }
}

} // namespace QindaOffice::Plan::GanttPrint
