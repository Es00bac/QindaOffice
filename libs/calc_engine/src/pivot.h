// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PivotTables (M74, D-083): Excel's model in Excel's names, so a file's
// pivotCacheDefinition / pivotCacheRecords / pivotTableDefinition parts map
// onto it one to one (calc_io's xlsx_read_pivot / xlsx_write_pivot).
//
//   PivotCache  — a snapshot of the source rows: one field per source
//                 column (shared items + one item index per record), plus
//                 the fields grouping makes (Years, Quarters, Region2 …).
//                 A PivotTable shows its cache; only a refresh re-reads the
//                 source (Excel's model: an edit to the data does not move
//                 the pivot until it is refreshed).
//   PivotTable  — what is shown: the fields on the Filters (page), Columns,
//                 Rows and Values areas, per-field settings (item order,
//                 hidden items, sort, subtotals, filters), the layout
//                 options and the style. It lives on its sheet (Sheet::
//                 pivots) and its output lives in that sheet's cells.
//   PivotRender — the derived layout of the last computation (pivot_layout):
//                 every output cell with its role for the style painter, the
//                 lines Excel stores as rowItems / colItems, and the
//                 location metrics. Never saved; recomputed after a restore.
//
// Files: pivot_cache.cpp (caches from a range or a table, display items,
// grouping, refresh), pivot_compute.cpp (filters, axis trees, aggregation,
// sort), pivot_showas.cpp (Show Values As), pivot_layout.cpp (compact,
// outline, tabular), pivot_style.cpp (the built-in PivotStyles),
// pivot_edit.cpp (workbook operations: create, render into the sheet,
// refresh, areas), pivot_query.cpp (GETPIVOTDATA, Show Details),
// pivot_structure.cpp (structural edits and sheet renames).
//
// AGENT-CONTRACT: field indexes are cache field indexes everywhere (a
// PivotTable's `fields` is index-aligned with its cache's `fields`, as
// Excel's pivotFields are with cacheFields); item indexes are DISPLAY item
// indexes of that field (pivotDisplayItems: the group names of a grouped
// field, else its shared items). calc_io writes `order` as the
// pivotField's <items>, so rowItems / colItems <x v> are positions in it.

#include "value.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QStringList>
#include <QVector>

#include <memory>

