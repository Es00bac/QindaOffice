// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to pivot_edit.cpp and pivot_ops.cpp (M74, D-083): writing a
// layout is prepare (compute, lay out, check the cells it would take) and
// commit (clear the old output, write the new one), so an operation over
// several pivots — a refresh of a shared cache, a grouping — changes no
// cell until every pivot is known to fit.

#include "pivot_edit.h"

#include <optional>

namespace QindaOffice::Calc::PivotInternal {

struct Prepared {
    PivotRef ref;
    PivotTable pivot; // with its new location and extent
    std::shared_ptr<const PivotResult> result;
    std::shared_ptr<const PivotRender> render;
};

[[nodiscard]] PivotCache *mutableCache(Workbook &book, qint32 id);
// `pivot` as it would be shown at `ref` (its own old output does not count
// as in the way); nullopt with Excel's message in `error`.
[[nodiscard]] std::optional<Prepared> prepare(const Workbook &book, const PivotRef &ref, const PivotTable &pivot,
                                              const PivotCache &cache, bool replace, QString *error);
// Writes it: the old output cleared, the new one written, columns widened,
// dependents recalculated; the pivot at prepared.ref replaced.
void commit(Workbook &book, const Prepared &prepared);
[[nodiscard]] QList<PivotRef> pivotsOfCache(const Workbook &book, qint32 cacheId);

} // namespace QindaOffice::Calc::PivotInternal
