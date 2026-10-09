// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// CSV import and export (M49, D-049 §7). Import is plan-then-run: the
// proposal detects the delimiter, the header row and each column's type
// (leading-zero codes stay text; money needs a currency sign or a money
// word in the header), the user adjusts it, and the import runs as a
// schema step (a new table) plus ONE data step — or not at all: a bad
// value names its row and field and nothing is kept, unless the user chose
// to skip bad rows.

#include "base_file.h"
#include "base_schema.h"

#include <QVariantMap>

#include <optional>

namespace QindaOffice::Base {

// AGENT-CONTRACT (with the Excel and Access readers, M65): a table read
// from a typed source carries `types` — one entry per column, set where
// the source states the type (an Excel column's cells and formats, an
// Access schema) — and proposeImport takes them instead of guessing from
// the text. Its cells are then canonical text every parseInput accepts
// (C-locale numbers, ISO dates, "Yes"/"No"); a file field's cells are the
// bytes as hexadecimal digits (mdb-export -b hex), which importCsv decodes.
struct CsvTable {
    QStringList header;
    QList<QStringList> rows;
    QChar delimiter = QLatin1Char(',');
    bool headerRow = true;
    QList<std::optional<FieldType>> types; // empty, or one per column
};

struct CsvColumnPlan {
    int source = -1;
    QString name;
    FieldType type = FieldType::Text;
    bool include = true;
    bool primaryKey = false; // an "id" column of unique whole numbers keeps its values
};

struct CsvImportPlan {
    QString table;
    bool createNew = true;
    bool addId = true;
    bool skipBadRows = false;
    QList<CsvColumnPlan> columns;
    [[nodiscard]] QVariantMap toMap() const;
    static CsvImportPlan fromMap(const QVariantMap &map);
};

struct ImportResult {
    bool ok = false;
    QString error;
    int imported = 0;
    int skipped = 0;
    QStringList problems; // the first 20
};

// Decodes UTF-8 (BOM stripped) or, when that fails, Windows-1252; the
// delimiter is detected among , ; tab | when `delimiter` is null.
[[nodiscard]] CsvTable parseCsv(const QByteArray &bytes, QChar delimiter = QChar(), bool headerRow = true);
[[nodiscard]] FieldType detectType(const QStringList &values, const QString &header = {});
[[nodiscard]] CsvImportPlan proposeImport(const BaseFile &file, const CsvTable &csv, const QString &table);
ImportResult importCsv(BaseFile &file, const CsvTable &csv, const CsvImportPlan &plan, Change *schemaChange,
                       Change *dataChange);

// RFC 4180 text (quotes only where needed), CRLF line ends.
[[nodiscard]] QByteArray csvBytes(const QStringList &header, const QList<QStringList> &rows,
                                  QChar delimiter = QLatin1Char(','));
Result writeFileAtomically(const QString &path, const QByteArray &bytes);

} // namespace QindaOffice::Base
