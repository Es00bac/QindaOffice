// SPDX-License-Identifier: GPL-3.0-or-later
// The Network Diagram's layout (network_layout.h has the rules).
#include "network_layout.h"

#include <QSet>

#include <algorithm>
#include <climits>
#include <cmath>

namespace QindaOffice::Plan {

namespace {

struct Graph {
    QList<int> tasks;             // node → task index
    QList<QList<int>> before;     // layering predecessors (links and summary → subtask)
    QList<QList<std::pair<int, int>>> links; // drawn: (predecessor node, link index) per node
};

Graph buildGraph(const Project &p, const QList<int> &rows, const NetworkOptions &o, QHash<int, int> &nodeOfTask)
{
    Graph g;
    for (int i : rows) {
        if (i < 0 || i >= p.tasks.size() || nodeOfTask.contains(i) || (!o.summaries && p.tasks.at(i).summary)) {
            continue;
        }
        nodeOfTask.insert(i, int(g.tasks.size()));
        g.tasks.append(i);
    }
    QHash<int, int> indexOfUid;
    for (int i = 0; i < p.tasks.size(); ++i) {
        indexOfUid.insert(p.tasks.at(i).uid, i);
    }
    const int n = int(g.tasks.size());
    g.before = QList<QList<int>>(n);
    g.links = QList<QList<std::pair<int, int>>>(n);
    for (int v = 0; v < n; ++v) {
        const Task &t = p.tasks.at(g.tasks.at(v));
        for (int l = 0; l < t.predecessors.size(); ++l) {
            if (t.ignoredLinks.contains(l)) {
                continue;
            }
            const int u = nodeOfTask.value(indexOfUid.value(t.predecessors.at(l).predecessorUid, -1), -1);
            if (u < 0 || u == v) {
                continue;
            }
            g.links[v].append({u, l});
            if (!g.before.at(v).contains(u)) {
                g.before[v].append(u);
            }
        }
        // A summary sits left of its subtasks (the nearest shown one).
        for (int a = t.parentIndex; a >= 0; a = p.tasks.at(a).parentIndex) {
            const int u = nodeOfTask.value(a, -1);
            if (u >= 0) {
                if (!g.before.at(v).contains(u)) {
                    g.before[v].append(u);
                }
                break;
            }
        }
    }
    return g;
}

// Longest-path columns (Kahn's order; a node left over by a cycle — none
// should be, the scheduler ignores cyclic links — goes to column 0).
QList<int> columnsOf(const Graph &g)
{
    const int n = int(g.tasks.size());
    QList<QList<int>> after(n);
    QList<int> indegree(n, 0);
    for (int v = 0; v < n; ++v) {
        for (int u : g.before.at(v)) {
            after[u].append(v);
            ++indegree[v];
        }
    }
    QList<int> column(n, 0);
    QList<int> ready;
    for (int v = n - 1; v >= 0; --v) {
        if (indegree.at(v) == 0) {
            ready.append(v); // a stack in reverse, so lower nodes pop first
        }
    }
    while (!ready.isEmpty()) {
        const int u = ready.takeLast();
        for (int v : after.at(u)) {
            column[v] = std::max(column.at(v), column.at(u) + 1);
            if (--indegree[v] == 0) {
                ready.append(v);
            }
        }
    }
    return column;
}

} // namespace

NetworkLayout layOutNetwork(const Project &project, const QList<int> &rows, const NetworkOptions &options)
{
    NetworkLayout layout;
    const Graph g = buildGraph(project, rows, options, layout.nodeOfTask);
    const int n = int(g.tasks.size());
    const QList<int> column = columnsOf(g);
    for (int v = 0; v < n; ++v) {
        layout.columns = std::max(layout.columns, column.at(v) + 1);
    }
    // Rows, column by column (see the header).
    QList<int> rowOf(n, -1);
    for (int c = 0; c < layout.columns; ++c) {
        QList<std::pair<double, int>> wanted; // (desired row, node), node order breaks ties
        for (int v = 0; v < n; ++v) {
            if (column.at(v) != c) {
                continue;
            }
            double sum = 0;
            int count = 0;
            for (int u : g.before.at(v)) {
                if (rowOf.at(u) >= 0) {
                    sum += rowOf.at(u);
                    ++count;
                }
            }
            wanted.append({count > 0 ? sum / count : -1.0, v});
        }
        std::stable_sort(wanted.begin(), wanted.end(),
                         [](const auto &a, const auto &b) { return a.first < b.first; });
        QSet<int> used;
        int next = 0; // nodes that want nothing stack up in order
        for (const auto &[desired, v] : std::as_const(wanted)) {
            int r = desired < 0 ? next : int(std::lround(desired));
            while (used.contains(r)) {
                ++r;
            }
            used.insert(r);
            rowOf[v] = r;
            next = std::max(next, r + 1);
            layout.rows = std::max(layout.rows, r + 1);
        }
    }
    const NetworkOptions &o = options;
    for (int v = 0; v < n; ++v) {
        NetworkNode node;
        node.taskIndex = g.tasks.at(v);
        node.column = column.at(v);
        node.row = rowOf.at(v);
        node.box = QRectF(o.margin + node.column * (o.boxWidth + o.columnGap),
                          o.margin + node.row * (o.boxHeight + o.rowGap), o.boxWidth, o.boxHeight);
        layout.nodes.append(node);
    }
    layout.size = QSizeF(2 * o.margin + std::max(0, layout.columns) * (o.boxWidth + o.columnGap) - (layout.columns > 0 ? o.columnGap : 0),
                         2 * o.margin + std::max(0, layout.rows) * (o.boxHeight + o.rowGap) - (layout.rows > 0 ? o.rowGap : 0));
    for (int v = 0; v < n; ++v) {
        const NetworkNode &to = layout.nodes.at(v);
        const Task &t = project.tasks.at(to.taskIndex);
        for (const auto &[u, l] : g.links.at(v)) {
            const NetworkNode &from = layout.nodes.at(u);
            NetworkEdge e;
            e.from = u;
            e.to = v;
            e.type = t.predecessors.at(l).type;
            e.critical = t.critical && project.tasks.at(from.taskIndex).critical;
            const QPointF a(from.box.right(), from.box.center().y());
            const QPointF b(to.box.left(), to.box.center().y());
            e.points.append(a);
            if (to.column == from.column + 1) {
                const qreal x = from.box.right() + o.columnGap / 2;
                e.points << QPointF(x, a.y()) << QPointF(x, b.y());
            } else if (to.column > from.column + 1) {
                const qreal lane = b.y() < a.y() ? from.box.top() - o.rowGap / 2 : from.box.bottom() + o.rowGap / 2;
                const qreal x1 = from.box.right() + o.columnGap / 2;
                const qreal x2 = to.box.left() - o.columnGap / 2;
                e.points << QPointF(x1, a.y()) << QPointF(x1, lane) << QPointF(x2, lane) << QPointF(x2, b.y());
            }
            e.points.append(b);
            layout.edges.append(e);
        }
    }
    return layout;
}

int networkNodeAt(const NetworkLayout &layout, const QPointF &point)
{
    for (int i = 0; i < layout.nodes.size(); ++i) {
        if (layout.nodes.at(i).box.contains(point)) {
            return i;
        }
    }
    return -1;
}

} // namespace QindaOffice::Plan
