// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Page-space geometry of shapes and connector ends (M50, D-050): the
// local -> page transform, rotated bounds, connection points with their
// outward directions, outline glue, and hit tests. Pure functions over the
// model; routing.cpp, ops.cpp, the painter and the app's canvas share them.

#include "drawing.h"

#include <QPainterPath>
#include <QPolygonF>
#include <QTransform>

namespace QindaOffice::Diagram {

// Local (0,0)-(w,h) -> page: flip about the box centre, rotate clockwise
// about it, place the box at rect.topLeft().
[[nodiscard]] QTransform shapeTransform(const Shape &shape);
// The four corners of the rotated box in page space (TL, TR, BR, BL).
[[nodiscard]] QPolygonF shapeCorners(const Shape &shape);
// Axis-aligned page bounds of the rotated box (without text below icons).
[[nodiscard]] QRectF shapeBounds(const Shape &shape);
// The outline in page space.
[[nodiscard]] QPainterPath pageOutline(const Shape &shape);
[[nodiscard]] bool shapeContains(const Shape &shape, QPointF pagePoint);

// Connection point `port` of the shape in page space, and the unit
// direction a line leaves it (axis-aligned in local space, then rotated).
[[nodiscard]] QPointF portPoint(const Shape &shape, int port);
[[nodiscard]] QPointF portDirection(const Shape &shape, int port);
[[nodiscard]] int portCount(const Shape &shape);
// The port nearest `pagePoint` within `maxDistance`, or -1.
[[nodiscard]] int nearestPort(const Shape &shape, QPointF pagePoint, double maxDistance);

// Where a line from the shape's centre towards `target` leaves the outline
// (dynamic glue). Falls back to the centre for a degenerate shape.
[[nodiscard]] QPointF outlinePoint(const Shape &shape, QPointF target);

// The side (0 top, 1 right, 2 bottom, 3 left) of the shape's box that faces
// `target` best, used by orthogonal dynamic glue.
[[nodiscard]] int facingSide(const Shape &shape, QPointF target);

// The resolved page point of an endpoint (glued ends follow their shape;
// `other` is where the line goes, for outline glue). A glue to a shape that
// no longer exists resolves to the stored point.
[[nodiscard]] QPointF endpointPoint(const Page &page, const Endpoint &end, QPointF other);

// Distance from `point` to the polyline.
[[nodiscard]] double distanceToPolyline(const QVector<QPointF> &polyline, QPointF point);

} // namespace QindaOffice::Diagram
