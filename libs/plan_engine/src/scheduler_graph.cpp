// SPDX-License-Identifier: GPL-3.0-or-later
// The scheduler's node graph: a summary is an entry and an exit node, links
// run exit → entry; Kahn's topological order and, when a file brought a
// cycle, Tarjan's strongly connected components (scheduler.h).
#include "scheduler_p.h"

#include <algorithm>

namespace QindaOffice::Plan::Detail {

void SchedulePass::buildGraph(QList<QList<int>> *adjacency, QList<bool> *present) const
{
    const QList<Task> &tasks = m_project.tasks;
    *adjacency = QList<QList<int>>(2 * m_n);
    *present = QList<bool>(2 * m_n, false);
    QList<QList<int>> children(m_n);
    for (int i = 0; i < m_n; ++i) {
        if (!tasks.at(i).effectiveActive) {
            continue;
        }
        (*present)[entry(i)] = (*present)[exit(i)] = true;
        if (tasks.at(i).parentIndex >= 0) {
            children[tasks.at(i).parentIndex].append(i);
        }
    }
    for (int i = 0; i < m_n; ++i) {
        if (!tasks.at(i).effectiveActive || !tasks.at(i).summary) {
            continue;
        }
        if (children.at(i).isEmpty()) {
            (*adjacency)[entry(i)].append(exit(i));
        }
        for (int c : children.at(i)) {
            (*adjacency)[entry(i)].append(entry(c));
            (*adjacency)[exit(c)].append(exit(i));
        }
    }
    for (int i = 0; i < m_n; ++i) {
        for (const LinkRef &ref : m_incoming.at(i)) {
            (*adjacency)[exit(ref.predecessor)].append(entry(i));
        }
    }
}

bool SchedulePass::topologicalOrder(QList<int> *order, QList<int> *stuck) const
{
    QList<QList<int>> adjacency;
    QList<bool> present;
    buildGraph(&adjacency, &present);
    QList<int> indegree(2 * m_n, 0);
    int total = 0;
    for (int u = 0; u < 2 * m_n; ++u) {
        total += present.at(u) ? 1 : 0;
        for (int v : adjacency.at(u)) {
            ++indegree[v];
        }
    }
    order->clear();
    QList<int> queue;
    for (int u = 0; u < 2 * m_n; ++u) {
        if (present.at(u) && indegree.at(u) == 0) {
            queue.append(u);
        }
    }
    for (int head = 0; head < queue.size(); ++head) {
        const int u = queue.at(head);
        order->append(u);
        for (int v : adjacency.at(u)) {
            if (--indegree[v] == 0) {
                queue.append(v);
            }
        }
    }
    if (order->size() == total) {
        return true;
    }
    for (int u = 0; u < 2 * m_n; ++u) {
        if (present.at(u) && indegree.at(u) > 0) {
            stuck->append(u);
        }
    }
    return false;
}

QList<int> SchedulePass::components(const QList<int> &nodes) const
{
    // Tarjan's strongly connected components, iterative, over the nodes the
    // topological sort could not place.
    QList<QList<int>> adjacency;
    QList<bool> present;
    buildGraph(&adjacency, &present);
    const int size = 2 * m_n;
    QList<bool> inSet(size, false);
    for (int u : nodes) {
        inSet[u] = true;
    }
    QList<int> index(size, -1);
    QList<int> low(size, 0);
    QList<bool> onStack(size, false);
    QList<int> component(size, -1);
    QList<int> stack;
    int counter = 0;
    int components = 0;
    struct Frame {
        int node;
        int next;
    };
    for (int root : nodes) {
        if (index.at(root) >= 0) {
            continue;
        }
        QList<Frame> frames{{root, 0}};
        index[root] = low[root] = counter++;
        stack.append(root);
        onStack[root] = true;
        while (!frames.isEmpty()) {
            Frame &f = frames.last();
            const QList<int> &out = adjacency.at(f.node);
            if (f.next < out.size()) {
                const int v = out.at(f.next++);
                if (!inSet.at(v)) {
                    continue;
                }
                if (index.at(v) < 0) {
                    index[v] = low[v] = counter++;
                    stack.append(v);
                    onStack[v] = true;
                    frames.append({v, 0});
                } else if (onStack.at(v)) {
                    low[f.node] = std::min(low.at(f.node), index.at(v));
                }
                continue;
            }
            const int u = f.node;
            frames.removeLast();
            if (!frames.isEmpty()) {
                low[frames.last().node] = std::min(low.at(frames.last().node), low.at(u));
            }
            if (low.at(u) == index.at(u)) {
                int w = -1;
                do {
                    w = stack.takeLast();
                    onStack[w] = false;
                    component[w] = components;
                } while (w != u);
                ++components;
            }
        }
    }
    return component;
}

} // namespace QindaOffice::Plan::Detail
