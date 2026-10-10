// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to the container and swimlane ops (M87, D-095;
// ops_containers.cpp, ops_lanes.cpp): the geometry they share.
//
// "Along" is the axis a lane runs (x for a horizontal lane), "across" the
// axis the lanes of a pool stack on.
//
// AGENT-GUARD (implicit sharing): take a copy of a Page (`before`) BEFORE
// taking a reference or pointer into its shapes for writing — a reference
// taken first points into the buffer the copy then shares, and writing
// through it changes the copy as well.

#include "drawing.h"

#include <QHash>
#include <QMarginsF>
#include <QSet>
#include <QStringList>

#include <cmath>

namespace QindaOffice::Diagram::Ops::Pool {

inline constexpr double kTolerance = 1.0; // page units: edges this close touch

inline bool near(double a, double b)
{
    return std::abs(a - b) <= kTolerance;
}

inline double alongStart(const QRectF &r, Qt::Orientation o) { return o == Qt::Horizontal ? r.left() : r.top(); }
inline double alongLength(const QRectF &r, Qt::Orientation o) { return o == Qt::Horizontal ? r.width() : r.height(); }
inline double acrossStart(const QRectF &r, Qt::Orientation o) { return o == Qt::Horizontal ? r.top() : r.left(); }
inline double acrossLength(const QRectF &r, Qt::Orientation o) { return o == Qt::Horizontal ? r.height() : r.width(); }

inline QRectF makeRect(Qt::Orientation o, double along, double alongLen, double across, double acrossLen)
{
    return o == Qt::Horizontal ? QRectF(along, across, alongLen, acrossLen) : QRectF(across, along, acrossLen, alongLen);
}

inline QPointF acrossDelta(Qt::Orientation o, double d)
{
    return o == Qt::Horizontal ? QPointF(0, d) : QPointF(d, 0);
}

[[nodiscard]] bool isPhase(const Shape &shape);
// Moves the shapes `ids` by `delta`, with the free ends of connectors
// hanging from them (Ops::moveItems' rule, without its own membership).
void translate(Page &page, const QSet<QString> &ids, QPointF delta);
// Every member of `id`, at every depth.
[[nodiscard]] QSet<QString> membersDeep(const Page &page, const QString &id);
// The rect container `c` needs to hold `content`: its margin and header band.
[[nodiscard]] QRectF holding(const Shape &c, const QRectF &content);
// The header band's thickness on each side.
[[nodiscard]] QMarginsF headerOf(const Shape &c);
[[nodiscard]] QRectF poolBounds(const Page &page, const QStringList &pool);
// Phases that spanned `before` across a pool span `after` now.
bool stretchPhases(Page &page, Qt::Orientation o, const QRectF &before, const QRectF &after);
// Puts a pool (its lanes in order, as in `before`) edited in `page` back in
// order: equally long (the edited lane's length wins), stacked edge to edge
// from where it started (the first lane's own edge when it is the one that
// was resized), a lane dragged along the stack taking its new place.
bool syncPool(Page &page, const Page &before, const QStringList &pool, const QString &edited);

} // namespace QindaOffice::Diagram::Ops::Pool
