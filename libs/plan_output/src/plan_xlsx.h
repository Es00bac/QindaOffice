// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The task sheet as an Excel workbook (M63, D-071), beside its CSV: the
// table on screen, one row per task, through the suite's own xlsx writer
// (calc_io, the code QindaCalc saves with).
//
// AGENT-CONTRACT (cells, tst_plan_output): the header row is the fields'
// titles (bold, frozen); costs are NUMBERS in major units under the
// project currency's format; dates are date-times (serial days) under
// yyyy-mm-dd hh:mm; durations, slack and variances are numbers of working
// days and work a number of hours (formatted "0.## d" / "0.## h");
// % complete a fraction under 0%; ID and priority whole numbers; the name
// indented by its outline level; everything else the sheet's own text.

#include "plan_types.h"

#include <QLocale>
#include <QString>
#include <QStringList>

namespace QindaOffice::Plan {

bool writeTaskSheetXlsx(const Project &project, const QStringList &fields, const QString &path,
                        const QLocale &locale = QLocale(), QString *error = nullptr);

} // namespace QindaOffice::Plan
