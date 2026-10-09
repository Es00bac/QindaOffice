// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QXlsx <-> calc_engine bridge (plan section 7.2), split in M33: the read
// half is xlsx_read.cpp, the write half xlsx_write.cpp, and the style/colour
// mapping xlsx_style.*. Reading reconstructs shared formulas by shifting the
// master cell's AST across the shared reference; writing stores formulas as
// "=..." text with the engine's evaluated value as the cached result, so
// consumers that do not recalculate on load still show correct data. Date
// cells convert through the Excel 1900 serial system; the 1904 variant
// shifts by 1462 when the source file declares it.

#include <workbook.h>

namespace QXlsx {
class Document;
}

namespace QindaOffice::Calc::IO {

// Reads a workbook from an xlsx document. Returns false with `error` set on
// structural failure; features the engine does not model are dropped,
// never fatal.
[[nodiscard]] bool readXlsx(QXlsx::Document &doc, Workbook *out, QString *error = nullptr);

// Convenience: read from a file path. `warnings` (M66, D-075) receives what
// the file holds that a save cannot keep — the app shows it after opening
// (honest, D-069); empty when everything travels.
[[nodiscard]] bool readXlsxFile(const QString &path, Workbook *out, QString *error = nullptr,
                                QStringList *warnings = nullptr);

// Writes a workbook into a new xlsx document (owned by the caller).
[[nodiscard]] std::unique_ptr<QXlsx::Document> writeXlsx(const Workbook &wb, QString *error = nullptr);

// Convenience: write to a file path. A path ending in .xlsm writes a macro-
// enabled workbook (keeping the source's VBA project, never running it);
// `warnings` (M66) receives what this save had to leave behind.
[[nodiscard]] bool writeXlsxFile(const Workbook &wb, const QString &path, QString *error = nullptr,
                                 QStringList *warnings = nullptr);

} // namespace QindaOffice::Calc::IO
