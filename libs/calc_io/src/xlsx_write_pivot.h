// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PivotTables written as Excel's parts (M74, D-083): the plan made while
// the workbook is in hand (buildXlsx → planPivotParts: which caches and
// pivots are written from the model and which travel from the source
// package as they were), and the post-pass stage that writes them
// (applyPivotParts): pivotCacheDefinition + pivotCacheRecords per cache,
// a pivotTableDefinition per pivot, their relationships and content types,
// and the workbook's <pivotCaches>. The rendered cells are ordinary cells
// QXlsx already wrote.
//
// AGENT-CONTRACT: applyPivotParts runs in the post-pass before applyCarry;
// the carry stage (xlsx_carry_book.cpp) skips the source parts the plan
// marks replaced, carries the untouched ones byte for byte (refreshOnLoad
// added only to a cache whose source data changed) and merges its
// <pivotCache> entries into the <pivotCaches> written here.

#include "xlsx_manifest.h"

#include <QHash>
#include <QSet>

#include <functional>

namespace QindaOffice::Calc::IO {

void planPivotParts(const Workbook &wb, PatchManifest *manifest);
[[nodiscard]] bool applyPivotParts(QHash<QString, QByteArray> *parts, const PatchManifest &manifest, QString *error);

// xlsx_write_pivot_cache.cpp / xlsx_write_pivot_table.cpp. `formatId` maps
// a number format code to the numFmtId the written styles part gives it.
using PivotFormatId = std::function<int(const QString &)>;
// `listed`: fields whose shared items are written (every field a pivot
// puts on an axis, every field with text, every grouped field).
[[nodiscard]] QByteArray pivotCacheDefinitionXml(const PivotCache &cache, const QSet<qint32> &listed,
                                                 const PivotFormatId &formatId, const QString &recordsId);
[[nodiscard]] QByteArray pivotCacheRecordsXml(const PivotCache &cache, const QSet<qint32> &listed);
[[nodiscard]] QSet<qint32> pivotListedFields(const PivotCache &cache, const QList<const PivotTable *> &pivots);
[[nodiscard]] QByteArray pivotTableXml(const PivotTable &pivot, const PivotCache &cache, const PivotFormatId &formatId);

} // namespace QindaOffice::Calc::IO
