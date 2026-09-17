// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Stroke editing (M22 eraser; the M24 selection ops build on this file):
// model-only ink surgery — no widgets, no painting. Everything works in
// PAGE coordinates and takes its candidates from SpatialGrid::query, so a
// page with thousands of strokes pays only for the nearby ones per call.

#include "stroke_model.h"

#include <QSet>

namespace QindaOffice::Ink {

enum class EraseMode { Stroke, Partial };

// Erases at `pos` among `candidates` (from SpatialGrid::query). Stroke mode
// removes whole strokes; Partial removes samples within `radius` and splits
// a stroke where a gap opens (a fragment with < 2 samples is dropped).
// Returns true when anything changed. Indices are invalidated on true.
bool eraseAt(StrokeLayer *layer, const QSet<int> &candidates, const QPointF &pos,
             qreal radius, EraseMode mode);

} // namespace QindaOffice::Ink
