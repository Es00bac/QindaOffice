// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal: the direct package readers. QXlsx has no public model for
// conditional formats, data validations, the autofilter, comments or chart
// parts, so those are read straight out of the package XML after the cell
// walk, per sheet. Split: xlsx_read_xml.cpp (orchestration),
// xlsx_rules.h (conditional formats, validations and the dxf table, M78),
// xlsx_read_parts.cpp (autofilter/comments/tables/view),
// xlsx_read_drawings.cpp (the sheet's drawing: charts and pictures); shared
// helpers in xlsx_parts.*.

#include "sheet_rules.h"
#include "xlsx_style.h"

#include <QHash>

namespace QXlsx {
class ZipReader;
}

namespace QindaOffice::Calc {
class Sheet;
class Workbook;
struct WorkbookSource;
}

namespace QindaOffice::Calc::IO {

struct RuleReadCounts;

// Reads conditional formats, data validations, the autofilter, cell
// comments, tables, the view, the print settings, scenarios and data
// tables for one sheet out of the package at `packagePath` (the normalized
// copy). `sheetIndex` selects the part; the palette resolves theme colours
// in dxfs and data bars; `sourceDxfs` is the package's dxf table
// (WorkbookSource::dxfs); `counts` adds how many rules were modelled and
// kept (M78, D-087).
void readSheetExtras(const QString &packagePath, qint32 sheetIndex, const ThemePalette &palette,
                     const QStringList &sourceDxfs, Sheet *sheet, RuleReadCounts *counts = nullptr);

// Reads the sheet's drawing (via its rels) and appends what it holds to the
// workbook: charts to `charts`, pictures to `images`, both with
// `sheetIndex` set.
void readSheetDrawings(const QString &packagePath, Workbook *wb, qint32 sheetIndex);

// A chart sheet (M69, D-078): the sheet becomes a chart sheet tied to its
// source part (the save carries the part, its drawing and chart verbatim),
// and its chart — read like a worksheet's — is the workbook chart on that
// sheet, sized from the drawing's absolute anchor.
void readChartSheet(const QString &packagePath, Workbook *wb, qint32 sheetIndex, const QString &sheetPart);

// The xlsx_read_parts.cpp readers (called by readSheetExtras).
[[nodiscard]] AutoFilter readAutoFilter(const QByteArray &xml);
[[nodiscard]] QHash<quint64, CellComment> readComments(QXlsx::ZipReader &zip,
                                                       const QString &sheetPart);
// Structured tables, resolved through the sheet's rels (table parts).
[[nodiscard]] QList<TableSpec> readTables(QXlsx::ZipReader &zip, const QString &sheetPart);
// Sheet view state (frozen pane, selection) from the sheet XML; the page
// setup is xlsx_print.h's since M78.
void readSheetViews(const QByteArray &xml, Sheet *sheet);

// Workbook-level reads (xlsx_read_book.cpp, M61/D-069).
// Defined names, BEFORE the cells (a formula resolves names when it is
// installed): workbook-wide plain ranges into the RangeRef table, every
// other name — sheet-scoped, constant, formula, hidden, commented,
// Excel's other built-ins — into Workbook::definedNames.
void readDefinedNames(const QByteArray &workbookXml, Workbook *wb);
// _xlnm print names (Print_Area/Print_Titles), AFTER the sheets exist.
void readPrintNames(const QByteArray &workbookXml, Workbook *wb);
// The source package's theme part and core document properties.
[[nodiscard]] WorkbookSource readWorkbookSource(QXlsx::ZipReader &zip);
// Validation lists that name a range get their items once every sheet is
// loaded (a list on Sheet1 may come from a range on Sheet3).
void resolveValidationLists(Workbook *wb);

} // namespace QindaOffice::Calc::IO
