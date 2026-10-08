// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Reading QuickBooks IIF (Intuit Interchange Format) text (M63, D-071),
// the format QuickBooks Desktop exports lists and transactions in:
//  * tab-separated lines (CRLF or LF); blank lines are ignored;
//  * a header row `!TYPE<TAB>FIELD<TAB>…` names the columns of the data
//    rows `TYPE<TAB>value<TAB>…` that follow it; a later header for the
//    same type replaces it (files may change columns half-way);
//  * a field may be wrapped in double quotes, a doubled quote inside being
//    one quote (QuickBooks quotes values that hold commas or quotes);
//  * a transaction is one TRNS row, its SPL rows, then ENDTRNS;
//  * dates are MM/DD/YYYY (M/D/YY too), amounts plain decimals with a
//    leading minus — positive is a debit, negative a credit, and a
//    transaction's rows sum to zero;
//  * bytes are UTF-8 when they decode as such, else Windows-1252 (what
//    QuickBooks Desktop writes).
//
// The reader only splits the text into records; what the records mean is
// iif_import.h's business.

#include <QDate>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace QindaOffice::Books {

struct IifRecord {
    QString type;                   // "ACCNT", "CUST", "TRNS", "SPL", … (upper case)
    int line = 0;                   // 1-based line of the file
    QHash<QString, QString> fields; // header name (upper case) → value as written
    [[nodiscard]] QString value(const QString &field) const { return fields.value(field).trimmed(); }
    [[nodiscard]] bool has(const QString &field) const { return fields.contains(field); }
};

struct IifTransaction {
    IifRecord trns;
    QList<IifRecord> splits;
    int firstLine = 0;
    int lastLine = 0;
    QString error; // structural problem: no ENDTRNS, no TRNS, …
};

struct IifDocument {
    QList<IifRecord> lists;              // every non-transaction data row, in file order
    QList<IifTransaction> transactions;  // in file order
    QStringList problems;                // "line 12: a CUST row before any !CUST header"
};

namespace IifReader {

[[nodiscard]] IifDocument parse(const QByteArray &bytes);
// One line's fields, quotes resolved.
[[nodiscard]] QStringList splitLine(const QString &line);
// MM/DD/YYYY, M/D/YY (two-digit years are 2000–2069 and 1970–1999) or
// yyyy-MM-dd; invalid when unreadable.
[[nodiscard]] QDate parseDate(const QString &text);
// "INVOICE", "Bill Pmt -Check" → "INVOICE", "BILLPMTCHECK": upper case,
// spaces, '-', '_' and '/' removed — so the spellings QuickBooks versions
// and hand-made files use compare equal.
[[nodiscard]] QString normalizedType(const QString &text);

} // namespace IifReader
} // namespace QindaOffice::Books
