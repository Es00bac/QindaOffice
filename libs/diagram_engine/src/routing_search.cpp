// SPDX-License-Identifier: GPL-3.0-or-later
// Right-angle routing round every shape (M87, D-095; routing_p.h): a
// cheapest-path search on the orthogonal visibility grid, for the routes
// the quick L/Z/U candidates of routing.cpp cannot keep clear.
#include "routing_p.h"

#include <QLineF>

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <vector>

namespace QindaOffice::Diagram::RoutingDetail {

namespace {

constexpr double kEps = 1e-6;

// 0 right, 1 down, 2 left, 3 up.
int directionOf(QPointF v)
{
    if (std::abs(v.x()) >= std::abs(v.y())) {
        return v.x() >= 0 ? 0 : 2;
    }
    return v.y() >= 0 ? 1 : 3;
}

QVector<double> coordinates(QVector<double> values)
{
    std::sort(values.begin(), values.end());
    QVector<double> out;
    for (const double v : std::as_const(values)) {
        if (out.isEmpty() || v - out.last() > kEps) {
            out.append(v);
        }
    }
    return out;
}

int indexOf(const QVector<double> &axis, double value)
{
    const auto it = std::lower_bound(axis.cbegin(), axis.cend(), value - kEps);
    return it != axis.cend() && std::abs(*it - value) <= kEps ? int(it - axis.cbegin()) : -1;
}

} // namespace

bool segmentHitsRect(QPointF a, QPointF b, const QRectF &r)
{
    double t0 = 0;
    double t1 = 1;
    const double dx = b.x() - a.x();
    const double dy = b.y() - a.y();
    const double p[] = {-dx, dx, -dy, dy};
    const double q[] = {a.x() - r.left(), r.right() - a.x(), a.y() - r.top(), r.bottom() - a.y()};
    for (int i = 0; i < 4; ++i) {
        if (std::abs(p[i]) < 1e-12) {
            if (q[i] <= 0) {
                return false;
            }
        } else {
            const double t = q[i] / p[i];
            if (p[i] < 0) {
                t0 = std::max(t0, t);
            } else {
                t1 = std::min(t1, t);
            }
            if (t0 >= t1) {
                return false;
            }
        }
    }
    return t1 - t0 > 1e-9;
}

namespace {

QVector<QPointF> search(QPointF a1, QPointF da, QPointF b1, QPointF db, const QVector<QRectF> &obstacles,
                        double clearance, double bendCost, bool gridFromAll)
{
    // Grid lines come from the shapes near the two ends (every shape is
    // still an obstacle); a second pass takes them all.
    const QRectF ends = QRectF(a1, b1).normalized();
    const double reach = std::max(240.0, std::max(ends.width(), ends.height()));
    const QRectF nearby = ends.adjusted(-reach, -reach, reach, reach);
    QVector<double> xv{a1.x(), b1.x()};
    QVector<double> yv{a1.y(), b1.y()};
    for (const QRectF &r : obstacles) {
        if (gridFromAll || r.intersects(nearby)) {
            xv << r.left() - clearance << r.right() + clearance;
            yv << r.top() - clearance << r.bottom() + clearance;
        }
    }
    const QVector<double> xs = coordinates(xv);
    const QVector<double> ys = coordinates(yv);
    const int nx = int(xs.size());
    const int ny = int(ys.size());
    const auto point = [&](int node) { return QPointF(xs.at(node / ny), ys.at(node % ny)); };
    const auto inside = [&](QPointF p) {
        return std::any_of(obstacles.cbegin(), obstacles.cend(), [&](const QRectF &r) {
            return p.x() > r.left() + kEps && p.x() < r.right() - kEps && p.y() > r.top() + kEps && p.y() < r.bottom() - kEps;
        });
    };
    const auto clear = [&](QPointF p, QPointF q) {
        return std::none_of(obstacles.cbegin(), obstacles.cend(), [&](const QRectF &r) { return segmentHitsRect(p, q, r); });
    };
    const int start = indexOf(xs, a1.x()) * ny + indexOf(ys, a1.y());
    const int goal = indexOf(xs, b1.x()) * ny + indexOf(ys, b1.y());
    if (start < 0 || goal < 0) {
        return {};
    }
    const int states = nx * ny * 4;
    std::vector<double> best(size_t(states), std::numeric_limits<double>::infinity());
    std::vector<int> previous(size_t(states), -1);
    using Entry = std::pair<double, int>; // (cost, state = node * 4 + direction)
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> queue;
    const int first = start * 4 + directionOf(da);
    best[size_t(first)] = 0;
    queue.emplace(0.0, first);
    const int arrive = (directionOf(db) + 2) % 4; // the last stub runs against db
    int reached = -1;
    while (!queue.empty()) {
        const auto [cost, state] = queue.top();
        queue.pop();
        if (cost > best[size_t(state)]) {
            continue;
        }
        const int node = state / 4;
        const int dir = state % 4;
        if (node == goal && dir != (arrive + 2) % 4) {
            reached = state;
            break;
        }
        const int i = node / ny;
        const int j = node % ny;
        static const int di[] = {1, 0, -1, 0};
        static const int dj[] = {0, 1, 0, -1};
        for (int d = 0; d < 4; ++d) {
            if (d == (dir + 2) % 4) {
                continue; // never double back
            }
            const int ni = i + di[d];
            const int nj = j + dj[d];
            if (ni < 0 || nj < 0 || ni >= nx || nj >= ny) {
                continue;
            }
            const int next = ni * ny + nj;
            const QPointF p = point(node);
            const QPointF q = point(next);
            if (inside(q) || !clear(p, q)) {
                continue;
            }
            double step = QLineF(p, q).length() + (d != dir ? bendCost : 0);
            if (next == goal && d != arrive) {
                step += bendCost; // one more bend into the last stub
            }
            const int nextState = next * 4 + d;
            if (cost + step < best[size_t(nextState)]) {
                best[size_t(nextState)] = cost + step;
                previous[size_t(nextState)] = state;
                queue.emplace(cost + step, nextState);
            }
        }
    }
    if (reached < 0) {
        return {};
    }
    QVector<QPointF> path;
    for (int s = reached; s >= 0; s = previous[size_t(s)]) {
        const QPointF p = point(s / 4);
        if (path.isEmpty() || QLineF(path.first(), p).length() > kEps) {
            path.prepend(p);
        }
    }
    return path;
}

} // namespace

QVector<QPointF> searchOrthogonal(QPointF a1, QPointF da, QPointF b1, QPointF db, const QVector<QRectF> &obstacles,
                                  double clearance, double bendCost)
{
    QVector<QPointF> path = search(a1, da, b1, db, obstacles, clearance, bendCost, false);
    if (path.isEmpty()) {
        path = search(a1, da, b1, db, obstacles, clearance, bendCost, true);
    }
    return path;
}

} // namespace QindaOffice::Diagram::RoutingDetail
