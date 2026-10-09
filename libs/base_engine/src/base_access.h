// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Access (.accdb/.mdb) import through mdb-tools (M49, completed in M65 —
// D-049 §7, D-074). The import itself is base_access_import.h; this is the
// reader under it.
//
// AGENT-NOTE (optional runtime dependency): QindaBase never links an
// Access reader. When the mdb-tools programs are on PATH (app-office/
// mdbtools on Gentoo) it reads through them:
//   mdb-tables -1 FILE                     the table names
//   mdb-export -b hex -D %Y-%m-%d -T "%Y-%m-%d %H:%M:%S" FILE TABLE
//                                          the rows as CSV: binary (OLE)
//                                          columns as hex, ISO dates
//   mdb-schema FILE postgres               types, keys, indexes, required,
//                                          descriptions and relationships
//   mdb-queries -L -1 FILE, mdb-queries FILE QUERY
//                                          the saved queries' SQL
//   mdb-export -b strip FILE MSysObjects   each query's kind (Flags)
// mdb-tables and mdb-export are required; the others refine, and one that
// is missing or fails is reported, never fatal (types are then detected
// from the text, as for CSV). The postgres schema backend is the one that
// prints keys, indexes and relationships and keeps AutoNumber (SERIAL),
// Currency (NUMERIC(15,2)) and date-only fields (DATE) apart; it lower-
// cases identifiers, so names are matched back case-blind to the spelling
// mdb-tables and mdb-export print (base_access_schema.h).

#include "base_csv.h"

#include <QHash>

#include <optional>

namespace QindaOffice::Base {

struct AccessTools {
    bool available = false; // mdb-tables and mdb-export
    QString tablesProgram;
    QString exportProgram;
    QString schemaProgram;  // optional
    QString queriesProgram; // optional
    [[nodiscard]] QString missingText() const;
};

[[nodiscard]] AccessTools findAccessTools();
[[nodiscard]] QStringList accessTableNames(const AccessTools &tools, const QString &path, QString *error);
[[nodiscard]] std::optional<CsvTable> exportAccessTable(const AccessTools &tools, const QString &path,
                                                        const QString &table, QString *error);

// What the import reads, so tests can feed it captured mdb-tools output.
class AccessReader {
public:
    virtual ~AccessReader() = default;
    [[nodiscard]] virtual QStringList tables(QString *error) = 0;
    [[nodiscard]] virtual std::optional<CsvTable> rows(const QString &table, QString *error) = 0;
    // mdb-schema's postgres DDL; empty with `error` when unavailable.
    [[nodiscard]] virtual QString schema(QString *error) = 0;
    [[nodiscard]] virtual QStringList queries(QString *error) = 0;
    [[nodiscard]] virtual QString querySql(const QString &name, QString *error) = 0;
    // Query name → MSysObjects.Flags (its kind); empty when unreadable.
    [[nodiscard]] virtual QHash<QString, int> queryFlags(QString *error) = 0;
};

class MdbToolsReader final : public AccessReader {
public:
    MdbToolsReader(AccessTools tools, QString path);
    [[nodiscard]] QStringList tables(QString *error) override;
    [[nodiscard]] std::optional<CsvTable> rows(const QString &table, QString *error) override;
    [[nodiscard]] QString schema(QString *error) override;
    [[nodiscard]] QStringList queries(QString *error) override;
    [[nodiscard]] QString querySql(const QString &name, QString *error) override;
    [[nodiscard]] QHash<QString, int> queryFlags(QString *error) override;

private:
    AccessTools m_tools;
    QString m_path;
};

// MSysObjects as mdb-export prints it → query name → Flags (Type 5 rows).
[[nodiscard]] QHash<QString, int> parseQueryFlags(const CsvTable &objects);

} // namespace QindaOffice::Base
