// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The read half of M66 (D-075): a sheet's hyperlinks, outline and hidden
// lines, protection and code name; the workbook's structure lock, code
// name and VBA project; which cells are dynamic-array anchors; and the
// protection of cell formats (QXlsx reads no <protection>). Straight from
// the NORMALIZED package (xlsx_package.h), element names compared as local
// names.

#include "xlsx_read_cells.h"

#include <QSet>
#include <QVector>

namespace QXlsx {
class ZipReader;
}

namespace QindaOffice::Calc {
class Sheet;
class Workbook;
}

namespace QindaOffice::Calc::IO {

// Per xf of styles.xml's cellXfs, in order: bit 1 unlocked, bit 2 formula
// hidden (0 = Excel's default: locked, shown).
[[nodiscard]] QVector<quint8> readXfProtection(QXlsx::ZipReader &zip);

// The cm values (1-based cell-metadata blocks of xl/metadata.xml) that
// name a dynamic array: an XLDAPR block whose dynamicArrayProperties say
// fDynamic="1".
[[nodiscard]] QSet<int> readDynamicArrayCms(QXlsx::ZipReader &zip);

// Hyperlinks (external targets through the sheet's rels), row and column
// outline state, <outlinePr>, <sheetProtection> and the sheet's code name.
void readSheetFeatures(QXlsx::ZipReader &zip, const QString &sheetPart, Sheet *sheet);

// After the cell walk of a bulk load: dynamic-array anchors become
// dynamic formulas and the cached values in their spill ranges are marked
// spilled, so the end-of-load recalculation spills them afresh instead of
// finding them in the way (#SPILL!). A legacy array formula (no cm, or a
// cm that is not XLDAPR) becomes an array block over its ref (M69, D-078;
// Cell::arrayRange), its other cells marked the same way.
void markDynamicArrays(Workbook *wb, qint32 sheetIndex, const CellScan &scan, const QSet<int> &dynamicCms);

// <workbookProtection>, <workbookPr codeName> and a VBA project.
void readWorkbookFeatures(QXlsx::ZipReader &zip, Workbook *wb);

} // namespace QindaOffice::Calc::IO
