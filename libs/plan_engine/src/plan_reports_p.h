// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Private to plan_reports*.cpp: the dashboards of plan_reports_costs.cpp
// (M79: Project's Cost Overview and Work Overview).

#include "plan_reports.h"

namespace QindaOffice::Plan::Reports {

[[nodiscard]] PlanReport costOverview(const Project &project, const QLocale &locale);
[[nodiscard]] PlanReport workOverview(const Project &project, const QLocale &locale);

} // namespace QindaOffice::Plan::Reports
