// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What is under the pointer in the slide show (M91, D-099): the action a
// click — or the mouse passing over — runs at a point of a slide as the
// show draws it at a build step. Linked text wins over its shape's own
// action (PowerPoint's order); a group member's action over its group's;
// the topmost shape over the ones under it. Shapes not on the slide yet
// (a later build), gone (an exit already played), hidden, or a medium
// hidden while it does not play are never hit.

#include "presentation.h"

#include <optional>

namespace QindaOffice::Show {

struct ActionHit {
    QString shapeId;   // the shape (or group member) the action belongs to
    ShowAction action;
    QRectF bounds;     // slide points: the shape's box (where a highlight goes)
    bool text = false; // a link on a run of text
};

[[nodiscard]] std::optional<ActionHit> actionAt(const Slide &slide, const QPointF &point, int buildStep, bool hover);
// True when `shape` is on the slide at `buildStep` as the show draws it.
[[nodiscard]] bool shownAt(const Shape &shape, int buildStep);

} // namespace QindaOffice::Show
