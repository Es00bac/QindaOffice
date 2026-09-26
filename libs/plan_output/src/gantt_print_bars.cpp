// SPDX-License-Identifier: GPL-3.0-or-later
// The Gantt printout's chart: timescale, non-working days, bars, links,
// today/status lines and the legend (gantt_print.h).
#include "gantt_print_p.h"
#include "plan_edit.h"
#include "plan_time.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace QindaOffice::Plan::GanttPrint {

namespace {

enum class Unit { Day, Week, Month, Year };

QDate unitStart(const QDate &d, Unit unit)
{
    switch (unit) {
    case Unit::Day:
        return d;
    case Unit::Week:
        return d.addDays(1 - d.dayOfWeek());
    case Unit::Month:
        return QDate(d.year(), d.month(), 1);
    case Unit::Year:
        return QDate(d.year(), 1, 1);
    }
    return d;
}

QDate advance(const QDate &d, Unit unit)
{
    switch (unit) {
    case Unit::Day:
        return d.addDays(1);
    case Unit::Week:
        return d.addDays(7);
    case Unit::Month:
        return d.addMonths(1);
    case Unit::Year:
        return d.addYears(1);
    }
    return d.addDays(1);
}

QString unitLabel(const QDate &d, Unit unit, bool major, qreal width, const Geometry &g)
{
    const QLocale &l = g.options.locale;
    const QFont &f = major ? g.smallBold : g.small;
    QStringList candidates;
    switch (unit) {
    case Unit::Day:
        // The short name's first letter: the C locale's narrow names are digits.
        candidates = {l.dayName(d.dayOfWeek(), QLocale::ShortFormat).left(1) + QLatin1Char(' ') + QString::number(d.day()),
                      QString::number(d.day())};
        break;
    case Unit::Week:
        candidates = major ? QStringList{l.toString(d, QStringLiteral("'Week of' d MMM yyyy")), l.toString(d, QStringLiteral("d MMM yyyy")),
                                         l.toString(d, QStringLiteral("d MMM"))}
                           : QStringList{l.toString(d, QStringLiteral("d MMM")), QString::number(d.day())};
        break;
    case Unit::Month:
        candidates = major ? QStringList{l.toString(d, QStringLiteral("MMMM yyyy")), l.toString(d, QStringLiteral("MMM yyyy")),
                                         l.toString(d, QStringLiteral("MMM"))}
                           : QStringList{l.toString(d, QStringLiteral("MMM")), l.toString(d, QStringLiteral("MMM")).left(1)};
        break;
    case Unit::Year:
        candidates = {QString::number(d.year())};
        break;
    }
    for (const QString &c : std::as_const(candidates)) {
        if (textWidth(f, c) + 4 <= width) {
            return c;
        }
    }
    return {};
}

void paintTier(QPainter *painter, const Geometry &g, int h, Unit unit, bool major, qreal top, qreal rowsBottom)
{
    const PlanPaperStyle &s = g.style;
    const qreal left = g.chartLeft(h);
    const qreal right = g.area.right();
    QDate d = unitStart(g.from.addDays(qint64(std::floor(g.dayAt(left, h)))), unit);
    const QDate last = g.from.addDays(qint64(std::ceil(g.dayAt(right, h))));
    for (int guard = 0; d <= last && guard < 2000; ++guard) {
        const QDate next = advance(d, unit);
        const qreal x0 = g.xOfDay(qreal(g.from.daysTo(d)), h);
        const qreal x1 = g.xOfDay(qreal(g.from.daysTo(next)), h);
        painter->setPen(QPen(s.rule, 0.5));
        painter->drawLine(QPointF(x0, top), QPointF(x0, top + g.tierHeight));
        if (major) {
            painter->setPen(QPen(s.hairline, 0.5));
            painter->drawLine(QPointF(x0, g.rowsTop()), QPointF(x0, rowsBottom));
        }
        // Labels stay on the visible part of their cell.
        const qreal vx0 = std::max(x0, left);
        const qreal vx1 = std::min(x1, right);
        const QString label = unitLabel(d, unit, major, vx1 - vx0, g);
        if (!label.isEmpty()) {
            const QFont &f = major ? g.smallBold : g.small;
            const qreal w = textWidth(f, label);
            const qreal x = major ? vx0 + 3 : (vx0 + vx1 - w) / 2;
            drawTextAt(painter, x, top + (g.tierHeight - QFontMetricsF(f).height()) / 2, label, f,
                       major ? s.ink : s.muted);
        }
        d = next;
    }
}

void dashedLine(QPainter *painter, qreal x, qreal top, qreal bottom, const QColor &color)
{
    QPen pen(color, 0.9);
    pen.setDashPattern({4, 3});
    painter->setPen(pen);
    painter->drawLine(QPointF(x, top), QPointF(x, bottom));
}

QPolygonF diamond(qreal cx, qreal cy, qreal half)
{
    return QPolygonF({QPointF(cx, cy - half), QPointF(cx + half, cy), QPointF(cx, cy + half), QPointF(cx - half, cy)});
}

void arrowHead(QPainter *painter, QPointF tip, bool pointingRight, const QColor &color)
{
    const qreal d = pointingRight ? -3.2 : 3.2;
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    painter->drawPolygon(QPolygonF({tip, QPointF(tip.x() + d, tip.y() - 2.2), QPointF(tip.x() + d, tip.y() + 2.2)}));
}

constexpr qreal kMilestoneHalf = 4.5;

} // namespace

