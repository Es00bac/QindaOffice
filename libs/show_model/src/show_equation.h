// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "presentation.h"
#include "math_layout.h"
namespace QindaOffice::Show {
// Slide coordinates are points. All canvas/player/print/PDF paths use this
// layout; resizing scales uniformly to fit the shape, never rasterizes it.
[[nodiscard]] Math::EquationLayout equationLayout(const Shape &shape);
[[nodiscard]] QSizeF equationSize(const Shape &shape);
void paintEquation(QPainter *painter, const Shape &shape);
}
