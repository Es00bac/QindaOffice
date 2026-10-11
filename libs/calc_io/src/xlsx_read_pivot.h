// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Excel PivotTables into the model on open (M74, D-083): every
// pivotCacheDefinition (+ pivotCacheRecords) a sheet's pivotTable parts
// reach becomes a PivotCache, every pivotTableDefinition a PivotTable on
// its sheet. The file's own cells stay what they are: the model's layout
// is computed and must agree with them (location, header rows, every
// value), or the pivot is kept as read — read-only, carried by the save as
// before (D-075) — with a note saying why. OLAP and Data Model pivots,
// external and consolidation sources, unsupported calculated formulas/items
// and collapsed items are kept that way. A cache marked
// refreshOnLoad is refreshed from its source, as Excel does on open.
//
// AGENT-CONTRACT: runs after the cell walk (readXlsxImpl), on the same
// normalized package path, before buildCarriedPackage; the part names it
// records (PivotTable::sourcePart, PivotCache::sourcePart) are names in
// that package and in the carried copy, which the save compares.

#include "pivot.h"

#include <QHash>
#include <QStringList>

#include <memory>

namespace QindaOffice::Ooxml {
class Package;
}

namespace QindaOffice::Calc {
class Workbook;
}

namespace QindaOffice::Calc::IO {

void readPivotTables(const QString &packagePath, Workbook *out, QStringList *warnings);

// xlsx_read_pivot_cache.cpp: one cache definition and its records. A cache
// the engine cannot model comes back with modelled = false and `reason`
// set ("uses an OLAP data source", …).
[[nodiscard]] PivotCache readPivotCachePart(const Ooxml::Package &package, const QString &part,
                                            const QHash<int, QString> &numberFormats, QString *reason);
// numFmtId → code for the custom formats of the package's styles part.
[[nodiscard]] QHash<int, QString> readPivotNumberFormats(const Ooxml::Package &package);
// A numFmtId as a format code: custom, built-in, or empty for General.
[[nodiscard]] QString pivotNumberFormat(int id, const QHash<int, QString> &numberFormats);

} // namespace QindaOffice::Calc::IO
