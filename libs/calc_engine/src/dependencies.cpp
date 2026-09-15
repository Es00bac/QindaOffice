// SPDX-License-Identifier: GPL-3.0-or-later
#include "dependencies.h"

namespace QindaOffice::Calc {

namespace {
constexpr qint32 kBandRows = 256;
}

bool targetCovers(const DepTarget &target, const CellKey &key)
{
    return target.sheet == key.sheet() && target.range.contains({key.row(), key.col()});
}

qint64 DependencyGraph::bandKey(qint32 sheet, qint32 bandRow)
{
    return (qint64(quint64(quint16(sheet))) << 48) | (qint64(quint64(quint32(bandRow))) << 24);
}

void DependencyGraph::updateCell(const CellKey &cell, const QVector<DepTarget> &targets)
{
    removeCell(cell);
    m_targets.insert(cell, targets);
    QSet<qint64> bands;
    for (const DepTarget &target : targets) {
        if (!target.range.isValid()) {
            continue;
        }
        const qint32 firstBand = target.range.topLeft.row / kBandRows;
        const qint32 lastBand = target.range.bottomRight.row / kBandRows;
        for (qint32 b = firstBand; b <= lastBand; ++b) {
            bands.insert(bandKey(target.sheet, b));
        }
        if (target.range.cellCount() == 1) {
            const CellKey dep(target.sheet, target.range.topLeft.row, target.range.topLeft.col);
            m_dependents[dep].insert(cell);
        }
    }
    m_cellBands.insert(cell, bands);
    for (const qint64 band : bands) {
        m_bandDependents[band].insert(cell);
    }
}

void DependencyGraph::removeCell(const CellKey &cell)
{
    const auto targetsIt = m_targets.find(cell);
    if (targetsIt != m_targets.end()) {
        for (const DepTarget &target : targetsIt.value()) {
            if (target.range.isValid() && target.range.cellCount() == 1) {
                const CellKey dep(target.sheet, target.range.topLeft.row,
                                  target.range.topLeft.col);
                auto it = m_dependents.find(dep);
                if (it != m_dependents.end()) {
                    it->remove(cell);
                    if (it->isEmpty()) {
                        m_dependents.erase(it);
                    }
                }
            }
        }
        m_targets.erase(targetsIt);
    }
    const auto bandsIt = m_cellBands.find(cell);
    if (bandsIt != m_cellBands.end()) {
        for (const qint64 band : bandsIt.value()) {
            auto it = m_bandDependents.find(band);
            if (it != m_bandDependents.end()) {
                it->remove(cell);
                if (it->isEmpty()) {
                    m_bandDependents.erase(it);
                }
            }
        }
        m_cellBands.erase(bandsIt);
    }
    m_volatile.remove(cell);
}

void DependencyGraph::clear()
{
    m_dependents.clear();
    m_targets.clear();
    m_bandDependents.clear();
    m_cellBands.clear();
    m_volatile.clear();
}

QSet<CellKey> DependencyGraph::directDependentsOf(const CellKey &cell) const
{
    QSet<CellKey> out;
    const auto it = m_dependents.constFind(cell);
    if (it != m_dependents.constEnd()) {
        out |= it.value();
    }
    const qint32 band = cell.row() / kBandRows;
    const auto bandIt = m_bandDependents.constFind(bandKey(cell.sheet(), band));
    if (bandIt != m_bandDependents.constEnd()) {
        out |= bandIt.value();
    }
    return out;
}

QVector<DepTarget> DependencyGraph::targetsOf(const CellKey &cell) const
{
    return m_targets.value(cell);
}

QSet<CellKey> DependencyGraph::dependentsOf(const QSet<CellKey> &edited) const
{
    QSet<CellKey> closure = edited;
    QSet<CellKey> frontier = edited;
    while (!frontier.isEmpty()) {
        QSet<CellKey> next;
        for (const CellKey &cell : frontier) {
            const QSet<CellKey> deps = directDependentsOf(cell);
            for (const CellKey &dep : deps) {
                if (!closure.contains(dep)) {
                    closure.insert(dep);
                    next.insert(dep);
                }
            }
        }
        frontier = next;
    }
    return closure;
}

void DependencyGraph::addVolatile(const CellKey &cell)
{
    m_volatile.insert(cell);
}

void DependencyGraph::removeVolatile(const CellKey &cell)
{
    m_volatile.remove(cell);
}

QVector<CellKey> topoOrder(const QSet<CellKey> &dirty, const DependencyGraph &graph,
                           QSet<CellKey> *cycles)
{
    // Build exact edges: for each dirty cell, its dependents refine to those
    // whose recorded targets actually cover it.
    QHash<CellKey, int> indegree;
    QMultiHash<CellKey, CellKey> edges;
    for (const CellKey &cell : dirty) {
        const QSet<CellKey> candidates = graph.directDependentsOf(cell);
        for (const CellKey &candidate : candidates) {
            if (!dirty.contains(candidate)) {
                continue;
            }
            const QVector<DepTarget> targets = graph.targetsOf(candidate);
            const bool covers = std::any_of(targets.begin(), targets.end(),
                                            [&cell](const DepTarget &t) {
                                                return targetCovers(t, cell);
                                            });
            if (covers) {
                edges.insert(cell, candidate);
                indegree[candidate] += 1;
            }
        }
    }
    QVector<CellKey> queue;
    for (const CellKey &cell : dirty) {
        if (indegree.value(cell) == 0) {
            queue.append(cell);
        }
    }
    QVector<CellKey> order;
    order.reserve(dirty.size());
    while (!queue.isEmpty()) {
        const CellKey cell = queue.takeLast();
        order.append(cell);
        const auto range = edges.equal_range(cell);
        for (auto it = range.first; it != range.second; ++it) {
            const CellKey dependent = it.value();
            if (--indegree[dependent] == 0) {
                queue.append(dependent);
            }
        }
    }
    if (cycles != nullptr) {
        cycles->clear();
        if (order.size() != dirty.size()) {
            for (const CellKey &cell : dirty) {
                if (indegree.value(cell) > 0) {
                    cycles->insert(cell);
                }
            }
        }
    }
    return order;
}

} // namespace QindaOffice::Calc
