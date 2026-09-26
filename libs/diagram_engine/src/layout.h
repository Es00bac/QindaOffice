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

enum class LayoutKind { TopDown, LeftRight, Tree };

struct LayoutOptions {
    LayoutKind kind = LayoutKind::TopDown;
    double rankGap = 56;   // between ranks / tree levels
    double nodeGap = 36;   // between neighbours in a rank
};

// Lays out `ids` (every non-container shape of the page when empty).
// False with `error` when there is nothing to arrange.
bool autoLayout(Page &page, const QStringList &ids, const LayoutOptions &options, QString *error = nullptr);

} // namespace QindaOffice::Diagram
