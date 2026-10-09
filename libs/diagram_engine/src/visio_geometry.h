// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to the Visio interchange (M65, D-073): Geometry sections ⇄ paths.
//
// Reading turns every visible Geometry section of a resolved sheet into one
// figure in the shape's LOCAL space (inches, y up, origin at the box's
// bottom-left): MoveTo, LineTo, ArcTo (a bow), EllipticalArcTo (a point on
// the arc, the axis angle and the axis ratio), Ellipse, the relative rows
// (RelMoveTo, RelLineTo, RelCubBezTo, RelQuadBezTo, RelEllipticalArcTo:
// fractions of Width/Height), PolylineTo and NURBSTo (their points in the
// POLYLINE()/NURBS() value) and SplineStart/SplineKnot. Arcs become cubic
// Béziers; NURBS and splines are sampled. InfiniteLine (a guide's line
// across the page) is skipped.
//
// Writing produces rows a reader without a formula engine draws right and
// that still scale when Visio resizes the shape: Rel* rows (fractions of
// Width/Height) for a shape outline, absolute rows for a connector route.

#include "visio_sheet.h"

#include <QList>
#include <QPainterPath>
#include <QVector>

namespace QindaOffice::Diagram::Visio {

struct Figure {
    QPainterPath path; // local inches, y up
    bool fill = true;  // NoFill off and every subpath closed
    bool stroke = true; // NoLine off
};

[[nodiscard]] QList<Figure> readGeometry(const Sheet &sheet, double width, double height);

// A path through `p0`, `p1` (on the arc) and `p2` of an ellipse whose first
// axis is turned `angle` radians and is `ratio` times the second (1 = a
// circle). Collinear points give a straight line. Exposed for the tests.
void appendArcThrough(QPainterPath &path, QPointF p0, QPointF p1, QPointF p2, double angle = 0, double ratio = 1);

// <Section N="Geometry" IX=ix> drawing `normalized` (0..1, y DOWN — a
// Shape::paths subpath) with Rel* rows.
[[nodiscard]] XmlElementPtr outlineSection(const QPainterPath &normalized, int ix, bool noFill, bool noLine);
// <Section N="Geometry" IX=ix> through `points` (local inches, y up): a
// polyline, or with `curved` one cubic Bézier of four points.
[[nodiscard]] XmlElementPtr routeSection(const QVector<QPointF> &points, bool curved, double width, double height,
                                         int ix);

} // namespace QindaOffice::Diagram::Visio
