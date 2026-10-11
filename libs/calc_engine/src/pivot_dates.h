// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "pivot.h"
#include <QDate>
namespace QindaOffice::Calc {
// Date filters compare calendar dates, ignoring the time-of-day fraction.
// `today` is injectable for repeatable tests; refresh uses the local date.
[[nodiscard]] bool pivotDateAccepts(const PivotFilter &filter, const Value &value,
                                    const QDate &today = QDate::currentDate());
[[nodiscard]] QStringList pivotDateTypes();
}
