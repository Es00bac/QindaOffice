// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Access (.accdb/.mdb) import through mdb-tools (M49, D-049 §7).
//
// AGENT-NOTE (optional runtime dependency): QindaBase never links an
// Access reader. When the `mdb-tables` and `mdb-export` programs are on
// PATH (app-office/mdbtools on Gentoo) it lists the file's tables and reads
// each as CSV, which then goes through the ordinary CSV import plan (types
// detected, one undo step per table). When they are missing, the command
// says exactly that and what to install; nothing else depends on them.

#include "base_csv.h"

#include <optional>

namespace QindaOffice::Base {

struct AccessTools {
    bool available = false;
    QString tablesProgram;
    QString exportProgram;
    [[nodiscard]] QString missingText() const;
};

[[nodiscard]] AccessTools findAccessTools();
[[nodiscard]] QStringList accessTableNames(const AccessTools &tools, const QString &path, QString *error);
[[nodiscard]] std::optional<CsvTable> exportAccessTable(const AccessTools &tools, const QString &path,
                                                        const QString &table, QString *error);

} // namespace QindaOffice::Base