namespace QindaOffice::Calc {

// ---- the cache ------------------------------------------------------------

enum class PivotGroupBy : quint8 { None, Range, Seconds, Minutes, Hours, Days, Months, Quarters, Years, Discrete };

// A cache field's grouping (Excel's fieldGroup): number ranges (rangePr
// startNum/endNum/groupInterval), date parts (rangePr groupBy, startDate,
// endDate; Days with groupInterval for n-day buckets) or manual groups
// (discretePr). `names` are Excel's groupItems, the "<start" and ">end"
// items of a range grouping included.
struct PivotGroup {
    PivotGroupBy by = PivotGroupBy::None;
    qint32 base = -1;       // the cache field whose values are grouped
    double start = 0;       // numbers: startNum; dates: the serial of startDate
    double end = 0;         // numbers: endNum; dates: the serial of endDate
    double interval = 1;    // numbers: groupInterval; Days: days per bucket
    bool autoStart = true;
    bool autoEnd = true;
    QList<qint32> discrete; // Discrete: base shared item → index into names
    QStringList names;
    bool operator==(const PivotGroup &) const = default;
};

struct PivotCacheField {
    QString name;
    QString numberFormat;     // the source column's format (cacheField numFmtId)
    QList<Value> items;       // shared items in cache order; records point here
    QVector<qint32> records;  // per record: index into items (database fields)
    bool database = true;     // false: made by grouping another field, no records
    PivotGroup group;         // by != None: grouped (in place, or a made field)
    bool operator==(const PivotCacheField &) const = default;
    [[nodiscard]] bool grouped() const { return group.by != PivotGroupBy::None; }
};

struct PivotSource {
    QString sheet;    // worksheetSource sheet (a range source)
    CellRange range;  // the header row first
    QString name;     // worksheetSource name: an Excel table (or a defined name)
    bool operator==(const PivotSource &) const = default;
};

struct PivotCache {
    qint32 id = 0; // unique in the workbook; the file's cacheId
    PivotSource source;
    QList<PivotCacheField> fields;
    qint32 recordCount = 0;
    bool refreshOnLoad = false;
    // false: a cache the engine does not model (OLAP, external, consolidation,
    // calculated fields) — its parts are carried as they were (D-075).
    bool modelled = true;
    // ---- file round trip (calc_io) ----
    QString sourcePart;         // the cache definition part it was read from
    QByteArray readFingerprint; // pivotCacheFingerprint as read: "untouched" at save
    QByteArray extensions;      // the definition's <extLst>, verbatim (slicers name its x14 id)
    bool operator==(const PivotCache &) const = default;
};

// ---- the PivotTable ---------------------------------------------------------

// Summarize Values By, in Excel's order of ST_DataConsolidateFunction names.
enum class PivotFunction : quint8 { Sum, Count, Average, Max, Min, Product, CountNums, StdDev, StdDevP, Var, VarP };
enum class PivotShowAs : quint8 {
    Normal, PercentOfTotal, PercentOfColumn, PercentOfRow, PercentOf, PercentOfParentRow, PercentOfParentColumn,
    PercentOfParent, Difference, PercentDifference, RunningTotal, PercentRunningTotal, RankAscending,
    RankDescending, Index
};
enum class PivotSort : quint8 { Manual, Ascending, Descending };
enum class PivotForm : quint8 { Compact, Outline, Tabular };

inline constexpr qint32 kPivotValuesField = -2;      // Σ Values on the Rows or Columns area
inline constexpr qint32 kPivotPreviousItem = 1048828; // Excel's baseItem (previous)
inline constexpr qint32 kPivotNextItem = 1048829;     // Excel's baseItem (next)

// A label, value or Top 10 filter on a row or column field (Excel's
// pivotFilter). Label filters compare the item captions (case-insensitive,
// * and ? wildcards); value filters and Top 10 compare a data field's
// totals per item.
struct PivotFilter {
    enum class Kind : quint8 { Label, Value, Top };
    enum class Op : quint8 {
        Equal, NotEqual, Greater, GreaterOrEqual, Less, LessOrEqual, BeginsWith, NotBeginsWith,
        EndsWith, NotEndsWith, Contains, NotContains, Between, NotBetween
    };
    enum class TopBy : quint8 { Items, Percent, Sum };
    Kind kind = Kind::Label;
    Op op = Op::Equal;
    QString text1;          // label filters
    QString text2;
    double value1 = 0;      // value filters; Top: the count, percent or sum
    double value2 = 0;
    qint32 dataField = 0;   // value and Top filters: which data field
    bool top = true;        // Top 10: top or bottom
    TopBy topBy = TopBy::Items;
    bool operator==(const PivotFilter &) const = default;
};

// Excel's pivotField: one per cache field, whether the field is shown or not.
struct PivotFieldSettings {
    QString caption;           // custom name; empty: the cache field's name
    QList<qint32> order;       // every display item, in display order (manual sort)
    QSet<qint32> hidden;       // items unchecked in the filter list
    PivotSort sort = PivotSort::Manual;
    qint32 sortData = -1;      // sort by this data field's totals; -1: by label
    bool subtotals = true;     // Automatic subtotal (defaultSubtotal)
    QList<PivotFunction> customSubtotals; // set: these subtotals instead of Automatic
    bool subtotalTop = true;   // compact and outline forms; tabular is always bottom
    bool insertBlankRow = false;
    bool repeatLabels = false; // Excel 2010's fillDownLabels
    bool showAllItems = false; // "Show items with no data"
    bool multipleSelection = false; // a page field showing several items (`hidden` hides the rest)
    qint32 pageItem = -1;      // a page field's one shown item; -1: (All)
    QList<PivotFilter> filters;
    bool operator==(const PivotFieldSettings &) const = default;
};

struct PivotDataField {
    QString name;              // "Sum of Sales"
    qint32 field = -1;
    PivotFunction function = PivotFunction::Sum;
    PivotShowAs showAs = PivotShowAs::Normal;
    qint32 baseField = -1;     // Show Values As base field
    qint32 baseItem = -1;      // a display item of baseField, or kPivotPrevious/NextItem
    QString numberFormat;      // empty: General
    bool operator==(const PivotDataField &) const = default;
};

struct PivotStyleInfo {
    QString name = QStringLiteral("PivotStyleLight16"); // empty: no style
    bool rowHeaders = true;
    bool colHeaders = true;
    bool rowStripes = false;
    bool colStripes = false;
    bool lastColumn = true;
    bool operator==(const PivotStyleInfo &) const = default;
};

struct PivotOptions {
    PivotForm form = PivotForm::Compact;
    // AGENT-NOTE: OOXML's names, checked against LibreOffice's import of
    // Excel files: rowGrandTotals is the Grand Total ROW under the row items
    // (rowItems' t="grand"), colGrandTotals the Grand Total COLUMN after the
    // column items. Excel's ribbon calls the column "On for Rows Only".
    bool rowGrandTotals = true;
    bool colGrandTotals = true;
    bool showMissing = true;
    QString missingCaption;    // what an empty value cell shows
    bool showHeaders = true;   // "Row Labels" / field captions over the axes
    QString rowHeaderCaption;  // empty: "Row Labels"
    QString colHeaderCaption;  // empty: "Column Labels"
    QString grandTotalCaption; // empty: "Grand Total"
    QString dataCaption = QStringLiteral("Values");
    PivotStyleInfo style;
    bool operator==(const PivotOptions &) const = default;
};

struct PivotRender;
struct PivotResult;

struct PivotTable {
    QString name;              // "PivotTable1", unique on its sheet
    qint32 cacheId = 0;
    CellAddr location;         // top-left of the body (Excel's location ref); filters sit above it
    QList<PivotFieldSettings> fields; // index-aligned with the cache's fields
    QList<qint32> rowFields;   // cache field indexes, kPivotValuesField for Σ Values
    QList<qint32> colFields;
    QList<qint32> pageFields;
    QList<PivotDataField> dataFields;
    PivotOptions options;
    // ---- read-only pivots, file round trip, derived state ----
    bool modelled = true;      // false: carried as read (OLAP, calculated items, …) and read-only
    QString note;              // why it is read-only
    CellRange extent;          // the cells it covers now, filters included
    QString sourcePart;        // the pivot table part it was read from; empty: made here
    QByteArray readFingerprint;
    QByteArray extensions;     // the definition's <extLst> entries the model does not own, verbatim
    // The read definition had <formats>, <conditionalFormats> or
    // <chartFormats>: kept while the pivot is untouched, left out (with a
    // save warning) once it is written from the model. Not in the fingerprint.
    bool customFormats = false;
    // Derived, never saved: the last computation and its layout (null until
    // rendered). Shared, so undo snapshots copy pointers, not grids.
    std::shared_ptr<const PivotResult> result;
    std::shared_ptr<const PivotRender> render;
};

// ---- the layout (pivot_layout.cpp) ---------------------------------------

// One rowItems / colItems line. `items` are positions in the axis fields'
// `order` lists (Σ Values: the data field index), from position `repeat`
// on; `path` is the whole member path (display items, data index for Σ
// Values), for GETPIVOTDATA and Show Details.
struct PivotLine {
    enum class Type : quint8 { Data, Subtotal, Grand, Blank };
    Type type = Type::Data;
    PivotFunction function = PivotFunction::Sum; // a custom subtotal's function
    bool custom = false;     // a custom subtotal (t="sum" …) rather than Automatic
    qint32 repeat = 0;       // Excel's r
    qint32 dataIndex = 0;    // Excel's i
    qint32 depth = 0;        // axis positions the line covers
    QVector<qint32> items;   // Excel's <x v>
    QVector<qint32> path;    // display item per covered position
    qint32 node = 0;         // the PivotResult axis node
    bool hasValues = true;   // false: a header line of a compact/outline group without top subtotals
    bool group = false;      // a compact/outline group's own line (lines of its items follow)
};

// What a cell of the output holds. The style a cell takes comes from its
// place (header rows, label columns, the line it is on — pivot_style.h).
enum class PivotCellKind : quint8 { PageLabel, PageValue, Header, RowLabel, ColumnLabel, Data };

struct PivotCell {
    Value value;
    QString numberFormat;
    PivotCellKind kind = PivotCellKind::Data;
    quint8 indent = 0;       // compact form: the label's level
    // Text the layout makes up ("Row Labels", "Grand Total", "East Total",
    // "(All)", "(blank)") rather than an item or a value: Excel writes it in
    // its own language, so a file's spelling may differ (xlsx_read_pivot).
    bool caption = false;
};

struct PivotRender {
    CellRange body;          // Excel's location ref
    CellRange extent;        // body plus the filter rows above it
    qint32 firstHeaderRow = 1;
    qint32 firstDataRow = 1;
    qint32 firstDataCol = 1;
    qint32 pageRows = 0;     // location rowPageCount
    QList<PivotLine> rowLines;
    QList<PivotLine> colLines;
    QHash<quint64, PivotCell> cells; // absolute CellAddr::key() → cell
    bool empty = false;      // no field placed yet: Excel's empty placeholder
    [[nodiscard]] const PivotCell *cellAt(const CellAddr &addr) const
    {
        const auto it = cells.constFind(addr.key());
        return it == cells.constEnd() ? nullptr : &it.value();
    }
};

// ---- helpers shared by the pivot files (pivot_cache.cpp) -------------------

// The items a field shows: its group names when grouped, else its shared items.
[[nodiscard]] QList<Value> pivotDisplayItems(const PivotCache &cache, qint32 field);
// Per record, the display item of `field` (grouped fields map their base's values).
[[nodiscard]] QVector<qint32> pivotRecordItems(const PivotCache &cache, qint32 field);
// The value a record holds in `field` (a made field: its group name).
[[nodiscard]] Value pivotRecordValue(const PivotCache &cache, qint32 field, qint32 record);
// Excel's natural item order: numbers, then text (no case), booleans,
// errors, blanks last; a grouped field keeps its group order.
[[nodiscard]] QList<qint32> pivotNaturalOrder(const PivotCache &cache, qint32 field);
// A caption for an item as a cell shows it ("(blank)" for an empty one).
[[nodiscard]] QString pivotItemCaption(const PivotCache &cache, qint32 field, qint32 item);
[[nodiscard]] QString pivotFieldCaption(const PivotCache &cache, const PivotTable &pivot, qint32 field);
[[nodiscard]] QString pivotFunctionName(PivotFunction function); // "Sum", "Count", "StdDev", …
// "Sum of Sales": Excel's default data field name, unique in the pivot.
[[nodiscard]] QString pivotDefaultDataName(const PivotCache &cache, const PivotTable &pivot, qint32 field,
                                           PivotFunction function);
// Deterministic summaries of what a save writes (the untouched test).
[[nodiscard]] QByteArray pivotCacheFingerprint(const PivotCache &cache);
[[nodiscard]] QByteArray pivotTableFingerprint(const PivotTable &pivot);

} // namespace QindaOffice::Calc
