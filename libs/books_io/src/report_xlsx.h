// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Every report as an Excel workbook (M63, D-071), next to its PDF and CSV:
// one sheet with the company, the title and the period above a bold header
// row, then the report's rows — indented labels, bold subtotals and totals
// with a rule above — and its notes. Written by the suite's own xlsx writer
// (calc_io, the code QindaCalc saves with), so the file is what QindaCalc
// and Excel open.
//
// AGENT-CONTRACT (cells, tst_books_io_reports): money cells are NUMBERS in
// major units with a currency number format of the company's currency
// ("$"#,##0.00 and its negative), never text; number and percent cells are
// numbers too (a percent as a fraction under 0.0%), dates are date serials
// under yyyy-mm-dd, labels text. No money is rounded: a minor-unit amount is
// written as amount ÷ 10^exponent and shown with exactly `exponent` digits.

#include "report_table.h"

#include <QString>

namespace QindaOffice::Books {

class ReportXlsx {
public:
    // The number format of a money cell in `currency` ("\"$\"#,##0.00;-\"$\"#,##0.00").
    [[nodiscard]] static QString moneyFormat(const QString &currency);
    static bool write(const ReportTable &table, const QString &path, QString *error = nullptr);
};

} // namespace QindaOffice::Books
