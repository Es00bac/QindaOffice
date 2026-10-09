// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The Network Diagram's layout (M79, D-088): one box per task, in columns
// by dependency (Project's "top down from left"), and the links between
// them routed through the gaps. Pure geometry in abstract points; the
// painter (plan_output's network_print.h) draws it for the window, paper
// and PDF alike, so all three show the same picture.
//
// AGENT-CONTRACT (layout rules, docs/plan.md "Network Diagram"):
//  * Nodes are the given rows (the view's filtered, collapsed, sorted
//    tasks); a summary is a node when `summaries` is on.
//  * Column = the longest chain of links (and summary → subtask, so a
//    summary sits left of its subtasks) leading to the task.
//  * Rows: columns are filled left to right; a node asks for the mean row
//    of the nodes that lead to it (so a chain runs straight) and takes the
//    first free row at or below that — boxes never overlap.
//  * Edges run from the right side of the predecessor's box to the left
//    side of the successor's: out through the column gap after the
//    predecessor, along the row gap above or below it, down the column gap
//    before the successor. Gaps are never occupied by boxes, so no edge
//    crosses a box. Ignored links (Task::ignoredLinks) are not drawn.

#include "plan_types.h"

#include <QHash>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSizeF>

namespace QindaOffice::Plan {

struct NetworkOptions {
    qreal boxWidth = 168;
    qreal boxHeight = 64;
    qreal columnGap = 44;
    qreal rowGap = 22;
    qreal margin = 16;
    bool summaries = true;
};

struct NetworkNode {
    int taskIndex = -1;
    int column = 0;
    int row = 0;
    QRectF box;
};

struct NetworkEdge {
    int from = -1; // node indices
    int to = -1;
    LinkType type = LinkType::FinishToStart;
    bool critical = false; // both ends critical
    QList<QPointF> points;
};

struct NetworkLayout {
    QList<NetworkNode> nodes;
    QList<NetworkEdge> edges;
    QHash<int, int> nodeOfTask; // task index → node index
    int columns = 0;
    int rows = 0;
    QSizeF size;
};

[[nodiscard]] NetworkLayout layOutNetwork(const Project &project, const QList<int> &rows,
                                          const NetworkOptions &options = {});
// The node whose box holds `point`, -1 when none.
[[nodiscard]] int networkNodeAt(const NetworkLayout &layout, const QPointF &point);

} // namespace QindaOffice::Plan
