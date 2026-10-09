// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Private to gantt_print*.cpp: the geometry of one laid-out Gantt printout.
// Every coordinate is in points on the page.

#include "gantt_print.h"
#include "work_calendar.h"

#include <QFont>
#include <QHash>

namespace QindaOffice::Plan::GanttPrint {

struct Column {
    QString field;
    QString title;
    qreal width = 0;
    bool right = false;
};

struct Geometry {
    Project project;
    GanttPrintOptions options;
    PlanPaperStyle style;
    WorkCalendar calendar;
    QRectF area;
    QList<int> rows;           // task indices printed, in order
    QHash<int, int> rowOfUid;  // task uid → printed row
    QList<Column> columns;
    qreal sheetWidth = 0;
    qreal headerHeight = 34;
    qreal tierHeight = 12;
    qreal rowHeight = 14;
    qreal legendHeight = 22;
    int rowsPerPage = 1;
    int rowPages = 1;
    int pagesWide = 1;
    QDate from;
    QDate to;
    qreal pointsPerDay = 1;
    QFont title;
    QFont body;
    QFont bold;
    QFont small;
    QFont smallBold;

    [[nodiscard]] int pageCount() const { return rowPages * pagesWide; }
    [[nodiscard]] qreal scaleTop() const { return area.top() + headerHeight; }
    [[nodiscard]] qreal rowsTop() const { return scaleTop() + 2 * tierHeight; }
    [[nodiscard]] qreal rowsBottom(int rowCount) const { return rowsTop() + rowCount * rowHeight; }
    // The chart's left edge on horizontal page h.
    [[nodiscard]] qreal chartLeft(int h) const { return h == 0 ? area.left() + sheetWidth : area.left(); }
    // x on horizontal page h of a fractional day offset from `from`.
    [[nodiscard]] qreal xOfDay(qreal day, int h) const;
    [[nodiscard]] qreal xOf(Minutes instant, int h) const;
    // The fractional day offset at page x.
    [[nodiscard]] qreal dayAt(qreal x, int h) const;
};

// gantt_print.cpp
[[nodiscard]] Geometry layOut(const Project &project, const GanttPrintOptions &options, const QRectF &area);
void paintPage(QPainter *painter, const Geometry &g, int page);

// gantt_print_bars.cpp
void paintTimescale(QPainter *painter, const Geometry &g, int h, qreal rowsBottom);
void paintBars(QPainter *painter, const Geometry &g, int h, int firstRow, int lastRow);
void paintLinks(QPainter *painter, const Geometry &g, int h, int firstRow, int lastRow);

// gantt_print_extras.cpp (M79): what goes under and around the bars —
// slack bars, deadline arrows, the leveling delay line — and the legend,
// which lists exactly what the chosen style and options draw.
void paintBarExtras(QPainter *painter, const Geometry &g, int h, int firstRow, int lastRow);
void paintLegend(QPainter *painter, const Geometry &g);
// The lower lane of a row: the chosen baseline ("gantt", "tracking") or the
// pre-leveled dates ("leveling"); false when the task has none.
bool lowerBar(const Geometry &g, const Task &t, Minutes *start, Minutes *finish);
// Shared drawing bits.
QPolygonF diamond(qreal cx, qreal cy, qreal half);
void arrowHead(QPainter *painter, QPointF tip, bool pointingRight, const QColor &color);
void dashedLine(QPainter *painter, qreal x, qreal top, qreal bottom, const QColor &color);
inline constexpr qreal kMilestoneHalf = 4.5;

} // namespace QindaOffice::Plan::GanttPrint