void paintTimescale(QPainter *painter, const Geometry &g, int h, qreal rowsBottom)
{
    const PlanPaperStyle &s = g.style;
    const qreal left = g.chartLeft(h);
    const qreal right = g.area.right();
    painter->fillRect(QRectF(left, g.scaleTop(), right - left, 2 * g.tierHeight), s.paper);
    if (g.pointsPerDay >= 1.2) {
        const qint64 d0 = qint64(std::floor(g.dayAt(left, h)));
        const qint64 d1 = qint64(std::ceil(g.dayAt(right, h)));
        for (qint64 d = d0; d <= d1; ++d) {
            if (!g.calendar.isWorkingDay(g.from.addDays(d))) {
                const qreal x0 = g.xOfDay(qreal(d), h);
                painter->fillRect(QRectF(x0, g.rowsTop(), g.pointsPerDay, rowsBottom - g.rowsTop()), s.shade);
            }
        }
    }
    Unit major = Unit::Year;
    Unit minor = Unit::Month;
    if (g.pointsPerDay >= 11) {
        major = Unit::Week;
        minor = Unit::Day;
    } else if (g.pointsPerDay >= 2.2) {
        major = Unit::Month;
        minor = Unit::Week;
    }
    paintTier(painter, g, h, major, true, g.scaleTop(), rowsBottom);
    paintTier(painter, g, h, minor, false, g.scaleTop() + g.tierHeight, rowsBottom);
    painter->setPen(QPen(s.rule, 0.5));
    painter->drawLine(QPointF(left, g.scaleTop() + g.tierHeight), QPointF(right, g.scaleTop() + g.tierHeight));
    if (g.options.today != kNoDate) {
        dashedLine(painter, g.xOf(g.options.today, h), g.rowsTop(), rowsBottom, s.accent);
    }
    if (g.options.statusDate != kNoDate) {
        dashedLine(painter, g.xOf(g.options.statusDate, h), g.rowsTop(), rowsBottom, s.critical);
    }
}

