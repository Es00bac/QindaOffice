// SPDX-License-Identifier: GPL-3.0-or-later
#include "spatial_grid.h"

namespace QindaOffice::Ink {

void SpatialGrid::rebuild(const QVector<Stroke> &strokes)
{
    clear();
    m_bounds.reserve(strokes.size());
    for (int i = 0; i < strokes.size(); ++i) {
        const QRectF bounds = strokes.at(i).bounds();
        m_bounds.append(bounds);
        const int x0 = int(std::floor(bounds.left() / m_cell));
        const int y0 = int(std::floor(bounds.top() / m_cell));
        const int x1 = int(std::floor(bounds.right() / m_cell));
        const int y1 = int(std::floor(bounds.bottom() / m_cell));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                m_cells[cellKey(x, y)].insert(i);
            }
        }
    }
}

void SpatialGrid::clear()
{
    m_cells.clear();
    m_bounds.clear();
}

QSet<int> SpatialGrid::query(const QRectF &rect) const
{
    QSet<int> out;
    const int x0 = int(std::floor(rect.left() / m_cell));
    const int y0 = int(std::floor(rect.top() / m_cell));
    const int x1 = int(std::floor(rect.right() / m_cell));
    const int y1 = int(std::floor(rect.bottom() / m_cell));
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            out |= m_cells.value(cellKey(x, y));
        }
    }
    return out;
}

int SpatialGrid::queryPoint(const QPointF &pos) const
{
    const QSet<int> candidates =
        m_cells.value(cellKey(int(std::floor(pos.x() / m_cell)),
                              int(std::floor(pos.y() / m_cell))));
    for (const int index : candidates) {
        if (m_bounds.at(index).contains(pos)) {
            return index;
        }
    }
    return -1;
}

} // namespace QindaOffice::Ink
