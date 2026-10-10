// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to connector routing (routing.cpp, routing_search.cpp; M87,
// D-095).

#include <QPointF>
#include <QRectF>
#include <QVector>

namespace QindaOffice::Diagram::RoutingDetail {

// Liang–Barsky: does segment a-b pass through the open rectangle `r`?
[[nodiscard]] bool segmentHitsRect(QPointF a, QPointF b, const QRectF &r);

// The shortest right-angle path from `a1` (leaving along `da`) to `b1`
// (arriving against `db`) that crosses none of `obstacles`, by length plus
// `bendCost` per bend, on the grid of lines through the two ends and a
// `clearance` outside every obstacle's edges (an orthogonal visibility
// graph). Empty when every way is blocked. Used when none of the router's
// quick L/Z/U candidates is clear (routing.cpp).
[[nodiscard]] QVector<QPointF> searchOrthogonal(QPointF a1, QPointF da, QPointF b1, QPointF db,
                                                const QVector<QRectF> &obstacles, double clearance, double bendCost);

} // namespace QindaOffice::Diagram::RoutingDetail
