// SPDX-License-Identifier: GPL-3.0-or-later
// The Gantt printout's layout, header and sheet (gantt_print.h); the chart
// itself is drawn in gantt_print_bars.cpp.
#include "gantt_print.h"

#include "gantt_print_p.h"
#include "plan_edit.h"
#include "plan_time.h"

#include <QFontMetricsF>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <memory>

namespace QindaOffice::Plan {

namespace GanttPrint {

namespace {

qreal columnWidth(const QString &field)
{
    static const QHash<QString, qreal> widths{
        {QStringLiteral("id"), 22},         {QStringLiteral("wbs"), 34},          {QStringLiteral("mode"), 38},
        {QStringLiteral("name"), 168},      {QStringLiteral("duration"), 46},     {QStringLiteral("start"), 66},
        {QStringLiteral("finish"), 66},     {QStringLiteral("predecessors"), 62}, {QStringLiteral("successors"), 62},
        {QStringLiteral("resources"), 96},  {QStringLiteral("percent"), 40},      {QStringLiteral("work"), 46},
        {QStringLiteral("cost"), 62},       {QStringLiteral("notes"), 110}};
    return widths.value(field, 60);
}

bool rightAligned(const QString &field)
{
    return field == QLatin1String("id") || field == QLatin1String("duration") || field == QLatin1String("percent")
        || field == QLatin1String("work") || field.endsWith(QLatin1String("Cost")) || field == QLatin1String("cost")
        || field.endsWith(QLatin1String("Slack")) || field.endsWith(QLatin1String("Variance"));
}

bool hiddenByAncestor(const Project &p, int index, const QList<int> &hidden)
{
    for (int k = p.tasks.at(index).parentIndex; k >= 0; k = p.tasks.at(k).parentIndex) {
        if (hidden.contains(p.tasks.at(k).uid)) {
            return true;
        }
    }
    return false;
}

} // namespace

qreal Geometry::xOfDay(qreal day, int h) const
{
    const qreal firstChart = area.width() - sheetWidth;
    const qreal offset = h == 0 ? 0 : firstChart + (h - 1) * area.width();
    return chartLeft(h) + day * pointsPerDay - offset;
}

qreal Geometry::xOf(Minutes instant, int h) const
{
    return xOfDay(qreal(instant - toMinutes(from, 0)) / 1440.0, h);
}

qreal Geometry::dayAt(qreal x, int h) const
{
    const qreal firstChart = area.width() - sheetWidth;
    const qreal offset = h == 0 ? 0 : firstChart + (h - 1) * area.width();
    return (x - chartLeft(h) + offset) / pointsPerDay;
}

Geometry layOut(const Project &project, const GanttPrintOptions &options, const QRectF &area)
{
    Geometry g;
    g.project = project;
    g.options = options;
    g.area = area;
    g.calendar = WorkCalendar(project, project.info.calendarUid);
    g.pagesWide = std::clamp(options.pagesWide, 1, 50);
    g.title = paperFont(13, QFont::Bold);
    g.body = paperFont(8);
    g.bold = paperFont(8, QFont::DemiBold);
    g.small = paperFont(7);
    g.smallBold = paperFont(7, QFont::DemiBold);

    for (int i = 0; i < project.tasks.size(); ++i) {
        const Task &t = project.tasks.at(i);
        // A hidden task hides its subtree too.
        if (!options.rows.isEmpty() || options.hiddenTaskUids.contains(t.uid)
            || hiddenByAncestor(project, i, options.hiddenTaskUids)) {
            continue;
        }
        g.rowOfUid.insert(t.uid, int(g.rows.size()));
        g.rows.append(i);
    }
    for (int i : options.rows) { // M79: the view's own rows and order
        if (i >= 0 && i < project.tasks.size() && !g.rowOfUid.contains(project.tasks.at(i).uid)) {
            g.rowOfUid.insert(project.tasks.at(i).uid, int(g.rows.size()));
            g.rows.append(i);
        }
    }

    // Columns: the requested ones, narrowed and then dropped from the
    // right until the chart keeps at least 40% of the first page.
    for (const QString &field : options.columns) {
        g.columns.append({field, taskFieldTitle(field), columnWidth(field), rightAligned(field)});
    }
    const auto sheet = [&g] {
        qreal w = 0;
        for (const Column &c : std::as_const(g.columns)) {
            w += c.width;
        }
        return w;
    };
    const qreal minChart = area.width() * 0.4;
    for (Column &c : g.columns) {
        if (c.field == QLatin1String("name") && area.width() - sheet() < minChart) {
            c.width = std::max<qreal>(110, c.width - (minChart - (area.width() - sheet())));
        }
    }
    while (g.columns.size() > 2 && area.width() - sheet() < minChart) {
        g.columns.removeLast();
    }
    g.sheetWidth = sheet();

    // Date range and scale.
    g.from = options.from.isValid() ? options.from : dateOf(project.info.start);
    g.to = options.to.isValid() ? options.to : dateOf(project.finish != kNoDate ? project.finish : project.info.start);
    if (!options.from.isValid()) {
        g.from = g.from.addDays(1 - g.from.dayOfWeek());
    }
    if (!options.to.isValid()) {
        g.to = g.to.addDays(7 - g.to.dayOfWeek());
    }
    if (g.to < g.from) {
        g.to = g.from;
    }
    const qreal days = qreal(g.from.daysTo(g.to) + 1);
    const qreal chartWidth = (area.width() - g.sheetWidth) + (g.pagesWide - 1) * area.width();
    g.pointsPerDay = chartWidth / days;

    const qreal rowSpace = area.height() - g.headerHeight - 2 * g.tierHeight - g.legendHeight;
    g.rowsPerPage = std::max(1, int(std::floor(rowSpace / g.rowHeight)));
    g.rowPages = std::max(1, int((g.rows.size() + g.rowsPerPage - 1) / g.rowsPerPage));
    return g;
}

namespace {

void paintHeader(QPainter *painter, const Geometry &g, int page)
{
    const PlanPaperStyle &s = g.style;
    const QString title = g.project.info.title.isEmpty() ? QStringLiteral("Untitled project") : g.project.info.title;
    drawTextAt(painter, g.area.left(), g.area.top(), elideText(g.title, title, g.area.width() * 0.7), g.title, s.ink);
    const QLocale &l = g.options.locale;
    const QString kind = g.options.style == QLatin1String("tracking")   ? QStringLiteral("Tracking Gantt")
                         : g.options.style == QLatin1String("leveling") ? QStringLiteral("Leveling Gantt")
                                                                        : QStringLiteral("Gantt chart");
    QString sub = QStringLiteral("%1  ·  %2 – %3")
                      .arg(kind, l.toString(g.from, QStringLiteral("ddd d MMM yyyy")), l.toString(g.to, QStringLiteral("ddd d MMM yyyy")));
    if (g.options.statusDate != kNoDate) {
        sub += QStringLiteral("  ·  status date %1").arg(formatDate(g.options.statusDate, l));
    }
    drawTextAt(painter, g.area.left(), g.area.top() + 18, sub, g.small, s.muted);
    drawTextAt(painter, g.area.right(), g.area.top() + 2,
               QStringLiteral("Page %1 of %2").arg(page + 1).arg(g.pageCount()), g.small, s.muted, true);
    if (g.pagesWide > 1) {
        const int h = page / g.rowPages;
        drawTextAt(painter, g.area.right(), g.area.top() + 18,
                   QStringLiteral("Timescale %1 of %2").arg(h + 1).arg(g.pagesWide), g.small, s.muted, true);
    }
}

void paintSheet(QPainter *painter, const Geometry &g, int firstRow, int lastRow)
{
    const PlanPaperStyle &s = g.style;
    const qreal top = g.scaleTop();
    // Column headers span both timescale tiers, text on the lower one.
    painter->fillRect(QRectF(g.area.left(), top, g.sheetWidth, 2 * g.tierHeight), s.shade);
    qreal x = g.area.left();
    for (const Column &c : g.columns) {
        const QString text = elideText(g.smallBold, c.title, c.width - 6);
        drawTextAt(painter, c.right ? x + c.width - 3 : x + 3, top + g.tierHeight + 1.5, text, g.smallBold, s.ink, c.right);
        x += c.width;
    }
    for (int r = firstRow; r <= lastRow; ++r) {
        const int index = g.rows.at(r);
        const Task &t = g.project.tasks.at(index);
        const qreal y = g.rowsTop() + (r - firstRow) * g.rowHeight;
        const qreal textTop = y + (g.rowHeight - QFontMetricsF(g.body).height()) / 2;
        qreal cx = g.area.left();
        for (const Column &c : g.columns) {
            QString text = taskFieldText(g.project, index, c.field, g.options.locale);
            qreal indent = 0;
            QFont font = t.summary && c.field == QLatin1String("name") ? g.bold : g.body;
            if (c.field == QLatin1String("name")) {
                indent = (t.outlineLevel - 1) * 9.0;
            }
            const QColor ink = t.effectiveActive ? s.ink : s.muted;
            text = elideText(font, text, c.width - 6 - indent);
            drawTextAt(painter, c.right ? cx + c.width - 3 : cx + 3 + indent, textTop, text, font, ink, c.right);
            cx += c.width;
        }
    }
    // Column rules.
    painter->setPen(QPen(s.hairline, 0.5));
    qreal rx = g.area.left();
    const qreal bottom = g.rowsBottom(lastRow - firstRow + 1);
    for (int c = 0; c + 1 < g.columns.size(); ++c) {
        rx += g.columns.at(c).width;
        painter->drawLine(QPointF(rx, top), QPointF(rx, bottom));
    }
    painter->setPen(QPen(s.rule, 0.8));
    painter->drawLine(QPointF(g.area.left() + g.sheetWidth, top), QPointF(g.area.left() + g.sheetWidth, bottom));
}

} // namespace

void paintPage(QPainter *painter, const Geometry &g, int page)
{
    const PlanPaperStyle &s = g.style;
    const int h = page / g.rowPages;
    const int rowPage = page % g.rowPages;
    const int firstRow = rowPage * g.rowsPerPage;
    const int lastRow = std::min(int(g.rows.size()), firstRow + g.rowsPerPage) - 1;
    const int count = std::max(0, lastRow - firstRow + 1);
    paintHeader(painter, g, page);
    const qreal bottom = g.rowsBottom(count);
    // Zebra rows across sheet and chart.
    for (int r = 0; r < count; ++r) {
        if ((firstRow + r) % 2 == 1) {
            painter->fillRect(QRectF(g.area.left(), g.rowsTop() + r * g.rowHeight, g.area.width(), g.rowHeight), s.zebra);
        }
    }
    const QRectF chart(g.chartLeft(h), g.scaleTop(), g.area.right() - g.chartLeft(h), bottom - g.scaleTop());
    painter->save();
    painter->setClipRect(chart);
    paintTimescale(painter, g, h, bottom);
    if (count > 0) {
        paintLinks(painter, g, h, firstRow, lastRow);
        paintBarExtras(painter, g, h, firstRow, lastRow);
        paintBars(painter, g, h, firstRow, lastRow);
    }
    painter->restore();
    if (h == 0) {
        paintSheet(painter, g, firstRow, lastRow);
    }
    painter->setPen(QPen(s.rule, 0.8));
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(QRectF(g.area.left(), g.scaleTop(), g.area.width(), bottom - g.scaleTop()));
    painter->drawLine(QPointF(g.area.left(), g.rowsTop()), QPointF(g.area.right(), g.rowsTop()));
    if (page == g.pageCount() - 1) {
        paintLegend(painter, g);
    }
}

} // namespace GanttPrint

PageLayout ganttLayout(const Project &project, const GanttPrintOptions &options)
{
    return [project, options](const QRectF &area) {
        auto geometry = std::make_shared<GanttPrint::Geometry>(GanttPrint::layOut(project, options, area));
        PageSet set;
        set.count = geometry->pageCount();
        set.paint = [geometry](QPainter *painter, int page) { GanttPrint::paintPage(painter, *geometry, page); };
        return set;
    };
}

int ganttPageCount(const Project &project, const GanttPrintOptions &options, const QSizeF &pagePoints)
{
    return GanttPrint::layOut(project, options, contentArea(pagePoints)).pageCount();
}

PagePainter ganttPainter(const Project &project, const GanttPrintOptions &options)
{
    return pagedPainter(ganttLayout(project, options));
}

} // namespace QindaOffice::Plan
