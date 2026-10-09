// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Excel import for QindaBase (M65, D-074) through calc_io — the reader
// QindaCalc opens workbooks with, so formulas arrive as their cached
// values. One sheet becomes a typed CsvTable (base_csv.h) for the ordinary
// import plan: the types come from the cells themselves, not from text —
//   all TRUE/FALSE                      → Yes/No
//   numbers with a date format          → Date, or Date & time when a
//                                          format shows a time or a value
//                                          has one
//   numbers with a currency format      → Currency (when every value fits
//                                          the file's decimals)
//   whole numbers                       → Whole number; other numbers →
//                                          Decimal number
//   any text cell                       → the CSV text rules (codes with
//                                          leading zeros stay text, …)
// .xls (BIFF) and .ods are not read; the caller says so.

#include "base_csv.h"

#include <QDateTime>
#include <QStringList>

#include <optional>

namespace QindaOffice::Apps::Base {

struct XlsxTable {
    QStringList sheets; // every sheet's name, in order
    QString sheet;      // the one read
    ::QindaOffice::Base::CsvTable table;
};

// Reads one sheet of an .xlsx file: `sheet` "" = the first one holding
// data; `currency` names the file's money symbol (formats that show it are
// money too). Rows wholly empty are skipped.
[[nodiscard]] std::optional<XlsxTable> readXlsxTable(const QString &path, const QString &sheet, bool headerRow,
                                                     const ::QindaOffice::Base::CurrencyFormat &currency,
                                                     QString *error);

// Excel format codes (exposed for tests).
[[nodiscard]] bool isDateFormatCode(const QString &code);
[[nodiscard]] bool hasTimeCode(const QString &code);
[[nodiscard]] bool isCurrencyFormatCode(const QString &code, const QString &symbol);
// Excel's 1900 serial (0 = 1899-12-30, as calc_engine counts) ↔ date-time.
[[nodiscard]] QDateTime dateTimeFromSerial(double serial);
[[nodiscard]] double serialFromDateTime(const QDateTime &when);

} // namespace QindaOffice::Apps::Base
