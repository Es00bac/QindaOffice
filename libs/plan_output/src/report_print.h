// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A QindaPlan report (plan_reports.h) on paper: title and subtitle, the
// KPIs as a grid of boxes, then each table — heading, header row, zebra
// rows, numeric columns right-aligned, row tones as a coloured left rule.
// A table that breaks across pages repeats its heading ("continued") and
// header row; a heading never ends a page alone. Nothing here recomputes a
// figure: the report's text is printed as built.

#include "plan_page_output.h"
#include "plan_reports.h"

namespace QindaOffice::Plan {

[[nodiscard]] PageLayout reportLayout(const PlanReport &report, const QLocale &locale = QLocale());
[[nodiscard]] PagePainter reportPainter(const PlanReport &report, const QLocale &locale = QLocale());

} // namespace QindaOffice::Plan
