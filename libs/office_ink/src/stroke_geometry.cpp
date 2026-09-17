// SPDX-License-Identifier: GPL-3.0-or-later
#include "stroke_geometry.h"

#include "stroke_model.h"

#include <QLineF>

#include <cmath>

namespace QindaOffice::Ink {

namespace {

// Page units between polyline points when the smoothed centreline is
// flattened for the outline walk: small enough that a fast curve keeps
// its shape, large enough that a 5 000-stroke page stays cheap.
constexpr qreal kMaxStep = 2.0;
// Arc samples per round cap.
constexpr int kCapSegments = 8;

// One flattened centreline point with the stroke's half width at it, so
// the outline pass needs no second trip through the samples.
struct WalkPoint {
    QPointF pos;
    qreal halfWidth;
};

// Pressure width at a fractional sample index (linear across each gap).
qreal widthAtT(const Stroke &stroke, qreal t)
{
    const int n = stroke.samples.size();
    t = qBound(0.0, t, qreal(n - 1));
    const int i = int(std::floor(t));
    const int j = qMin(i + 1, n - 1);
    const qreal f = t - i;
    return stroke.widthAt(i) * (1.0 - f) + stroke.widthAt(j) * f;
}

// Flattens the midpoint-quadratic centreline into <= kMaxStep polyline
// points. `t` runs 0 .. n-1 in sample space: the quadratic over sample i
// spans [i-0.5, i+0.5], the final straight segment [n-1.5, n-1].
QVector<WalkPoint> walkCentreline(const Stroke &stroke)
{
    const int n = stroke.samples.size();
    const bool pen = stroke.tool != StrokeTool::Highlighter;
    const auto halfAt = [&stroke, pen](qreal t) {
        // The highlighter is constant width: pressure is a pen term.
        return (pen ? widthAtT(stroke, t) : stroke.width) / 2.0;
    };
    QVector<WalkPoint> out;
    out.reserve(n * 4);
    out.append({stroke.samples.first().pos, halfAt(0.0)});
    // `start` by value: it usually points into `out`, which the appends
    // below can reallocate — a reference would dangle mid-segment.
    const auto appendSegment = [&out, halfAt](const QPointF start, const QPointF &control,
                                              const QPointF &target, qreal t0, qreal t1,
                                              bool quadratic) {
        const qreal approxLength = quadratic
            ? QLineF(start, control).length() + QLineF(control, target).length()
            : QLineF(start, target).length();
        const int steps = qBound(1, int(std::ceil(approxLength / kMaxStep)), 64);
        for (int s = 1; s <= steps; ++s) {
            const qreal u = qreal(s) / steps;
            const QPointF pos = quadratic
                ? (1.0 - u) * (1.0 - u) * start + 2.0 * u * (1.0 - u) * control + u * u * target
                : start + u * (target - start);
            out.append({pos, halfAt(t0 + u * (t1 - t0))});
        }
    };
    if (n == 2) {
        appendSegment(stroke.samples.first().pos, {}, stroke.samples.last().pos, 0.0, 1.0,
                      /*quadratic=*/false);
        return out;
    }
    for (int i = 1; i < n - 1; ++i) {
        const QPointF control = stroke.samples.at(i).pos;
        const QPointF target = (control + stroke.samples.at(i + 1).pos) / 2.0;
        appendSegment(out.constLast().pos, control, target, i - 0.5, i + 0.5,
                      /*quadratic=*/true);
    }
    appendSegment(out.constLast().pos, {}, stroke.samples.last().pos, n - 1.5, n - 1.0,
                  /*quadratic=*/false);
    return out;
}

// Outward-bulging semicircle cap from `from` to `to` around `center`;
// `outward` picks which of the two possible arcs is the cap (the one
// passing through the outward tangent direction).
void appendCap(QPainterPath *path, const QPointF &center, const QPointF &from,
               const QPointF &to, const QPointF &outward)
{
    Q_UNUSED(to);
    const qreal radius = QLineF(center, from).length();
    if (radius <= 0.0) {
        return;
    }
    const qreal a0 = std::atan2(from.y() - center.y(), from.x() - center.x());
    const auto norm = [](qreal angle) {
        while (angle > M_PI) {
            angle -= 2.0 * M_PI;
        }
        while (angle <= -M_PI) {
            angle += 2.0 * M_PI;
        }
        return angle;
    };
    // `from` sits at the end normal; the cap sweeps twice the angle from
    // the normal to the outward tangent, landing exactly on -normal = `to`.
    const qreal sweep = 2.0 * norm(std::atan2(outward.y(), outward.x()) - a0);
    for (int s = 1; s <= kCapSegments; ++s) {
        const qreal angle = a0 + sweep * qreal(s) / kCapSegments;
        path->lineTo(center + radius * QPointF(std::cos(angle), std::sin(angle)));
    }
}

} // namespace

QPainterPath centreline(const Stroke &stroke)
{
    QPainterPath path;
    const int n = stroke.samples.size();
    if (n == 0) {
        return path;
    }
    path.moveTo(stroke.samples.first().pos);
    if (n == 2) {
        path.lineTo(stroke.samples.last().pos);
        return path;
    }
    for (int i = 1; i < n - 1; ++i) {
        const QPointF control = stroke.samples.at(i).pos;
        path.quadTo(control, (control + stroke.samples.at(i + 1).pos) / 2.0);
    }
    path.lineTo(stroke.samples.last().pos);
    return path;
}

QPainterPath outline(const Stroke &stroke)
{
    QPainterPath path;
    if (stroke.samples.size() < 2) {
        return path;
    }
    const QVector<WalkPoint> walk = walkCentreline(stroke);
    const int count = walk.size();
    const bool pen = stroke.tool != StrokeTool::Highlighter;

    QVector<QPointF> left(count), right(count), tangents(count);
    for (int i = 0; i < count; ++i) {
        const QPointF before = walk.at(qMax(0, i - 1)).pos;
        const QPointF after = walk.at(qMin(count - 1, i + 1)).pos;
        QPointF dir = after - before;
        if (QLineF(QPointF(), dir).length() < 1e-6) {
            // Duplicate neighbours (a slow pen resampled densely): keep the
            // previous tangent rather than producing a zero normal.
            dir = i > 0 ? tangents.at(i - 1) : QPointF(1, 0);
        }
        const qreal length = QLineF(QPointF(), dir).length();
        tangents[i] = dir / length;
    }
    for (int i = 0; i < count; ++i) {
        const QPointF normal(-tangents.at(i).y(), tangents.at(i).x());
        left[i] = walk.at(i).pos + normal * walk.at(i).halfWidth;
        right[i] = walk.at(i).pos - normal * walk.at(i).halfWidth;
    }

    path.moveTo(left.constFirst());
    for (int i = 1; i < count; ++i) {
        path.lineTo(left.at(i));
    }
    if (pen) {
        appendCap(&path, walk.constLast().pos, left.constLast(), right.constLast(),
                  tangents.constLast());
    } else {
        path.lineTo(right.constLast());
    }
    for (int i = count - 2; i >= 0; --i) {
        path.lineTo(right.at(i));
    }
    if (pen) {
        appendCap(&path, walk.constFirst().pos, right.constFirst(), left.constFirst(),
                  -tangents.constFirst());
    } else {
        path.lineTo(left.constFirst());
    }
    path.closeSubpath();
    return path;
}

} // namespace QindaOffice::Ink
