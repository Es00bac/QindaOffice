// SPDX-License-Identifier: GPL-3.0-or-later
// Auto-layout styles of M87 (D-095; layout.h): Radial, Circular, Compact
// Tree and the mirror that turns flowcharts bottom-to-top / right-to-left.
// Every style keeps the top-left corner of what it arranges where it was,
// as the M50 styles do, so a selection re-arranges in place.
#include "layout.h"
#include "layout_p.h"

#include "geometry.h"
#include "stencils.h"

#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>
#include <numeric>

namespace QindaOffice::Diagram::LayoutDetail {

namespace {

constexpr double kPi = std::numbers::pi;

// A spanning forest of the graph: each node under the source of its first
// incoming edge that does not close a cycle; roots in left-to-right order.
struct Forest {
    QVector<int> parent;
    QVector<QVector<int>> children;
    QVector<int> roots;
};

Forest forestOf(const Page &page, const Graph &g)
{
    const int n = int(g.nodes.size());
    Forest f;
    f.parent.fill(-1, n);
    f.children.resize(n);
    for (const auto &edge : g.edges) {
        const int a = edge.first;
        const int b = edge.second;
        if (a == b || f.parent[b] >= 0) {
            continue;
        }
        bool cycle = false;
        for (int up = a; up >= 0 && !cycle; up = f.parent[up]) {
            cycle = up == b;
        }
        if (!cycle) {
            f.parent[b] = a;
            f.children[a].append(b);
        }
    }
    const auto centreX = [&](int v) { return page.shapes.at(g.nodes.at(v)).rect.center().x(); };
    for (int v = 0; v < n; ++v) {
        std::stable_sort(f.children[v].begin(), f.children[v].end(), [&](int a, int b) { return centreX(a) < centreX(b); });
        if (f.parent[v] < 0) {
            f.roots.append(v);
        }
    }
    std::stable_sort(f.roots.begin(), f.roots.end(), [&](int a, int b) { return centreX(a) < centreX(b); });
    return f;
}

QRectF &rectOf(Page &page, const Graph &g, int v)
{
    return page.shapes[g.nodes.at(v)].rect;
}

// Shifts every node so the laid-out bounds start at `origin`.
void anchor(Page &page, const Graph &g, QPointF origin)
{
    const QRectF bounds = currentBounds(page, g);
    const QPointF shift = origin - bounds.topLeft();
    for (int v = 0; v < g.nodes.size(); ++v) {
        rectOf(page, g, v).translate(shift);
    }
}

// Radial and circular layouts draw straight lines between outlines.
void straighten(Page &page, const Graph &g)
{
    for (const int c : g.edgeConnectors) {
        Connector &connector = page.connectors[c];
        connector.from.port = -1;
        connector.to.port = -1;
        connector.routing = Routing::Straight;
    }
}

double diameterOf(const QRectF &r)
{
    return std::hypot(r.width(), r.height());
}

} // namespace

void mirror(Page &page, const Graph &g, bool vertical)
{
    const QRectF bounds = currentBounds(page, g);
    for (int v = 0; v < g.nodes.size(); ++v) {
        QRectF &r = rectOf(page, g, v);
        QPointF c = r.center();
        if (vertical) {
            c.setY(bounds.top() + bounds.bottom() - c.y());
        } else {
            c.setX(bounds.left() + bounds.right() - c.x());
        }
        r.moveCenter(c);
    }
}

void radial(Page &page, Graph &g, double rankGap, double nodeGap)
{
    const QPointF origin = currentBounds(page, g).topLeft();
    const Forest f = forestOf(page, g);
    const int n = int(g.nodes.size());
    QVector<int> leaves(n, 1);
    QVector<int> depth(n, 0);
    int maxDepth = 0;
    double diameter = 0;
    std::function<int(int, int)> count = [&](int v, int d) {
        depth[v] = d;
        maxDepth = std::max(maxDepth, d);
        diameter = std::max(diameter, diameterOf(rectOf(page, g, v)));
        int sum = 0;
        for (const int k : f.children.at(v)) {
            sum += count(k, d + 1);
        }
        leaves[v] = std::max(1, sum);
        return leaves[v];
    };
    // Trees side by side, each with its root in the middle of its rings.
    double left = 0;
    for (const int root : f.roots) {
        maxDepth = 0;
        count(root, 0);
        // Rings far enough apart for the shapes, and the outer ring long
        // enough for every leaf with a gap between neighbours.
        const double outer = leaves.at(root) * (diameter + nodeGap) / (2 * kPi);
        const double ring = std::max(rankGap + diameter, maxDepth > 0 ? outer / maxDepth : 0.0);
        const double extent = maxDepth * ring + diameter / 2;
        const QPointF centre(left + extent, extent);
        std::function<void(int, double, double)> place = [&](int v, double from, double to) {
            const double angle = (from + to) / 2 - kPi / 2; // the first wedge starts at the top
            const double radius = depth.at(v) * ring;
            rectOf(page, g, v).moveCenter(centre + QPointF(std::cos(angle), std::sin(angle)) * radius);
            double cursor = from;
            for (const int k : f.children.at(v)) {
                const double span = (to - from) * leaves.at(k) / double(leaves.at(v));
                place(k, cursor, cursor + span);
                cursor += span;
            }
        };
        place(root, 0, 2 * kPi);
        left += extent * 2 + nodeGap * 2;
    }
    anchor(page, g, origin);
    straighten(page, g);
}

void circular(Page &page, Graph &g, double nodeGap)
{
    const QPointF origin = currentBounds(page, g).topLeft();
    const int n = int(g.nodes.size());
    // Neighbours stay neighbours: a depth-first walk over the undirected
    // graph, each component from its best-connected shape.
    QVector<QVector<int>> adjacent(n);
    for (const auto &edge : g.edges) {
        adjacent[edge.first].append(edge.second);
        adjacent[edge.second].append(edge.first);
    }
    QVector<int> order;
    QVector<bool> seen(n, false);
    QVector<int> starts(n);
    std::iota(starts.begin(), starts.end(), 0);
    std::stable_sort(starts.begin(), starts.end(), [&](int a, int b) { return adjacent.at(a).size() > adjacent.at(b).size(); });
    std::function<void(int)> walk = [&](int v) {
        seen[v] = true;
        order.append(v);
        for (const int w : adjacent.at(v)) {
            if (!seen.at(w)) {
                walk(w);
            }
        }
    };
    for (const int v : std::as_const(starts)) {
        if (!seen.at(v)) {
            walk(v);
        }
    }
    double circumference = 0;
    double widest = 0;
    for (const int v : std::as_const(order)) {
        const double d = diameterOf(rectOf(page, g, v));
        circumference += d + nodeGap;
        widest = std::max(widest, d);
    }
    const double radius = std::max(circumference / (2 * kPi), widest);
    double walked = 0;
    for (const int v : std::as_const(order)) {
        const double d = diameterOf(rectOf(page, g, v)) + nodeGap;
        const double angle = (walked + d / 2) / radius - kPi / 2;
        rectOf(page, g, v).moveCenter(QPointF(std::cos(angle), std::sin(angle)) * radius);
        walked += d;
    }
    anchor(page, g, origin);
    straighten(page, g);
}

void compactTree(Page &page, Graph &g, double rankGap, double nodeGap)
{
    const QPointF origin = currentBounds(page, g).topLeft();
    const Forest f = forestOf(page, g);
    // Each child sits under its parent, indented, its own subtree below it
    // before the next sibling (a file tree, Visio's "down then right").
    const double step = std::max(nodeGap * 0.5, 12.0);
    double right = origin.x();
    std::function<double(int, double, double)> place = [&](int v, double x, double y) {
        QRectF &r = rectOf(page, g, v);
        r.moveTopLeft(QPointF(x, y));
        right = std::max(right, r.right());
        const double indent = std::max(24.0, r.width() * 0.25);
        double cursor = r.bottom() + rankGap * 0.5;
        for (const int k : f.children.at(v)) {
            cursor = place(k, x + indent, cursor) + step;
        }
        return f.children.at(v).isEmpty() ? r.bottom() : cursor - step;
    };
    double x = origin.x();
    for (const int root : f.roots) {
        place(root, x, origin.y());
        x = right + nodeGap * 2;
    }
    // Bottom of the parent into the left side of the child.
    for (int e = 0; e < g.edges.size(); ++e) {
        Connector &connector = page.connectors[g.edgeConnectors.at(e)];
        const int a = g.edges.at(e).first;
        const int b = g.edges.at(e).second;
        if (f.parent.at(b) != a) {
            continue;
        }
        const Shape &from = page.shapes.at(g.nodes.at(a));
        const Shape &to = page.shapes.at(g.nodes.at(b));
        if (portCount(from) >= 4 && portCount(to) >= 4) {
            connector.from.port = 2;
            connector.to.port = 3;
        }
        connector.routing = Routing::Orthogonal;
    }
}

} // namespace QindaOffice::Diagram::LayoutDetail
