// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QXlsx <-> calc_engine bridge (plan section 7.2). Reading reconstructs
// shared formulas by shifting the master cell's AST across the shared
// reference; writing stores formulas as "=..." text without cached values
// (consumers recalculate on load). Date cells convert through the Excel
// 1900 serial system; the 1904 variant shifts by 1462 when the source file
// declares it.

#include <workbook.h>

namespace QXlsx {
class Document;
}

namespace QindaOffice::Calc::IO {

// Reads a workbook from an xlsx document. Returns false with `error` set on
// structural failure; unknown features (charts, conditional formats) are
// dropped with a warning, never fatal.
[[nodiscard]] bool readXlsx(QXlsx::Document &doc, Workbook *out, QString *error = nullptr);

// Convenience: read from a file path.
[[nodiscard]] bool readXlsxFile(const QString &path, Workbook *out, QString *error = nullptr);

// Writes a workbook into a new xlsx document (owned by the caller).
[[nodiscard]] std::unique_ptr<QXlsx::Document> writeXlsx(const Workbook &wb, QString *error = nullptr);

// Convenience: write to a file path.
[[nodiscard]] bool writeXlsxFile(const Workbook &wb, const QString &path, QString *error = nullptr);

} // namespace QindaOffice::Calc::IO
