// SPDX-License-Identifier: GPL-3.0-or-later
// The Network Diagram on screen, paper and PDF (network_print.h).
#include "network_print.h"

#include "plan_edit.h"
#include "plan_time.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <memory>

namespace QindaOffice::Plan {

namespace {

constexpr qreal kHeaderHeight = 34;
constexpr qreal kLegendHeight = 22;

QPainterPath boxPath(const QRectF &r, bool milestone)
{
    QPainterPath path;
    if (milestone) {
        // A hexagon: pointed ends, as Project draws a milestone.
        const qreal d = std::min<qreal>(10, r.height() / 2);
        path.moveTo(r.left() + d, r.top());
        path.lineTo(r.right() - d, r.top());
        path.lineTo(r.right(), r.center().y());
        path.lineTo(r.right() - d, r.bottom());
        path.lineTo(r.left() + d, r.bottom());
        path.lineTo(r.left(), r.center().y());
        path.closeSubpath();
    } else {
        path.addRect(r);
    }
    return path;
}

void paintBox(QPainter *painter, const Project &project, const NetworkNode &node, const NetworkPaintOptions &o,
              const PlanPaperStyle &s)
{
    const Task &t = project.tasks.at(node.taskIndex);
    const QRectF r = node.box;
    const bool critical = o.showCritical && t.critical;
    const bool milestone = !t.summary && t.isMilestone();
    const QColor edge = !t.effectiveActive ? s.muted : t.summary ? s.ink : critical ? s.critical : s.accent;
    if (o.selectedTaskUid != 0 && o.selectedTaskUid == t.uid) {
        painter->setPen(QPen(s.accent, 2.4));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(boxPath(r.adjusted(-3.5, -3.5, 3.5, 3.5), milestone));
    }
    QPen pen(edge, critical ? 1.6 : 1.0);
    if (!t.effectiveActive) {
        pen.setDashPattern({3, 2});
    }
    painter->setPen(pen);
    painter->setBrush(critical ? s.tint(s.critical, 0.06) : s.paper);
    painter->drawPath(boxPath(r, milestone));
    if (t.summary) {
        painter->fillRect(QRectF(r.left(), r.top(), r.width(), 3.2), s.ink);
    }
    // Progress: done crossed out, under way struck once (Project's marks).
    if (t.percentComplete > 0) {
        painter->setPen(QPen(s.muted, 0.8));
        painter->drawLine(r.bottomLeft(), r.topRight());
        if (t.percentComplete >= 100) {
            painter->drawLine(r.topLeft(), r.bottomRight());
        }
    }
    const QFont name = paperFont(8, QFont::DemiBold);
    const QFont body = paperFont(7);
    const qreal pad = milestone ? 11 : 5;
    const qreal width = r.width() - 2 * pad;
    const QLocale &l = o.locale;
    const QColor ink = t.effectiveActive ? s.ink : s.muted;
    qreal y = r.top() + 4.5;
    drawTextAt(painter, r.left() + pad, y, elideText(name, t.name, width), name, ink);
    y += 12.5;
    const auto pair = [&](const QString &left, const QString &right) {
        const qreal rightWidth = textWidth(body, right);
        drawTextAt(painter, r.left() + pad, y, elideText(body, left, width - rightWidth - 6), body, ink);
        drawTextAt(painter, r.right() - pad, y, right, body, s.muted, true);
        y += 11;
    };
    const DurationFormat fmt = durationFormatOf(project.info);
    if (milestone) {
        pair(QStringLiteral("Date: %1").arg(formatDate(t.finish, l)), QStringLiteral("ID: %1").arg(t.id));
    } else {
        pair(QStringLiteral("Start: %1").arg(formatDate(t.start, l)), QStringLiteral("ID: %1").arg(t.id));
        pair(QStringLiteral("Finish: %1").arg(formatDate(t.finish, l)),
             QStringLiteral("Dur: %1").arg(formatDuration(t.duration, t.summary ? DurationUnit::Days : t.durationUnit, fmt,
                                                          t.estimated, l)));
    }
    if (!t.summary) {
        const QString res = resourceNamesText(project, node.taskIndex, l);
        if (!res.isEmpty()) {
            drawTextAt(painter, r.left() + pad, y, elideText(body, QStringLiteral("Res: %1").arg(res), width), body, s.muted);
        }
    }
}

void paintEdge(QPainter *painter, const Project &project, const NetworkLayout &layout, const NetworkEdge &e,
               const NetworkPaintOptions &o, const PlanPaperStyle &s)
{
    if (e.points.size() < 2) {
        return;
    }
    const QColor c = o.showCritical && e.critical ? s.critical : s.muted;
    QPainterPath path(e.points.first());
    for (int i = 1; i < e.points.size(); ++i) {
        path.lineTo(e.points.at(i));
    }
    painter->setPen(QPen(c, o.showCritical && e.critical ? 1.3 : 0.8));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path);
    const QPointF tip = e.points.last();
    painter->setPen(Qt::NoPen);
    painter->setBrush(c);
    painter->drawPolygon(QPolygonF({tip, QPointF(tip.x() - 5, tip.y() - 3), QPointF(tip.x() - 5, tip.y() + 3)}));
    if (e.type != LinkType::FinishToStart) {
        const QFont f = paperFont(6, QFont::DemiBold);
        const QPointF at = e.points.at(e.points.size() - 2);
        drawTextAt(painter, at.x() + 2, at.y() - QFontMetricsF(f).height() - 1, linkTypeCode(e.type), f, c);
    }
    Q_UNUSED(project)
    Q_UNUSED(layout)
}

struct Paged {
    Project project;
    NetworkLayout layout;
    NetworkPrintOptions options;
    QRectF area;
    qreal scale = 1;
    int pagesWide = 1;
    int pagesTall = 1;
    // Where each column / row of pages starts, in layout points.
    QList<qreal> xStarts{0};
    QList<qreal> yStarts{0};
    [[nodiscard]] qreal chartHeight() const { return area.height() - kHeaderHeight - kLegendHeight; }
};

// AGENT-NOTE: a page ends in the gap before the last column (row) of
// boxes that fits on it, so no box is ever cut between two sheets; only a
// box wider than a page (never at these sizes) would be cut.
QList<qreal> pageStarts(qreal total, qreal span, const QList<qreal> &gaps)
{
    QList<qreal> starts{0};
    qreal start = 0;
    while (start + span < total - 0.5 && starts.size() < 500) {
        qreal next = start + span;
        for (qreal gap : gaps) {
            if (gap > start + 1 && gap <= start + span) {
                next = gap;
            }
        }
        starts.append(next);
        start = next;
    }
    return starts;
}

std::shared_ptr<Paged> layOutPages(const Project &project, const NetworkPrintOptions &options, const QRectF &area)
{
    auto p = std::make_shared<Paged>();
    p->project = project;
    p->options = options;
    p->area = area;
    QList<int> rows = options.rows;
    if (rows.isEmpty()) {
        for (int i = 0; i < project.tasks.size(); ++i) {
            rows.append(i);
        }
    }
    p->layout = layOutNetwork(project, rows);
    const qreal width = std::max<qreal>(1, p->layout.size.width());
    const qreal height = std::max<qreal>(1, p->layout.size.height());
    const qreal fit = std::max(1, options.pagesWide) * area.width() / width;
    p->scale = std::clamp(fit, kMinNetworkScale, 1.0);
    // A diagram a little too tall for one page row shrinks to fit it when
    // that stays readable (≥ kMinNetworkScale) rather than spill a page.
    const qreal tall = std::max<qreal>(1, p->chartHeight()) / height;
    if (height * p->scale > p->chartHeight() && tall >= kMinNetworkScale) {
        p->scale = std::min(p->scale, tall);
    }
    const NetworkOptions o;
    QList<qreal> columnGaps;
    for (int c = 1; c < p->layout.columns; ++c) {
        columnGaps.append(o.margin + c * (o.boxWidth + o.columnGap) - o.columnGap / 2);
    }
    QList<qreal> rowGaps;
    for (int r = 1; r < p->layout.rows; ++r) {
        rowGaps.append(o.margin + r * (o.boxHeight + o.rowGap) - o.rowGap / 2);
    }
    p->xStarts = pageStarts(width, area.width() / p->scale, columnGaps);
    p->yStarts = pageStarts(height, std::max<qreal>(1, p->chartHeight()) / p->scale, rowGaps);
    p->pagesWide = int(p->xStarts.size());
    p->pagesTall = int(p->yStarts.size());
    return p;
}

void paintLegend(QPainter *painter, const Paged &p, const PlanPaperStyle &s)
{
    const QFont f = paperFont(7);
    const qreal cy = p.area.bottom() - kLegendHeight + 13;
    qreal x = p.area.left();
    const auto swatch = [&](const QColor &edge, qreal width, const QString &label) {
        painter->setPen(QPen(edge, width));
        painter->setBrush(s.paper);
        painter->drawRect(QRectF(x, cy - 4, 18, 8));
        x += 22;
        drawTextAt(painter, x, cy - QFontMetricsF(f).height() / 2, label, f, s.ink);
        x += textWidth(f, label) + 16;
    };
    swatch(s.accent, 1.0, QStringLiteral("Task"));
    if (p.options.showCritical) {
        swatch(s.critical, 1.6, QStringLiteral("Critical task"));
    }
    swatch(s.ink, 1.0, QStringLiteral("Summary"));
    painter->setPen(QPen(s.muted, 0.8));
    painter->drawRect(QRectF(x, cy - 4, 18, 8));
    painter->drawLine(QPointF(x, cy + 4), QPointF(x + 18, cy - 4));
    x += 22;
    drawTextAt(painter, x, cy - QFontMetricsF(f).height() / 2, QStringLiteral("In progress"), f, s.ink);
    x += textWidth(f, QStringLiteral("In progress")) + 16;
    painter->drawRect(QRectF(x, cy - 4, 18, 8));
    painter->drawLine(QPointF(x, cy + 4), QPointF(x + 18, cy - 4));
    painter->drawLine(QPointF(x, cy - 4), QPointF(x + 18, cy + 4));
    x += 22;
    drawTextAt(painter, x, cy - QFontMetricsF(f).height() / 2, QStringLiteral("Complete"), f, s.ink);
}

void paintPage(QPainter *painter, const Paged &p, int page)
{
    const PlanPaperStyle s;
    const int h = page / p.pagesTall;
    const int v = page % p.pagesTall;
    const QFont title = paperFont(13, QFont::Bold);
    const QFont small = paperFont(7);
    const QString name = p.project.info.title.isEmpty() ? QStringLiteral("Untitled project") : p.project.info.title;
    drawTextAt(painter, p.area.left(), p.area.top(), elideText(title, name, p.area.width() * 0.7), title, s.ink);
    QString sub = QStringLiteral("Network diagram  ·  %1 – %2  ·  %3%")
                      .arg(formatDate(p.project.info.start, p.options.locale), formatDate(p.project.finish, p.options.locale))
                      .arg(std::lround(p.scale * 100));
    if (p.options.statusDate != kNoDate) {
        sub += QStringLiteral("  ·  status date %1").arg(formatDate(p.options.statusDate, p.options.locale));
    }
    drawTextAt(painter, p.area.left(), p.area.top() + 18, sub, small, s.muted);
    drawTextAt(painter, p.area.right(), p.area.top() + 2,
               QStringLiteral("Page %1 of %2").arg(page + 1).arg(p.pagesWide * p.pagesTall), small, s.muted, true);
    // This page shows [xStarts[h], next start) × [yStarts[v], next start).
    const qreal x0 = p.xStarts.at(h);
    const qreal y0 = p.yStarts.at(v);
    const qreal x1 = h + 1 < p.xStarts.size() ? p.xStarts.at(h + 1) : p.layout.size.width();
    const qreal y1 = v + 1 < p.yStarts.size() ? p.yStarts.at(v + 1) : p.layout.size.height();
    const QRectF chart(p.area.left(), p.area.top() + kHeaderHeight, std::min(p.area.width(), (x1 - x0) * p.scale),
                       std::min(p.chartHeight(), (y1 - y0) * p.scale));
    painter->save();
    painter->setClipRect(chart);
    painter->translate(chart.left() - x0 * p.scale, chart.top() - y0 * p.scale);
    painter->scale(p.scale, p.scale);
    NetworkPaintOptions o;
    o.locale = p.options.locale;
    o.showCritical = p.options.showCritical;
    paintNetwork(painter, p.project, p.layout, o);
    painter->restore();
    if (page == p.pagesWide * p.pagesTall - 1) {
        paintLegend(painter, p, s);
    }
}

} // namespace