void paintBars(QPainter *painter, const Geometry &g, int h, int firstRow, int lastRow)
{
    const PlanPaperStyle &s = g.style;
    const int b = g.options.baseline;
    for (int r = firstRow; r <= lastRow; ++r) {
        const int index = g.rows.at(r);
        const Task &t = g.project.tasks.at(index);
        if (t.start == kNoDate) {
            continue;
        }
        const qreal y = g.rowsTop() + (r - firstRow) * g.rowHeight;
        const qreal cy = y + g.rowHeight / 2;
        const bool critical = g.options.showCritical && t.critical;
        const QColor color = critical ? s.critical : s.accent;
        const bool hasBaseline = b >= 0 && b < kBaselineCount && t.baselines[size_t(b)].set;
        const qreal x0 = g.xOf(t.start, h);
        const qreal x1 = std::max(x0 + 0.8, g.xOf(t.finish, h));
        qreal textX = x1 + 7; // clear of the link stubs leaving the bar end (4pt)
        if (hasBaseline) {
            const BaselineData &bl = t.baselines[size_t(b)];
            const qreal bx0 = g.xOf(bl.start, h);
            const qreal bx1 = g.xOf(bl.finish, h);
            painter->setPen(Qt::NoPen);
            painter->setBrush(s.baseline);
            if (t.isMilestone()) {
                painter->setBrush(Qt::NoBrush);
                painter->setPen(QPen(s.baseline, 0.9));
                painter->drawPolygon(diamond(bx0, cy + 1.5, 3.4));
            } else {
                painter->drawRect(QRectF(bx0, y + g.rowHeight - 4.4, std::max<qreal>(0.8, bx1 - bx0), 2.6));
            }
        }
        if (t.summary) {
            const qreal top = y + 3.2;
            painter->setPen(Qt::NoPen);
            painter->setBrush(s.ink); // summaries stay ink, as in Project, critical or not
            painter->drawRect(QRectF(x0, top, x1 - x0, 3.4));
            painter->drawPolygon(QPolygonF({QPointF(x0, top), QPointF(x0 + 4.5, top), QPointF(x0, top + 7.5)}));
            painter->drawPolygon(QPolygonF({QPointF(x1, top), QPointF(x1 - 4.5, top), QPointF(x1, top + 7.5)}));
            continue;
        }
        if (t.isMilestone()) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(critical ? s.critical : s.ink);
            painter->drawPolygon(diamond(x0, cy, kMilestoneHalf));
            const QString date = g.options.locale.toString(dateOf(t.start), QStringLiteral("d MMM"));
            drawTextAt(painter, x0 + kMilestoneHalf + 3, cy - QFontMetricsF(g.small).height() / 2, date, g.small, s.ink);
            textX = x0 + kMilestoneHalf + 3 + textWidth(g.small, date) + 5;
        } else {
            const qreal barH = hasBaseline ? 6.4 : 8;
            const QRectF bar(x0, y + 2.6, x1 - x0, barH);
            if (!t.effectiveActive) {
                QPen pen(s.muted, 0.7);
                pen.setDashPattern({2, 2});
                painter->setPen(pen);
                painter->setBrush(Qt::NoBrush);
                painter->drawRect(bar);
            } else {
                painter->setPen(QPen(color, 0.6));
                painter->setBrush(s.tint(color, 0.42));
                painter->drawRect(bar);
                if (t.percentComplete > 0) {
                    const qreal band = barH * 0.44;
                    painter->setPen(Qt::NoPen);
                    painter->setBrush(color.darker(115));
                    painter->drawRect(QRectF(x0, bar.center().y() - band / 2, bar.width() * t.percentComplete / 100.0, band));
                }
            }
        }
        if (g.options.showBarText) {
            const QString text = taskFieldText(g.project, index, QStringLiteral("resources"), g.options.locale);
            if (!text.isEmpty()) {
                drawTextAt(painter, textX, cy - QFontMetricsF(g.small).height() / 2, text, g.small, s.muted);
            }
        }
    }
}

