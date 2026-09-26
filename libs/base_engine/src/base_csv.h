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

namespace QindaOffice::Base {

struct CsvTable {
    QStringList header;
    QList<QStringList> rows;
    QChar delimiter = QLatin1Char(',');
    bool headerRow = true;
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
