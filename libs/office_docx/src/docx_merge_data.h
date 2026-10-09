// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A mail merge's recipients (M77, D-086): a table of named columns, read
// from a delimited text file (Word's text data sources: CSV with a header
// row; tab, semicolon and pipe also sniffed), and written as the CSV that
// Type a New List saves. QindaWrite fills the same table from a workbook
// (calc_io) or QindaMail's People.
//
// The recipient list the merge uses is a VIEW of the table: the records
// the filters keep, in the sort's order, minus the ones the user cleared
// in Edit Recipient List — Word's Filter and Sort and its check boxes.

#include "docx_mailmerge.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace QindaOffice::Docx {

struct MergeData {
    QStringList fields;          // column names, as the header row has them
    QList<QStringList> records;  // each as long as `fields`
    QString source;              // a path, or "qindamail:people[/group]"

    [[nodiscard]] bool isEmpty() const { return fields.isEmpty(); }
    [[nodiscard]] int fieldIndex(const QString &name) const;
    // A record's value of `field` (case-insensitive name); empty when the
    // field or record does not exist.
    [[nodiscard]] QString value(int record, const QString &field) const;
};

// Bounds for a data file: what a hostile or mistaken file can make the
// app allocate (a merge of more is not a mail merge).
inline constexpr qint64 kMaxMergeFileBytes = 32LL * 1024 * 1024;
inline constexpr int kMaxMergeRecords = 100000;
inline constexpr int kMaxMergeFields = 255;

// RFC 4180 text with a header row; the delimiter is sniffed from the first
// line (comma, tab, semicolon, pipe; quoted text ignored). A UTF-8 BOM is
// dropped; text that is not UTF-8 reads as Latin-1 (Word's ANSI export).
[[nodiscard]] MergeData parseDelimited(const QByteArray &bytes, QString *error = nullptr);
[[nodiscard]] MergeData readDelimitedFile(const QString &path, QString *error = nullptr);
// UTF-8 CSV with a header row, CRLF line ends, fields quoted when they hold
// a comma, quote or line break (what Word and Excel read back).
[[nodiscard]] QByteArray csvBytes(const MergeData &data);
bool writeCsvFile(const QString &path, const MergeData &data, QString *error = nullptr);

// Whether record `record` passes Word's filter rows (AND binds tighter
// than OR, as in Word's query). Comparisons are numeric when both sides are
// numbers, else case-insensitive text; "contains" is a substring test.
[[nodiscard]] bool passesFilters(const MergeData &data, int record, const QList<MergeFilter> &filters);

// The records the merge uses, in merge order: filtered, sorted (stable;
// numbers as numbers), the excluded ones (data source indices) left out.
[[nodiscard]] QList<int> recipientOrder(const MergeData &data, const MailMergeSettings &settings);
// Like recipientOrder() but keeping the excluded records (Edit Recipient
// List shows them unchecked).
[[nodiscard]] QList<int> filteredOrder(const MergeData &data, const MailMergeSettings &settings);

// Groups of records that are duplicates of each other (every value equal,
// ignoring case and surrounding space), each group in data source order;
// Word's Find Duplicates.
[[nodiscard]] QList<QList<int>> duplicateGroups(const MergeData &data);

} // namespace QindaOffice::Docx
