// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Connector routing (M50, D-050). A route is a pure function of the page:
// the same shapes and connector always give the same path, so the canvas,
// print, PNG/SVG export and the tests agree without storing geometry.
//
//  * Orthogonal: leaves each glued end along its connection point's normal
//    (dynamic glue takes the side facing the other end), then picks the
//    cheapest of a few Manhattan candidates — L, Z and U shapes and detours
//    around every shape between the ends — by length, bends and shapes
//    crossed. Lanes, containers and text never count as obstacles.
//  * Straight: one segment; dynamic glue meets the outline.
//  * Curved: one cubic Bézier leaving and entering along the end normals.
//
// Line jumps (Page::lineJumps): a later connector hops over each earlier
// connector it crosses, on straight segments only — or, by the page's
// JumpOn (M87), the horizontal or the vertical one of the two; drawn as an
// arc, a gap or a square (Page::jumpStyle).
//
// M87 (D-095): when every quick candidate crosses a shape, an orthogonal
// route is searched on the visibility grid around the shapes
// (routing_search.cpp), so a right-angle connector goes round whatever is
// in its way whenever any way exists.

#include "drawing.h"

#include <QHash>
#include <QPainterPath>
#include <QPointF>
#include <QVector>

namespace QindaOffice::Diagram {

struct Jump {
    int segment = 0;       // index of the segment's start point
    double distance = 0;   // from the segment start, page units
    friend bool operator==(const Jump &, const Jump &) = default;
};

struct Route {
    // Polyline points, or for a curved route the four Bézier points.
    QVector<QPointF> points;
    bool curved = false;
    QVector<Jump> jumps;
    JumpStyle jumpStyle = JumpStyle::Arc; // M87: how routePath draws the jumps
    QPointF labelPoint;
    // Unit directions pointing INTO each end (arrowheads point along them).
    QPointF startDirection;
    QPointF endDirection;

    [[nodiscard]] QPointF start() const { return points.isEmpty() ? QPointF() : points.first(); }
    [[nodiscard]] QPointF end() const { return points.isEmpty() ? QPointF() : points.last(); }
};

// Distance an orthogonal route leaves an end before its first bend.
inline constexpr double kRouteStub = 18;
// Radius of a line jump.
inline constexpr double kJumpRadius = 5;

[[nodiscard]] Route routeConnector(const Page &page, const Connector &connector);
// Every connector of the page, with jumps when the page asks for them.
[[nodiscard]] QHash<QString, Route> routePage(const Page &page);
// The stroke path of a route (jumps drawn as half circles); `trimStart` and
// `trimEnd` shorten the ends so an arrowhead's tip lands on the end point.
[[nodiscard]] QPainterPath routePath(const Route &route, double trimStart = 0, double trimEnd = 0);
// A flattened polyline of the route (for hit tests and label placement).
[[nodiscard]] QVector<QPointF> routePolyline(const Route &route);

} // namespace QindaOffice::Diagram
