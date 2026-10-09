// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PivotCaches from the workbook (M74, D-083): reading a source — a range
// or an Excel table — into a cache, the grouping Excel stores as
// fieldGroup (number ranges, date parts, manual groups), and what a
// refresh carries from the old cache and the old pivot settings to the new
// ones. pivot_cache.cpp reads sources; pivot_group.cpp groups and remaps.

#include "pivot.h"

#include <optional>
#include <utility>

namespace QindaOffice::Calc {

class Workbook;

// The cells a source names now: its sheet index and range — a table's
// header and data rows (never its totals row). nullopt when it is gone.
[[nodiscard]] std::optional<std::pair<qint32, CellRange>> pivotSourceRange(const Workbook &book,
                                                                           const PivotSource &source);

// A cache of the source's current cells: one database field per column,
// shared items in order of first appearance (text without regard to case,
// as Excel merges "East" and "east"), the column's number format from its
// first value. nullopt, with Excel's message in `error`, when the source
// cannot feed a PivotTable (gone, or a header cell empty).
[[nodiscard]] std::optional<PivotCache> readPivotSource(const Workbook &book, const PivotSource &source,
                                                        QString *error = nullptr);

// True when `cache` still holds what its source holds now (a save marks a
// cache refreshOnLoad when it does not, D-083).
[[nodiscard]] bool pivotCacheCurrent(const Workbook &book, const PivotCache &cache);

// ---- grouping (pivot_group.cpp) ----

// The group a value falls in: its index into group.names (0 is "<start"
// for a range or date grouping, the last ">end"). `rawItem` is the value's
// shared item index (manual groups map items, not values).
[[nodiscard]] qint32 pivotGroupIndex(const PivotGroup &group, const Value &value, qint32 rawItem);

// Auto bounds from the base field's values, then the names, the way Excel
// spells its groupItems ("<1/5/2024", "Qtr1", "Jan", "5-14", ">35").
void pivotResolveGroup(PivotGroup &group, const PivotCache &cache);

// Groups `field` by number ranges (`by` Range) or by date parts (`parts`,
// finest first used in place; each coarser part a new field named "Years",
// "Quarters", … placed before it). Returns the fields that now show the
// grouping, coarsest first (the axis order Excel gives them), or empty with
// `error`. start/end NaN: automatic.
[[nodiscard]] QList<qint32> pivotGroupField(PivotCache &cache, qint32 field, const QList<PivotGroupBy> &parts,
                                            double start, double end, double interval, QString *error);
// Manual groups: a new field over `field` whose items are `members` (shared
// item indexes, all in "Group<N>") and every other item on its own.
// Returns the new field's index, or -1 with `error`.
[[nodiscard]] qint32 pivotGroupItems(PivotCache &cache, qint32 field, const QList<qint32> &members, QString *error);
// Removes a field's grouping: a made field goes, an in-place grouping is
// cleared together with the fields made from the same field's dates.
// Returns the removed field indexes, highest first (later fields moved
// down one for each; callers fix their pivots with pivotFieldRemoved).
QList<qint32> pivotUngroupField(PivotCache &cache, qint32 field);

// A refresh: `fresh`, read from the source, with `old`'s groupings applied
// again (made fields after the database fields, in their old order) and
// `old`'s id, source and file ties kept.
[[nodiscard]] PivotCache pivotRegroup(const PivotCache &old, PivotCache fresh);
// A pivot's settings carried from `old` to `fresh`: fields matched by name,
// items by value (group items by name); new items join at the end of a
// manual order, gone ones leave every list.
[[nodiscard]] PivotTable pivotRemap(const PivotTable &pivot, const PivotCache &old, const PivotCache &fresh);
// After a cache field was removed (pivotUngroupField): every index above it
// in the pivot moves down; references to it go.
void pivotFieldRemoved(PivotTable &pivot, qint32 removed);
// After a field was inserted at `index` (a new grouping field).
void pivotFieldInserted(PivotTable &pivot, const PivotCache &cache, qint32 index);

} // namespace QindaOffice::Calc
