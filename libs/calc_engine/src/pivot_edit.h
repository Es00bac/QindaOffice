// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PivotTables in a workbook (M74, D-083): making one, laying it out in its
// sheet's cells, refreshing, the four areas of the field list, moving,
// clearing, changing the source, and keeping every pivot's derived layout
// current after a load or an undo. pivot_edit.cpp and pivot_areas.cpp;
// GETPIVOTDATA and Show Details are pivot_query.cpp, structural edits
// pivot_structure.cpp.
//
// AGENT-CONTRACT: callers own undo (QindaCalc snapshots the workbook around
// each call, calc_undo.h). Every function that writes cells recalculates
// what reads them (GETPIVOTDATA, a SUM over the output) before returning.
// A pivot's output cells are plain values the engine wrote; a write into
// them is refused by the app (SpreadsheetDocument::refuseCellEdit), never
// here.

#include "pivot.h"

#include <optional>

namespace QindaOffice::Calc {

class Workbook;
struct StructureEdit;

struct PivotRef {
    qint32 sheet = -1;
    qint32 index = -1; // into Sheet::pivots()
    [[nodiscard]] bool isValid() const { return sheet >= 0 && index >= 0; }
    bool operator==(const PivotRef &) const = default;
};

enum class PivotArea : quint8 { Filters, Columns, Rows, Values };

// ---- finding ----
// The pivot whose output (filters included) covers `addr`; invalid when none.
[[nodiscard]] PivotRef pivotAt(const Workbook &book, qint32 sheet, const CellAddr &addr);
[[nodiscard]] const PivotTable *pivotOf(const Workbook &book, const PivotRef &ref);
[[nodiscard]] const PivotCache *pivotCacheOf(const Workbook &book, qint32 cacheId);
[[nodiscard]] qint32 nextPivotCacheId(const Workbook &book);
// "PivotTable<N>", unused in the workbook.
[[nodiscard]] QString nextPivotName(const Workbook &book);

// ---- making and showing ----
// Insert ▸ PivotTable: a new cache over `source` and an empty PivotTable
// (Excel's placeholder) whose body starts at `at` on `sheet`. Invalid, with
// Excel's message in `error`, when the source cannot feed a pivot or the
// place holds data.
PivotRef createPivotTable(Workbook &book, const PivotSource &source, qint32 sheet, const CellAddr &at,
                          QString *error = nullptr);
// Recomputes the pivot from its cache and writes its cells: the old output
// goes, the new one (values, number formats) is written, the columns widen
// to fit (Excel's "Autofit column widths on update"). False, with nothing
// changed, when the new output would cover cells that are not its own
// (content, another PivotTable, a table, merged cells) — unless `replace`
// (Excel's "There's already data … replace it?" answered Yes).
bool renderPivotTable(Workbook &book, const PivotRef &ref, QString *error = nullptr, bool replace = false);
// Refresh: re-reads the cache's source (groupings kept), carries every
// pivot's settings across and renders each one; all or nothing.
bool refreshPivotCache(Workbook &book, qint32 cacheId, QString *error = nullptr);
// Refresh All: every modelled cache; the messages of the ones that failed.
bool refreshAllPivots(Workbook &book, QStringList *errors = nullptr);
// Every modelled pivot's layout from its cache, writing nothing (after a
// load, an undo or a recovery: the cells are already there).
void rebuildPivotRenders(Workbook &book);
// The one pivot's layout, writing nothing.
void rebuildPivotRender(Workbook &book, const PivotRef &ref);

// ---- the field list (pivot_areas.cpp); render afterwards ----
// Puts `field` (a cache field, or kPivotValuesField) on `area` at
// `position` (-1: the end). On Filters, Columns or Rows it leaves the
// other two; on Values it adds a data field (Sum for numbers, Count
// otherwise — Excel's defaults), so a field may be summarized twice.
bool pivotPlaceField(PivotTable &pivot, const PivotCache &cache, qint32 field, PivotArea area, qint32 position = -1);
bool pivotRemoveField(PivotTable &pivot, PivotArea area, qint32 position);
bool pivotMoveField(PivotTable &pivot, const PivotCache &cache, PivotArea from, qint32 fromPosition, PivotArea to,
                    qint32 toPosition);
// The field list's checkbox: on puts a number field on Values and any
// other on Rows; off takes it off every area.
void pivotToggleField(PivotTable &pivot, const PivotCache &cache, qint32 field, bool on);
// Σ Values on an axis exactly when two or more data fields exist.
void pivotNormalize(PivotTable &pivot, const PivotCache &cache);
// The fields (and data field indexes) an area holds, in order.
[[nodiscard]] QList<qint32> pivotAreaFields(const PivotTable &pivot, PivotArea area);
// Groups the field (pivot_cache.h) and shows the new fields where it is.
bool pivotGroup(Workbook &book, const PivotRef &ref, qint32 field, const QList<PivotGroupBy> &parts, double start,
                double end, double interval, QString *error = nullptr);
bool pivotGroupSelection(Workbook &book, const PivotRef &ref, qint32 field, const QList<qint32> &items,
                         QString *error = nullptr);
bool pivotUngroup(Workbook &book, const PivotRef &ref, qint32 field, QString *error = nullptr);

// ---- the whole pivot ----
// Clear All: the fields leave every area; the pivot stays (empty).
bool clearPivotTable(Workbook &book, const PivotRef &ref, QString *error = nullptr);
// Deletes the pivot and its output (selecting it all and pressing Delete).
bool removePivotTable(Workbook &book, const PivotRef &ref);
bool movePivotTable(Workbook &book, const PivotRef &ref, qint32 sheet, const CellAddr &at, QString *error = nullptr);
bool changePivotSource(Workbook &book, const PivotRef &ref, const PivotSource &source, QString *error = nullptr);

// ---- GETPIVOTDATA and Show Details (pivot_query.cpp) ----
// The value the pivot covering (sheet, addr) shows for the data field
// named `dataField` (its name or its source field's) where each named
// field shows the named item; #REF! when that cell is not shown.
[[nodiscard]] Value pivotDataValue(const Workbook &book, qint32 sheet, const CellAddr &addr, const QString &dataField,
                                   const QList<QPair<QString, Value>> &pairs);
struct PivotDetails {
    QStringList headers;
    QStringList formats;
    QList<QList<Value>> rows;
};
// The source records behind a value cell (or a label: its whole item).
[[nodiscard]] std::optional<PivotDetails> pivotDetails(const Workbook &book, const PivotRef &ref, const CellAddr &addr);
// The data field and row / column fields a cell of the output belongs to.
struct PivotCellInfo {
    qint32 dataField = -1;  // a value cell's data field (or a data field name's); -1 elsewhere
    qint32 field = -1;      // the field a label, header or filter cell belongs to; a value's innermost row field
    qint32 item = -1;       // a label's display item
    bool label = false;     // an item label (row or column)
    bool page = false;      // a filter (page field) cell
    bool value = false;     // a value cell
    bool rows = true;       // the label or header is on the row axis
};
[[nodiscard]] PivotCellInfo pivotCellInfo(const Workbook &book, const PivotRef &ref, const CellAddr &addr);

// ---- structural edits (pivot_structure.cpp; called by structure.cpp) ----
// Moves the edited sheet's pivots with their cells and the caches' source
// ranges like references. A pivot the edit cuts through is dropped (the
// app refuses such an edit first).
void movePivotsForEdit(Workbook &book, const StructureEdit &edit);
// A sheet rename: range sources follow.
void requalifyPivotSources(Workbook &book, const QString &from, const QString &to);
// True when inserting (or removing) lines [at, at+count) on `sheet` would
// cut through a pivot's output (Excel refuses).
[[nodiscard]] bool pivotSplitBy(const Workbook &book, qint32 sheet, bool rows, qint32 at, qint32 count, bool insert);

} // namespace QindaOffice::Calc
