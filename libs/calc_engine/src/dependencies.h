// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "value.h"

#include <QHash>
#include <QSet>

namespace QindaOffice::Calc {

// Packed cell key: sheet index (16 bits), row (24 bits), col (24 bits).
struct CellKey {
    qint64 packed = -1;

    CellKey() = default;
    CellKey(qint32 sheet, qint32 row, qint32 col)
        : packed((qint64(quint64(quint16(sheet))) << 48) | (qint64(quint64(quint32(row))) << 24)
                 | qint64(quint64(quint32(col))))
    {
    }

    [[nodiscard]] qint32 sheet() const { return qint32((packed >> 48) & 0xffff); }
    [[nodiscard]] qint32 row() const { return qint32((packed >> 24) & 0xffffff); }
    [[nodiscard]] qint32 col() const { return qint32(packed & 0xffffff); }
    [[nodiscard]] bool isValid() const { return packed >= 0; }
    bool operator==(const CellKey &) const = default;
};
static_assert(sizeof(CellKey) == 8);

inline size_t qHash(const CellKey &key, size_t seed = 0)
{
    return ::qHash(key.packed, seed);
}

// Resolved dependency target: a sheet index plus the cell range the formula
// reads there. Workbook resolves sheet names before indexing.
struct DepTarget {
    qint32 sheet = 0;
    CellRange range;
};

[[nodiscard]] bool targetCovers(const DepTarget &target, const CellKey &key);

// Dependency index for recalculation (plan section 7.2). Range references
// are banded per sheet in 256-row groups so an edit touches only the bands
// it overlaps; the closure (dependentsOf) is deliberately over-approximate
// there, while topoOrder refines candidates through exact target coverage so
// cycle detection and evaluation order are precise.
class DependencyGraph {
public:
    void updateCell(const CellKey &cell, const QVector<DepTarget> &targets);
    void removeCell(const CellKey &cell);
    void clear();

    [[nodiscard]] QSet<CellKey> dependentsOf(const QSet<CellKey> &edited) const;
    [[nodiscard]] QSet<CellKey> directDependentsOf(const CellKey &cell) const;
    [[nodiscard]] QVector<DepTarget> targetsOf(const CellKey &cell) const;

    // Borrowed views for the hot paths. directDependentsOf and targetsOf
    // both return by value, and the closure and the topological sort call
    // them once per dirty cell: on a 100k-cell workbook that was 177s of
    // set-building and vector-copying to order 1.7s of evaluation. These
    // hand out the stored containers instead, and never outlive an edit.
    [[nodiscard]] static qint64 bandOf(const CellKey &cell);
    [[nodiscard]] const QSet<CellKey> *bandDependents(qint64 band) const;
    [[nodiscard]] const QVector<DepTarget> *targetsFor(const CellKey &cell) const;

    void addVolatile(const CellKey &cell);
    void removeVolatile(const CellKey &cell);
    [[nodiscard]] const QSet<CellKey> &volatileCells() const { return m_volatile; }

private:
    static qint64 bandKey(qint32 sheet, qint32 bandRow);

    QHash<CellKey, QSet<CellKey>> m_dependents;            // single-cell dep -> dependents
    QHash<CellKey, QVector<DepTarget>> m_targets;          // cell -> read targets
    QHash<qint64, QSet<CellKey>> m_bandDependents;         // band -> cells with ranges over it
    QHash<CellKey, QSet<qint64>> m_cellBands;              // cell -> bands its ranges span
    QSet<CellKey> m_volatile;
};

// Kahn topological order over the dirty set. Edges dep -> dependent are
// established only when the dependent's recorded targets exactly cover the
// dependency cell, so band over-approximation can create spurious ordering
// hints but never a false cycle. Leftover dirty nodes are real cycles.
// Candidates come from the band index alone: updateCell files every target
// there, single cells included, so it is a superset of the exact map and
// each edge is still discovered exactly once.
[[nodiscard]] QVector<CellKey> topoOrder(const QSet<CellKey> &dirty,
                                         const DependencyGraph &graph,
                                         QSet<CellKey> *cycles);

} // namespace QindaOffice::Calc
