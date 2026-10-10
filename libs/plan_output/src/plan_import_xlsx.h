// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Excel workbooks for the task import (M83, D-092; plan_import.h): the
// first worksheet as a table of text through calc_io — each cell as Excel
// shows it, except that a date or time cell reads as "yyyy-MM-ddTHH:mm" so
// the import's date fields take it whatever its number format.

#include "plan_import.h"

namespace QindaOffice::Plan {

[[nodiscard]] bool readTaskTableXlsx(const QString &path, ImportTable *out, QString *error = nullptr);

} // namespace QindaOffice::Plan
