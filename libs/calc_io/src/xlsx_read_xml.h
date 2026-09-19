// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal: the direct package readers. QXlsx has no public model for
// conditional formats, data validations, the autofilter, comments or chart
// parts, so those are read straight out of the package XML after the cell
// walk, per sheet. Split: xlsx_read_xml.cpp (conditional formats + the dxf
// table + orchestration), xlsx_read_parts.cpp (validations/autofilter/
// comments), xlsx_read_charts.cpp (charts); shared helpers in xlsx_parts.*.

#include "sheet_rules.h"
#include "xlsx_style.h"

#include <QHash>

namespace QXlsx {
class ZipReader;
}

namespace QindaOffice::Calc {
class Sheet;
class Workbook;
}

namespace QindaOffice::Calc::IO {

// Reads conditional formats (+ the dxf table), data validations, the
// autofilter and cell comments for one sheet out of the package at
// `packagePath` (the normalized copy). `sheetIndex` selects the part; the
// palette resolves theme colours in dxfs and data bars.
void readSheetExtras(const QString &packagePath, Workbook *wb, qint32 sheetIndex,
                     const ThemePalette &palette, Sheet *sheet);

// Reads the sheet's chart parts (via its drawing rels) and appends them to
// the workbook's chart list with `sheetIndex` set.
void readSheetCharts(const QString &packagePath, Workbook *wb, qint32 sheetIndex);

// The xlsx_read_parts.cpp readers (called by readSheetExtras).
[[nodiscard]] QList<ValidationRule> readValidations(const QByteArray &xml, Workbook *wb,
                                                    qint32 sheetIndex, int *skipped);
[[nodiscard]] AutoFilter readAutoFilter(const QByteArray &xml);
[[nodiscard]] QHash<quint64, CellComment> readComments(QXlsx::ZipReader &zip,
                                                       const QString &sheetPart);
// Structured tables, resolved through the sheet's rels (table parts).
[[nodiscard]] QList<TableSpec> readTables(QXlsx::ZipReader &zip, const QString &sheetPart);

} // namespace QindaOffice::Calc::IO
