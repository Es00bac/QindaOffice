// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// RFC 4180 CSV reader/writer (plan section 7.2): quoted fields, doubled
// quotes, embedded newlines and CRLF; UTF-8 BOM handling; separator
// auto-detection over the first 20 lines (', ; \t |'); decimal inferred from
// the separator (';' implies ','); a leading ' in a cell forces text.

#include <QString>
#include <QVector>

namespace QindaOffice::Csv {

struct Table {
    QVector<QStringList> rows;
    QString separator = QStringLiteral(",");
    QString decimal = QStringLiteral(".");

    [[nodiscard]] int columnCount() const;
    [[nodiscard]] QString cellAt(int row, int col) const; // empty when out of range
};

struct ReadOptions {
    QString separator; // empty = auto-detect
    QString decimal;   // empty = infer from separator
};

[[nodiscard]] Table parse(const QByteArray &data, const ReadOptions &options = {},
                          QString *error = nullptr);

struct WriteOptions {
    QString separator = QStringLiteral(",");
    bool excelCompatible = false; // locale separator + BOM (plan section 7.2)
};

[[nodiscard]] QByteArray serialize(const Table &table, const WriteOptions &options = {});

// Splits one line of plain (unquoted) text for detection heuristics.
[[nodiscard]] QStringList splitPlainLine(const QString &line, const QString &separator);

} // namespace QindaOffice::Csv
