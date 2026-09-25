// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Timestamped backup copies of a company file in a folder the user
// chooses, keeping the newest `keep`. Each copy is a complete, openable
// company file (CompanyFile::backupTo = SQLite VACUUM INTO, consistent even
// while the books are open).

#include "books_types.h"

#include <QString>
#include <QStringList>

namespace QindaOffice::Books {

class CompanyFile;

class Backups {
public:
    // "<company file base name>-YYYYMMDD-HHMMSS.qbooks" in `dir` (created
    // when missing); then deletes the oldest of this company's backups in
    // `dir` beyond `keep`. Result.ok with the new path in `createdPath`.
    static Result createTimestamped(CompanyFile &file, const QString &dir, int keep = 10,
                                    QString *createdPath = nullptr);
    // This company's backups in `dir`, oldest first.
    [[nodiscard]] static QStringList list(const CompanyFile &file, const QString &dir);
};

} // namespace QindaOffice::Books