void paintLinks(QPainter *painter, const Geometry &g, int h, int firstRow, int lastRow)
{
    const PlanPaperStyle &s = g.style;
    for (int r = firstRow; r <= lastRow; ++r) {
        const Task &t = g.project.tasks.at(g.rows.at(r));
        for (int l = 0; l < t.predecessors.size(); ++l) {
            if (t.ignoredLinks.contains(l)) {
                continue;
            }
            const Link &link = t.predecessors.at(l);
            const int pr = g.rowOfUid.value(link.predecessorUid, -1);
            if (pr < firstRow || pr > lastRow) {
                continue;
            }
            const Task &p = g.project.tasks.at(g.rows.at(pr));
            if (p.start == kNoDate || t.start == kNoDate) {
                continue;
            }
            const bool fromEnd = link.type == LinkType::FinishToStart || link.type == LinkType::FinishToFinish;
            const bool intoStart = link.type == LinkType::FinishToStart || link.type == LinkType::StartToStart;
            qreal x1 = g.xOf(fromEnd ? p.finish : p.start, h);
            qreal x2 = g.xOf(intoStart ? t.start : t.finish, h);
            if (p.isMilestone()) {
                x1 += fromEnd ? kMilestoneHalf : -kMilestoneHalf;
            }
            if (t.isMilestone()) {
                x2 += intoStart ? -kMilestoneHalf : kMilestoneHalf;
            }
            const qreal y1 = g.rowsTop() + (pr - firstRow + 0.5) * g.rowHeight;
            const qreal y2 = g.rowsTop() + (r - firstRow + 0.5) * g.rowHeight;
            const qreal outX = fromEnd ? x1 + 4 : x1 - 4;
            const qreal inX = intoStart ? x2 - 5 : x2 + 5;
            QPainterPath path(QPointF(x1, y1));
            path.lineTo(outX, y1);
            if ((intoStart && outX <= inX) || (!intoStart && outX >= inX)) {
                path.lineTo(outX, y2);
            } else {
                const qreal gap = y2 > y1 ? y2 - g.rowHeight / 2 : y2 + g.rowHeight / 2;
                path.lineTo(outX, gap);
                path.lineTo(inX, gap);
                path.lineTo(inX, y2);
            }
            path.lineTo(x2, y2);
            const QColor color = g.options.showCritical && t.critical && p.critical ? s.critical : s.muted;
            painter->setPen(QPen(color, 0.6));
            painter->setBrush(Qt::NoBrush);
            painter->drawPath(path);
            arrowHead(painter, QPointF(x2, y2), intoStart, color);
        }
    }
}

void paintLegend(QPainter *painter, const Geometry &g)
{
    const PlanPaperStyle &s = g.style;
    const qreal y = g.area.bottom() - g.legendHeight + 8;
    const qreal cy = y + 5;
    qreal x = g.area.left();
    const auto label = [&](const QString &text) {
        drawTextAt(painter, x, cy - QFontMetricsF(g.small).height() / 2, text, g.small, s.ink);
        x += textWidth(g.small, text) + 16;
    };
    const auto swatchBar = [&](const QColor &c, bool progress) {
        painter->setPen(QPen(c, 0.6));
        painter->setBrush(s.tint(c, 0.42));
        painter->drawRect(QRectF(x, cy - 3.5, 18, 7));
        if (progress) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(c.darker(115));
            painter->drawRect(QRectF(x, cy - 1.6, 11, 3.2));
        }
        x += 22;
    };
    swatchBar(s.accent, false);
    label(QStringLiteral("Task"));
    if (g.options.showCritical) {
        swatchBar(s.critical, false);
        label(QStringLiteral("Critical task"));
    }
    swatchBar(s.accent, true);
    label(QStringLiteral("Progress"));
    if (g.options.baseline >= 0) {
        painter->fillRect(QRectF(x, cy - 1.3, 18, 2.6), s.baseline);
        x += 22;
        label(g.options.baseline == 0 ? QStringLiteral("Baseline") : QStringLiteral("Baseline %1").arg(g.options.baseline));
    }
    painter->setPen(Qt::NoPen);
    painter->setBrush(s.ink);
    painter->drawRect(QRectF(x, cy - 2, 18, 3.4));
    x += 22;
    label(QStringLiteral("Summary"));
    painter->setPen(Qt::NoPen);
    painter->setBrush(s.ink);
    painter->drawPolygon(diamond(x + 4, cy, kMilestoneHalf));
    x += 12;
    label(QStringLiteral("Milestone"));
    if (g.options.today != kNoDate) {
        dashedLine(painter, x + 4, cy - 5, cy + 5, s.accent);
        x += 10;
        label(QStringLiteral("Today"));
    }
    if (g.options.statusDate != kNoDate) {
        dashedLine(painter, x + 4, cy - 5, cy + 5, s.critical);
        x += 10;
        label(QStringLiteral("Status date"));
    }
    painter->setPen(QPen(s.muted, 0.6));
    painter->setBrush(Qt::NoBrush);
    QPainterPath link(QPointF(x, cy - 3));
    link.lineTo(x + 6, cy - 3);
    link.lineTo(x + 6, cy + 3);
    link.lineTo(x + 14, cy + 3);
    painter->drawPath(link);
    arrowHead(painter, QPointF(x + 16, cy + 3), true, s.muted);
    x += 22;
    label(QStringLiteral("Link"));
}

} // namespace QindaOffice::Plan::GanttPrint
