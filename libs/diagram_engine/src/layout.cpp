// SPDX-License-Identifier: GPL-3.0-or-later
#include "layout.h"
#include "layout_p.h"

#include "geometry.h"
#include "stencils.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <functional>
#include <numeric>

namespace QindaOffice::Diagram {

namespace LayoutDetail {

Graph buildGraph(const Page &page, const QStringList &ids)
{
    Graph g;
    for (int i = 0; i < page.shapes.size(); ++i) {
        const Shape &shape = page.shapes.at(i);
        if (isContainer(shape) || shape.locked) {
            continue;
        }
        if (ids.isEmpty() || ids.contains(shape.id)) {
            g.slot.insert(i, int(g.nodes.size()));
            g.nodes.append(i);
        }
    }
    g.out.resize(g.nodes.size());
    g.in.resize(g.nodes.size());
    for (int c = 0; c < page.connectors.size(); ++c) {
        const Connector &connector = page.connectors.at(c);
        if (!connector.from.glued() || !connector.to.glued()) {
            continue;
        }
        const int a = g.slot.value(page.shapeIndex(connector.from.shapeId), -1);
        const int b = g.slot.value(page.shapeIndex(connector.to.shapeId), -1);
        if (a < 0 || b < 0 || a == b) {
            continue;
        }
        g.edges.append({a, b});
        g.edgeConnectors.append(c);
    }
    return g;
}

} // namespace LayoutDetail

namespace {

using namespace LayoutDetail;

// Reverses DFS back edges so the graph is acyclic (edges are only reversed
// for ranking; the connectors keep their direction).
void breakCycles(Graph &g)
{
    const int n = int(g.nodes.size());
    QVector<int> state(n, 0); // 0 new, 1 on stack, 2 done
    QVector<QVector<int>> adjacency(n);
    for (int e = 0; e < g.edges.size(); ++e) {
        adjacency[g.edges.at(e).first].append(e);
    }
    std::function<void(int)> visit = [&](int v) {
        state[v] = 1;
        for (int e : std::as_const(adjacency[v])) {
            const int w = g.edges.at(e).second;
            if (state[w] == 1) {
                std::swap(g.edges[e].first, g.edges[e].second);
            } else if (state[w] == 0) {
                visit(w);
            }
        }
        state[v] = 2;
    };
    for (int v = 0; v < n; ++v) {
        if (state[v] == 0) {
            visit(v);
        }
    }
    for (auto &list : g.out) list.clear();
    for (auto &list : g.in) list.clear();
    for (const auto &edge : std::as_const(g.edges)) {
        g.out[edge.first].append(edge.second);
        g.in[edge.second].append(edge.first);
    }
}

QVector<int> longestPathRanks(const Graph &g)
{
    const int n = int(g.nodes.size());
    QVector<int> rank(n, 0);
    QVector<int> indegree(n, 0);
    for (int v = 0; v < n; ++v) indegree[v] = int(g.in.at(v).size());
    QVector<int> queue;
    for (int v = 0; v < n; ++v) if (indegree[v] == 0) queue.append(v);
    for (int head = 0; head < queue.size(); ++head) {
        const int v = queue.at(head);
        for (int w : g.out.at(v)) {
            rank[w] = std::max(rank[w], rank[v] + 1);
            if (--indegree[w] == 0) queue.append(w);
        }
    }
    return rank;
}

} // namespace

namespace LayoutDetail {

void reglue(Page &page, const Graph &g, bool vertical, const QSet<int> &hanging)
{
    for (int e = 0; e < g.edgeConnectors.size(); ++e) {
        Connector &connector = page.connectors[g.edgeConnectors.at(e)];
        const Shape *from = page.shape(connector.from.shapeId);
        const Shape *to = page.shape(connector.to.shapeId);
        if (from == nullptr || to == nullptr || portCount(*from) < 4 || portCount(*to) < 4) {
            continue;
        }
        // Ports 0..3 are top, right, bottom, left (stencils.cpp sides()).
        const bool forward = vertical ? from->rect.center().y() <= to->rect.center().y()
                                      : from->rect.center().x() <= to->rect.center().x();
        const int out = vertical ? (forward ? 2 : 0) : (forward ? 1 : 3);
        const int in = vertical ? (forward ? 0 : 2) : (forward ? 3 : 1);
        connector.from.port = out;
        connector.to.port = hanging.contains(g.slot.value(page.shapeIndex(to->id), -1)) ? 3 : in;
    }
}

QRectF currentBounds(const Page &page, const Graph &g)
{
    QRectF bounds;
    for (int index : g.nodes) {
        const QRectF r = page.shapes.at(index).rect;
        bounds = bounds.isNull() ? r : bounds | r;
    }
    return bounds;
}

} // namespace LayoutDetail

namespace {

void layered(Page &page, Graph &g, const LayoutOptions &o)
{
    const bool vertical = o.kind == LayoutKind::TopDown;
    breakCycles(g);
    const QVector<int> rank = longestPathRanks(g);
    const int ranks = rank.isEmpty() ? 0 : *std::max_element(rank.begin(), rank.end()) + 1;
    const QRectF origin = currentBounds(page, g);
    const auto along = [&](int v) { const QRectF r = page.shapes.at(g.nodes.at(v)).rect; return vertical ? r.width() : r.height(); };
    const auto across = [&](int v) { const QRectF r = page.shapes.at(g.nodes.at(v)).rect; return vertical ? r.height() : r.width(); };
    // Initial order: where the shapes are now.
    QVector<QVector<int>> layers(ranks);
    for (int v = 0; v < rank.size(); ++v) layers[rank.at(v)].append(v);
    for (auto &layer : layers) {
        std::stable_sort(layer.begin(), layer.end(), [&](int a, int b) {
            const QPointF pa = page.shapes.at(g.nodes.at(a)).rect.center();
            const QPointF pb = page.shapes.at(g.nodes.at(b)).rect.center();
            return vertical ? pa.x() < pb.x() : pa.y() < pb.y();
        });
    }
    // Barycentre sweeps (down then up) on positions within the layer.
    QVector<double> position(rank.size(), 0);
    const auto indexPositions = [&] { for (const auto &layer : layers) for (int i = 0; i < layer.size(); ++i) position[layer.at(i)] = i; };
    indexPositions();
    for (int sweep = 0; sweep < 4; ++sweep) {
        const bool down = sweep % 2 == 0;
        for (int r = down ? 1 : ranks - 2; down ? r < ranks : r >= 0; r += down ? 1 : -1) {
            QVector<int> &layer = layers[r];
            QHash<int, double> key;
            for (int v : std::as_const(layer)) {
                const QVector<int> &neighbours = down ? g.in.at(v) : g.out.at(v);
                double sum = 0;
                int count = 0;
                for (int w : neighbours) {
                    if (rank.at(w) == r + (down ? -1 : 1)) { sum += position.at(w); ++count; }
                }
                key.insert(v, count > 0 ? sum / count : position.at(v));
            }
            std::stable_sort(layer.begin(), layer.end(), [&](int a, int b) { return key.value(a) < key.value(b); });
            for (int i = 0; i < layer.size(); ++i) position[layer.at(i)] = i;
        }
    }
    // Coordinates: ranks stacked along the flow, members packed across it,
    // each pulled towards its predecessors' centre.
    QVector<double> centre(rank.size(), 0);
    double flow = vertical ? origin.top() : origin.left();
    double widest = 0;
    QVector<double> rankStart(ranks);
    QVector<double> rankDepth(ranks, 0);
    for (int r = 0; r < ranks; ++r) {
        for (int v : layers.at(r)) rankDepth[r] = std::max(rankDepth[r], across(v));
        double width = -o.nodeGap;
        for (int v : layers.at(r)) width += along(v) + o.nodeGap;
        widest = std::max(widest, width);
        rankStart[r] = flow;
        flow += rankDepth[r] + o.rankGap;
    }
    const double axis = (vertical ? origin.left() : origin.top()) + widest / 2;
    for (int r = 0; r < ranks; ++r) {
        const QVector<int> &layer = layers.at(r);
        QVector<double> desired;
        double cursor = axis - [&] { double w = -o.nodeGap; for (int v : layer) w += along(v) + o.nodeGap; return w; }() / 2;
        for (int v : layer) {
            double want = cursor + along(v) / 2;
            double sum = 0;
            int count = 0;
            for (int w : g.in.at(v)) if (rank.at(w) < r) { sum += centre.at(w); ++count; }
            if (count > 0) want = sum / count;
            desired.append(want);
            cursor += along(v) + o.nodeGap;
        }
        // Resolve overlaps left to right, then shift the rank so it sits on
        // average where it wanted to be.
        QVector<double> placed(layer.size());
        double right = -1e18;
        for (int i = 0; i < layer.size(); ++i) {
            const double half = along(layer.at(i)) / 2;
            placed[i] = std::max(desired.at(i), right + o.nodeGap + half);
            right = placed[i] + half;
        }
        double drift = 0;
        for (int i = 0; i < layer.size(); ++i) drift += desired.at(i) - placed.at(i);
        drift /= std::max<double>(1, layer.size());
        for (int i = 0; i < layer.size(); ++i) centre[layer.at(i)] = placed.at(i) + drift;
    }
    // Keep the left/top edge where the selection was.
    double minEdge = 1e18;
    for (int v = 0; v < rank.size(); ++v) minEdge = std::min(minEdge, centre.at(v) - along(v) / 2);
    const double shift = (vertical ? origin.left() : origin.top()) - minEdge;
    for (int v = 0; v < rank.size(); ++v) {
        QRectF &rect = page.shapes[g.nodes.at(v)].rect;
        const int r = rank.at(v);
        const double mid = rankStart.at(r) + rankDepth.at(r) / 2;
        const QPointF c = vertical ? QPointF(centre.at(v) + shift, mid) : QPointF(mid, centre.at(v) + shift);
        rect.moveCenter(c);
    }
    reglue(page, g, vertical);
}

void tree(Page &page, Graph &g, const LayoutOptions &o)
{
    const int n = int(g.nodes.size());
    QVector<int> parent(n, -1);
    QVector<QVector<int>> children(n);
    for (const auto &edge : std::as_const(g.edges)) {
        if (parent[edge.second] < 0 && edge.first != edge.second) {
            // Refuse an edge that would close a cycle.
            int up = edge.first;
            bool cycle = false;
            while (up >= 0 && !cycle) { cycle = up == edge.second; up = parent[up]; }
            if (!cycle) { parent[edge.second] = edge.first; children[edge.first].append(edge.second); }
        }
    }
    const auto rect = [&](int v) -> QRectF & { return page.shapes[g.nodes.at(v)].rect; };
    for (auto &list : children) {
        std::stable_sort(list.begin(), list.end(), [&](int a, int b) { return rect(a).center().x() < rect(b).center().x(); });
    }
    constexpr double trunk = 26; // hanging column offset from the manager's centre
    QSet<int> hanging;
    const auto hangs = [&](int v) {
        const QVector<int> &kids = children.at(v);
        return kids.size() >= 4 && std::all_of(kids.begin(), kids.end(), [&](int k) { return children.at(k).isEmpty(); });
    };
    QVector<double> width(n, 0);
    std::function<double(int)> measure = [&](int v) {
        if (hangs(v)) {
            double widestKid = 0;
            for (int k : children.at(v)) widestKid = std::max(widestKid, rect(k).width());
            width[v] = std::max(rect(v).width(), rect(v).width() / 2 + trunk + widestKid);
            return width[v];
        }
        double sum = -o.nodeGap;
        for (int k : children.at(v)) sum += measure(k) + o.nodeGap;
        width[v] = std::max(rect(v).width(), children.at(v).isEmpty() ? 0.0 : sum);
        return width[v];
    };
    QVector<int> roots;
    for (int v = 0; v < n; ++v) if (parent[v] < 0) roots.append(v);
    std::stable_sort(roots.begin(), roots.end(), [&](int a, int b) { return rect(a).center().x() < rect(b).center().x(); });
    const QRectF origin = currentBounds(page, g);
    // Level heights: the tallest shape per depth.
    QVector<double> levelHeight;
    std::function<void(int, int)> depthOf = [&](int v, int d) {
        if (levelHeight.size() <= d) levelHeight.resize(d + 1);
        levelHeight[d] = std::max(levelHeight[d], rect(v).height());
        if (!hangs(v)) for (int k : children.at(v)) depthOf(k, d + 1);
    };
    for (int r : roots) depthOf(r, 0);
    QVector<double> levelTop(levelHeight.size());
    double y = origin.top();
    for (int d = 0; d < levelHeight.size(); ++d) { levelTop[d] = y; y += levelHeight.at(d) + o.rankGap; }
    std::function<void(int, int, double)> place = [&](int v, int d, double left) {
        QRectF &r = rect(v);
        if (hangs(v)) {
            // The block is exactly as wide as manager + trunk + column
            // (measure), so the manager starts at the block's left edge.
            r.moveTopLeft(QPointF(left, levelTop.at(d) + (levelHeight.at(d) - r.height()) / 2));
            double ky = r.bottom() + o.rankGap * 0.5;
            for (int k : children.at(v)) {
                QRectF &kr = rect(k);
                kr.moveTopLeft(QPointF(r.center().x() + trunk, ky));
                ky += kr.height() + o.nodeGap * 0.45;
                hanging.insert(k);
            }
            return;
        }
        double childrenWidth = -o.nodeGap;
        for (int k : children.at(v)) childrenWidth += width.at(k) + o.nodeGap;
        const double centreX = left + width.at(v) / 2;
        r.moveTopLeft(QPointF(centreX - r.width() / 2, levelTop.at(d) + (levelHeight.at(d) - r.height()) / 2));
        double cursor = left + (width.at(v) - std::max(0.0, childrenWidth)) / 2;
        for (int k : children.at(v)) {
            place(k, d + 1, cursor);
            cursor += width.at(k) + o.nodeGap;
        }
    };
    double left = origin.left();
    for (int root : roots) {
        measure(root);
        place(root, 0, left);
        left += width.at(root) + o.nodeGap * 2;
    }
    reglue(page, g, true, hanging);
}

} // namespace

bool autoLayout(Page &page, const QStringList &ids, const LayoutOptions &options, QString *error)
{
    Graph g = buildGraph(page, ids);
    if (g.nodes.size() < 2) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("DiagramLayout", "Select two or more shapes to arrange, or none to arrange the page.");
        }
        return false;
    }
    LayoutOptions o = options;
    switch (options.kind) {
    case LayoutKind::Tree:
        tree(page, g, o);
        break;
    case LayoutKind::TopDown:
    case LayoutKind::LeftRight:
        layered(page, g, o);
        break;
    case LayoutKind::BottomUp:
    case LayoutKind::RightLeft: {
        // M87: the layered layout, mirrored along the flow.
        const bool vertical = options.kind == LayoutKind::BottomUp;
        o.kind = vertical ? LayoutKind::TopDown : LayoutKind::LeftRight;
        layered(page, g, o);
        mirror(page, g, vertical);
        reglue(page, g, vertical);
        break;
    }
    case LayoutKind::CompactTree:
        compactTree(page, g, o.rankGap, o.nodeGap);
        break;
    case LayoutKind::Radial:
        radial(page, g, o.rankGap, o.nodeGap);
        break;
    case LayoutKind::Circular:
        circular(page, g, o.nodeGap);
        break;
    }
    return true;
}

QString layoutKindName(LayoutKind kind)
{
    switch (kind) {
    case LayoutKind::TopDown: return QStringLiteral("topDown");
    case LayoutKind::LeftRight: return QStringLiteral("leftRight");
    case LayoutKind::Tree: return QStringLiteral("tree");
    case LayoutKind::BottomUp: return QStringLiteral("bottomUp");
    case LayoutKind::RightLeft: return QStringLiteral("rightLeft");
    case LayoutKind::CompactTree: return QStringLiteral("compactTree");
    case LayoutKind::Radial: return QStringLiteral("radial");
    case LayoutKind::Circular: return QStringLiteral("circular");
    }
    return QStringLiteral("topDown");
}

LayoutKind layoutKindOf(const QString &name, LayoutKind fallback)
{
    for (const LayoutKind kind : {LayoutKind::TopDown, LayoutKind::LeftRight, LayoutKind::Tree, LayoutKind::BottomUp,
                                  LayoutKind::RightLeft, LayoutKind::CompactTree, LayoutKind::Radial,
                                  LayoutKind::Circular}) {
        if (layoutKindName(kind) == name) {
            return kind;
        }
    }
    return fallback;
}

} // namespace QindaOffice::Diagram
