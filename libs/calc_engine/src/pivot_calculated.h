// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "pivot_compute.h"
namespace QindaOffice::Calc {
// Excel calculated fields evaluate once over field SUMS for each pivot cell,
// including grand totals; they must never sum per-record formula results.
[[nodiscard]] QString pivotCalculatedProblem(const PivotCache &cache, qint32 field);
[[nodiscard]] Value pivotCalculatedValue(const PivotCache &cache, qint32 field, const QVector<PivotAcc> &sums);
}
