// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// SpatialGrid (plan section 7.4): uniform grid over the page; strokes are
// indexed by the cells their bounds touch, so hit-testing and region
// erasure touch only nearby strokes. Cell size is a compile-time choice:
// strokes are small relative to pages.

#include "stroke_model.h"

#include <QHash>
#include <QSet>

namespace QindaOffice::Ink {

class SpatialGrid {
public:
    explicit SpatialGrid(qreal cellSize = 64.0) : m_cell(cellSize) {}

    void rebuild(const QVector<Stroke> &strokes);
    void clear();

    // Stroke indices whose bounds may contain `rect`.
    [[nodiscard]] QSet<int> query(const QRectF &rect) const;
    [[nodiscard]] int queryPoint(const QPointF &pos) const;

private:
    [[nodiscard]] quint64 cellKey(int x, int y) const
    {
        return (quint64(quint32(x)) << 32) | quint64(quint32(y));
    }

    qreal m_cell;
    QHash<quint64, QSet<int>> m_cells;
    QVector<QRectF> m_bounds;
};

} // namespace QindaOffice::Ink
