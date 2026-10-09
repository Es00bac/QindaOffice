// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The renderer's building blocks (internal to show_model; the public entry
// points are in slide_renderer.h). Everything paints in slide points.
//   shape_painter.cpp  shapes of every kind, fills, outlines, arrowheads
//   table_painter.cpp  tables with their style regions and merges
//   chart_painter.cpp  charts from cached values
//   text_painter.cpp   text bodies (text_painter.h)

#include "presentation.h"

#include <QPainterPath>
#include <QPen>

class QPainter;

namespace QindaOffice::Show::Render {

// One shape with its rotation and flips (children of groups included).
void paintShape(QPainter *painter, const Shape &shape);

// `path` filled with `style`, or with `plain` when the style is Plain.
void paintFill(QPainter *painter, const QPainterPath &path, const QColor &plain, const FillStyle &style);

// The pen an outline draws with; Qt::NoPen when it is invisible.
[[nodiscard]] QPen outlinePen(const QColor &color, qreal width, const LineStyle &line);

void paintTable(QPainter *painter, const QRectF &rect, const Table &table);
void paintChart(QPainter *painter, const QRectF &rect, const ChartData &chart);

// A grey box with a caption, for content QindaShow keeps but cannot draw.
void paintPlaceholderBox(QPainter *painter, const QRectF &rect, const QString &label);

} // namespace QindaOffice::Show::Render
