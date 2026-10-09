// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A PivotTable's numbers (M74, D-083): the records that pass the filters,
// the row and column axis trees (one node per shown item path, the root
// is the grand total), one accumulator per (row node, column node, data
// field), and the values after Show Values As. pivot_compute.cpp builds
// it (page fields, hidden items, label / value / Top 10 filters, Show
// items with no data, sort by label or value); pivot_showas.cpp turns the
// raw aggregates into what Show Values As asks for. pivot_layout.cpp lays
// it out in cells.

#include "pivot.h"

namespace QindaOffice::Calc {

// Every summary of one cell at once, so subtotals in another function
// (custom subtotals) need no second pass. Welford's running mean keeps the
// variance exact enough for StdDev and Var.
struct PivotAcc {
    qint64 count = 0;  // non-blank values (Count)
    qint64 nums = 0;   // numbers (Count Numbers, and every numeric summary)
    double sum = 0;
    double product = 1;
    double min = 0;
    double max = 0;
    double mean = 0;
    double m2 = 0;
    bool hasError = false;
    Err error = Err::NA;
    void add(const Value &value);
};
[[nodiscard]] Value pivotFinalize(const PivotAcc &acc, PivotFunction function);

struct PivotAxisNode {
    qint32 parent = -1;
    qint32 level = -1;      // -1: the root (grand total); else a position in PivotAxis::fields
    qint32 item = -1;       // the display item of fields[level]
    QList<qint32> children; // display order
    QVector<qint32> path;   // display items from fields[0] to this node
};

struct PivotAxis {
    QList<qint32> fields;   // the axis's cache fields in order, Σ Values left out
    qint32 valuesAt = -1;   // Σ Values' position among the axis positions; -1: not on this axis
    QList<PivotAxisNode> nodes; // nodes[0] is the root
    QHash<QVector<qint32>, qint32> byPath;
    [[nodiscard]] qint32 find(const QVector<qint32> &path) const { return byPath.value(path, -1); }
};

struct PivotResult {
    PivotAxis rows;
    PivotAxis cols;
    qint32 dataCount = 0;
    // (row node, column node) → one entry per data field. Absent: no record
    // reached that cell (it shows the empty-cell caption).
    QHash<quint64, QVector<PivotAcc>> accs;
    QHash<quint64, QVector<Value>> raw;    // each data field's own summary
    QHash<quint64, QVector<Value>> values; // after Show Values As
    QVector<bool> included;                // per record: passed every filter
    [[nodiscard]] static quint64 key(qint32 row, qint32 col) { return (quint64(quint32(row)) << 32) | quint32(col); }
    [[nodiscard]] const QVector<Value> *valuesAt(qint32 row, qint32 col) const
    {
        const auto it = values.constFind(key(row, col));
        return it == values.constEnd() ? nullptr : &it.value();
    }
};

[[nodiscard]] PivotResult computePivot(const PivotCache &cache, const PivotTable &pivot);

// A label filter's verdict on an item (its caption as shown), and a value
// filter's on a total (pivot_filters.cpp).
[[nodiscard]] bool pivotLabelAccepts(const PivotFilter &filter, const Value &item, const QString &caption);
[[nodiscard]] bool pivotValueAccepts(const PivotFilter &filter, const Value &value);

// pivot_showas.cpp: fills result.values from result.raw.
void applyPivotShowAs(PivotResult &result, const PivotCache &cache, const PivotTable &pivot);

// pivot_layout.cpp: the cells, rowItems / colItems lines and location of a
// computed pivot whose body starts at `location` (moved down when the
// filters above it would leave the sheet). Excel's three forms — compact
// (one label column, indented), outline (a column per row field, a group's
// label on its own row), tabular (a column per row field, a group's label
// on its first item's row, subtotals under it).
[[nodiscard]] PivotRender layoutPivot(const PivotCache &cache, const PivotTable &pivot, const PivotResult &result,
                                      const CellAddr &location);

} // namespace QindaOffice::Calc
