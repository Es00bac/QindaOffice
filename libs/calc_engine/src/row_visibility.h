// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "dependencies.h"
namespace QindaOffice::Calc {
class Workbook;
// Totals to recompute after ordinary formulas settle an active filter's predicates.
[[nodiscard]] QSet<CellKey> rowVisibilityTotals(const Workbook &book);
}
