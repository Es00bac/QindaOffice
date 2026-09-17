// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Stroke geometry (M22): strokes render as FILLED outlines, not as chains
// of round-cap segments. A segment chain double-paints every joint of a
// translucent stroke (the highlighter bloomed at each sample) and aliases
// hard; one closed outline fills exactly once and antialiases cleanly.
// Smoothing here is a render-time property (midpoint quadratics) — stored
// samples never change, so old notebooks keep their ink shape.

#include <QPainterPath>

namespace QindaOffice::Ink {

struct Stroke;

// Midpoint-quadratic smoothed centreline through the samples. A 2-sample
// stroke is a straight line.
[[nodiscard]] QPainterPath centreline(const Stroke &stroke);

// Closed, fillable outline: variable width from widthAt() for Pen,
// constant `stroke.width` for Highlighter (a highlighter has no pressure
// term); round caps for Pen, flat caps for Highlighter.
[[nodiscard]] QPainterPath outline(const Stroke &stroke);

} // namespace QindaOffice::Ink
