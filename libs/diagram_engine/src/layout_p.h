// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to auto-layout (layout.cpp, layout_more.cpp): the graph of the
// shapes being laid out and the helpers every style shares.

#include "drawing.h"

#include <QHash>
#include <QPair>
#include <QRectF>
#include <QSet>
#include <QStringList>
#include <QVector>

namespace QindaOffice::Diagram::LayoutDetail {

struct Graph {
    QVector<int> nodes;                // indices into page.shapes
    QHash<int, int> slot;              // shape index -> position in nodes
    QVector<QVector<int>> out;         // by slot
    QVector<QVector<int>> in;          // by slot
    QVector<int> edgeConnectors;       // connector index per edge
    QVector<QPair<int, int>> edges;    // (from slot, to slot)
};

// The non-container, unlocked shapes `ids` (all when empty) and the
// connectors glued between two of them. `out`/`in` are filled by the
// styles that need them.
[[nodiscard]] Graph buildGraph(const Page &page, const QStringList &ids);
// Re-glues the graph's connectors to the facing connection points (ports
// 0..3 = top, right, bottom, left): bottom → top when `vertical`, else
// right → left; reversed when the target lies before the source.
void reglue(Page &page, const Graph &g, bool vertical, const QSet<int> &hanging = {});
[[nodiscard]] QRectF currentBounds(const Page &page, const Graph &g);

// layout_more.cpp: the M87 styles (D-095).
void radial(Page &page, Graph &g, double rankGap, double nodeGap);
void circular(Page &page, Graph &g, double nodeGap);
void compactTree(Page &page, Graph &g, double rankGap, double nodeGap);
// Mirrors the laid-out shapes along the flow (top-to-bottom becomes
// bottom-to-top, left-to-right becomes right-to-left) inside their bounds.
void mirror(Page &page, const Graph &g, bool vertical);

} // namespace QindaOffice::Diagram::LayoutDetail