void paintNetwork(QPainter *painter, const Project &project, const NetworkLayout &layout, const NetworkPaintOptions &options)
{
    const PlanPaperStyle s;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    for (const NetworkEdge &e : layout.edges) {
        if (!(options.showCritical && e.critical)) {
            paintEdge(painter, project, layout, e, options, s);
        }
    }
    for (const NetworkEdge &e : layout.edges) { // critical links on top
        if (options.showCritical && e.critical) {
            paintEdge(painter, project, layout, e, options, s);
        }
    }
    for (const NetworkNode &node : layout.nodes) {
        paintBox(painter, project, node, options, s);
    }
    painter->restore();
}

PageLayout networkPages(const Project &project, const NetworkPrintOptions &options)
{
    return [project, options](const QRectF &area) {
        const std::shared_ptr<Paged> paged = layOutPages(project, options, area);
        PageSet set;
        set.count = paged->pagesWide * paged->pagesTall;
        set.paint = [paged](QPainter *painter, int page) { paintPage(painter, *paged, page); };
        return set;
    };
}

int networkPageCount(const Project &project, const NetworkPrintOptions &options, const QSizeF &pagePoints)
{
    const std::shared_ptr<Paged> paged = layOutPages(project, options, contentArea(pagePoints));
    return paged->pagesWide * paged->pagesTall;
}

PagePainter networkPainter(const Project &project, const NetworkPrintOptions &options)
{
    return pagedPainter(networkPages(project, options));
}

} // namespace QindaOffice::Plan
