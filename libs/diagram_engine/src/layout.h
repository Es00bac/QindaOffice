// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Automatic layout (M50, D-050): Design ▸ Re-layout / Arrange ▸ Auto-layout.
//
//  * Flowchart (top-down or left-to-right): a layered layout — cycles broken
//    by reversing DFS back edges, ranks by longest path, order within a rank
//    by barycentre sweeps, each shape pulled towards the centre of its
//    predecessors without overlapping its neighbours.
//  * Tree (org charts): each shape under its first incoming connector's
//    source; a manager whose reports are all leaves (four or more) hangs
//    them in a column beside a trunk, the way org charts save width; other
//    subtrees sit side by side, parents centred over their children.
//
// Only shapes move (containers never; they stay where they are); the
// layout keeps the top-left of the laid-out shapes' old bounding box, so a
// selection re-arranges in place. Connectors between laid-out shapes are
// re-glued to the facing connection points (bottom -> top, or right ->
// left) so the routes read cleanly.

#include "drawing.h"

#include <QString>
#include <QStringList>

namespace QindaOffice::Diagram {

// M87 (D-095) adds Visio's other Re-Layout Page styles: flowcharts bottom
// to top and right to left (the layered layout mirrored), Compact Tree
// (each child under its parent, indented, the way a file tree reads),
// Radial (the root in the middle, each level on a ring, every subtree in a
// wedge the size of its leaf count) and Circular (every shape on one ring,
// neighbours in the graph next to each other). Radial and Circular glue
// their connectors to the outlines and draw them straight, as Visio does.
enum class LayoutKind { TopDown, LeftRight, Tree, BottomUp, RightLeft, CompactTree, Radial, Circular };

struct LayoutOptions {
    LayoutKind kind = LayoutKind::TopDown;
    double rankGap = 56;   // between ranks / tree levels / rings
    double nodeGap = 36;   // between neighbours in a rank or on a ring
};

// Kind names as the app and the file write them ("topDown", "radial" …).
[[nodiscard]] QString layoutKindName(LayoutKind kind);
[[nodiscard]] LayoutKind layoutKindOf(const QString &name, LayoutKind fallback = LayoutKind::TopDown);

// Lays out `ids` (every non-container shape of the page when empty).
// False with `error` when there is nothing to arrange.
bool autoLayout(Page &page, const QStringList &ids, const LayoutOptions &options, QString *error = nullptr);

} // namespace QindaOffice::Diagram
