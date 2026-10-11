// SPDX-License-Identifier: GPL-3.0-or-later
// A PivotTable's numbers (pivot_compute.h; M74, D-083): record filters,
// axis trees, accumulators, value and Top 10 filters, items with no data,
// and the sort of every level. The label and value tests themselves are
// pivot_filters.cpp.
//
// AGENT-NOTE: Excel's order of work, which decides what totals include:
// page fields, hidden items and label filters drop records first; value
// and Top 10 filters are then evaluated once each, outermost row field
// first, against the totals of the data left — and drop the records of the
// items they remove, so subtotals and grand totals show only what is
// visible (Excel's default; "include filtered items in totals" is an OLAP
// option). Sorting comes last and never changes a number.

#include "pivot_compute.h"
#include "pivot_dates.h"
#include "pivot_calculated.h"

#include "number_format.h"

#include <algorithm>
#include <cmath>

namespace QindaOffice::Calc {

namespace {

struct Builder {
    const PivotCache &cache;
    const PivotTable &pivot;
    PivotResult &result;
    QHash<qint32, QVector<qint32>> items; // field → per-record display item
    QList<QList<Value>> dataItems;        // per data field: the values its records point at
    QList<QVector<qint32>> dataRecords;
    QVector<qint32> rowLeaf;
    QVector<qint32> colLeaf;

    void prepareData()
    {
        for (const PivotDataField &data : pivot.dataFields) {
            const qint32 f = data.field;
            if (f < 0 || f >= cache.fields.size()) {
                dataItems.append(QList<Value>());
                dataRecords.append(QVector<qint32>(cache.recordCount, -1));
                continue;
            }
            const PivotCacheField &field = cache.fields.at(f);
            if (field.database) {
                dataItems.append(field.items); // a grouped date field still sums its dates
                dataRecords.append(field.records);
            } else {
                dataItems.append(pivotDisplayItems(cache, f));
                dataRecords.append(pivotRecordItems(cache, f));
            }
        }
    }

    [[nodiscard]] Value dataValue(qsizetype d, qint32 record) const
    {
        const QVector<qint32> &records = dataRecords.at(d);
        const qint32 item = record < records.size() ? records.at(record) : -1;
        return item >= 0 && item < dataItems.at(d).size() ? dataItems.at(d).at(item) : Value{};
    }

    void build(PivotAxis &axis, QVector<qint32> &leafOf)
    {
        axis.nodes.clear();
        axis.byPath.clear();
        axis.nodes.append(PivotAxisNode{});
        QList<QHash<qint32, qint32>> childIndex{{}};
        QList<QVector<qint32>> fieldItems;
        for (const qint32 f : std::as_const(axis.fields)) {
            fieldItems.append(items.value(f));
        }
        leafOf.fill(-1, cache.recordCount);
        for (qint32 r = 0; r < cache.recordCount; ++r) {
            if (!result.included.at(r)) {
                continue;
            }
            qint32 node = 0;
            for (qsizetype k = 0; k < fieldItems.size(); ++k) {
                const qint32 item = r < fieldItems.at(k).size() ? fieldItems.at(k).at(r) : 0;
                qint32 child = childIndex.at(node).value(item, -1);
                if (child < 0) {
                    child = qint32(axis.nodes.size());
                    PivotAxisNode made;
                    made.parent = node;
                    made.level = qint32(k);
                    made.item = item;
                    made.path = axis.nodes.at(node).path;
                    made.path.append(item);
                    axis.nodes.append(made);
                    childIndex.append(QHash<qint32, qint32>());
                    childIndex[node].insert(item, child);
                    axis.nodes[node].children.append(child);
                }
                node = child;
            }
            leafOf[r] = node;
        }
    }

    void aggregate()
    {
        result.accs.clear();
        QHash<quint64, QVector<PivotAcc>> sourceSums;
        const bool calculated = std::any_of(cache.fields.begin(), cache.fields.end(), [](const auto &f) { return !f.formula.isEmpty(); });
        const qsizetype dataCount = pivot.dataFields.size();
        for (qint32 r = 0; r < cache.recordCount; ++r) {
            if (!result.included.at(r)) {
                continue;
            }
            for (qint32 ra = rowLeaf.at(r); ra >= 0; ra = result.rows.nodes.at(ra).parent) {
                for (qint32 ca = colLeaf.at(r); ca >= 0; ca = result.cols.nodes.at(ca).parent) {
                    const quint64 key = PivotResult::key(ra, ca);
                    if (calculated) {
                        auto &sums = sourceSums[key];
                        sums.resize(cache.fields.size());
                        for (qsizetype f = 0; f < cache.fields.size(); ++f) {
                            const auto &field = cache.fields.at(f);
                            if (field.database) sums[f].add(field.items.value(field.records.value(r, -1)));
                        }
                    }
                    QVector<PivotAcc> &accs = result.accs[key];
                    if (accs.size() != dataCount) {
                        accs.resize(dataCount);
                    }
                    for (qsizetype d = 0; d < dataCount; ++d) {
                        accs[d].add(dataValue(d, r));
                    }
                }
            }
        }
        for (const auto &[key, sums] : sourceSums.asKeyValueRange()) {
            for (qsizetype d = 0; d < dataCount; ++d) {
                const qint32 field = pivot.dataFields.at(d).field;
                if (field >= 0 && field < cache.fields.size() && !cache.fields.at(field).formula.isEmpty()) {
                    result.accs[key][d] = PivotAcc{};
                    result.accs[key][d].add(pivotCalculatedValue(cache, field, sums));
                }
            }
        }
    }

    [[nodiscard]] Value total(bool rowsAxis, qint32 node, qint32 dataField) const
    {
        const auto it = result.accs.constFind(rowsAxis ? PivotResult::key(node, 0) : PivotResult::key(0, node));
        if (it == result.accs.constEnd() || dataField < 0 || dataField >= it->size()) {
            return {};
        }
        return pivotFinalize(it->at(dataField), pivot.dataFields.at(dataField).function);
    }

    // One value or Top 10 filter of the field at `level`; true when it removed items.
    bool applyValueFilter(bool rowsAxis, qint32 level, const PivotFilter &filter)
    {
        PivotAxis &axis = rowsAxis ? result.rows : result.cols;
        if (filter.dataField < 0 || filter.dataField >= pivot.dataFields.size()) {
            return false;
        }
        QSet<qint32> removed;
        for (const PivotAxisNode &parent : std::as_const(axis.nodes)) {
            if (parent.level != level - 1 || parent.children.isEmpty()) {
                continue;
            }
            QList<QPair<qint32, double>> scored;
            for (const qint32 child : parent.children) {
                const Value v = total(rowsAxis, child, filter.dataField);
                if (filter.kind == PivotFilter::Kind::Value) {
                    if (!pivotValueAccepts(filter, v)) {
                        removed.insert(child);
                    }
                } else if (const auto *n = std::get_if<double>(&v)) {
                    scored.append({child, *n});
                } else {
                    removed.insert(child); // no number: never among the top
                }
            }
            if (filter.kind != PivotFilter::Kind::Top) {
                continue;
            }
            std::stable_sort(scored.begin(), scored.end(), [&filter](const auto &a, const auto &b) {
                return filter.top ? a.second > b.second : a.second < b.second;
            });
            double whole = 0;
            for (const auto &entry : std::as_const(scored)) {
                whole += entry.second;
            }
            double running = 0;
            for (qsizetype i = 0; i < scored.size(); ++i) {
                bool keep = false;
                switch (filter.topBy) {
                case PivotFilter::TopBy::Items: keep = i < qsizetype(filter.value1); break;
                case PivotFilter::TopBy::Percent: keep = running < whole * filter.value1 / 100.0 - 1e-12; break;
                case PivotFilter::TopBy::Sum: keep = running < filter.value1 - 1e-12; break;
                }
                running += scored.at(i).second;
                if (!keep) {
                    removed.insert(scored.at(i).first);
                }
            }
        }
        if (removed.isEmpty()) {
            return false;
        }
        const QVector<qint32> &leaves = rowsAxis ? rowLeaf : colLeaf;
        for (qint32 r = 0; r < cache.recordCount; ++r) {
            for (qint32 node = leaves.at(r); result.included.at(r) && node > 0; node = axis.nodes.at(node).parent) {
                if (removed.contains(node)) {
                    result.included[r] = false;
                }
            }
        }
        return true;
    }

    void addEmptyItems(PivotAxis &axis)
    {
        for (qsizetype level = 0; level < axis.fields.size(); ++level) {
            const qint32 f = axis.fields.at(level);
            const PivotFieldSettings &settings = pivot.fields.at(f);
            if (!settings.showAllItems) {
                continue;
            }
            const qsizetype count = pivotDisplayItems(cache, f).size();
            const qsizetype nodes = axis.nodes.size();
            for (qsizetype p = 0; p < nodes; ++p) {
                if (axis.nodes.at(p).level != level - 1) {
                    continue;
                }
                QSet<qint32> present;
                for (const qint32 child : axis.nodes.at(p).children) {
                    present.insert(axis.nodes.at(child).item);
                }
                for (qint32 item = 0; item < count; ++item) {
                    if (present.contains(item) || settings.hidden.contains(item)) {
                        continue;
                    }
                    PivotAxisNode made;
                    made.parent = qint32(p);
                    made.level = qint32(level);
                    made.item = item;
                    made.path = axis.nodes.at(p).path;
                    made.path.append(item);
                    axis.nodes[p].children.append(qint32(axis.nodes.size()));
                    axis.nodes.append(made);
                }
            }
        }
    }

    void sort(PivotAxis &axis, bool rowsAxis)
    {
        QList<QHash<qint32, qint32>> manual;
        QList<QHash<qint32, qint32>> natural;
        for (const qint32 f : std::as_const(axis.fields)) {
            QHash<qint32, qint32> m;
            const QList<qint32> &order = pivot.fields.at(f).order;
            for (qsizetype i = 0; i < order.size(); ++i) {
                m.insert(order.at(i), qint32(i));
            }
            manual.append(m);
            QHash<qint32, qint32> n;
            const QList<qint32> nat = pivotNaturalOrder(cache, f);
            for (qsizetype i = 0; i < nat.size(); ++i) {
                n.insert(nat.at(i), qint32(i));
            }
            natural.append(n);
        }
        for (PivotAxisNode &node : axis.nodes) {
            if (node.children.size() < 2) {
                continue;
            }
            const qint32 level = node.level + 1;
            const PivotFieldSettings &settings = pivot.fields.at(axis.fields.at(level));
            const QHash<qint32, qint32> &nat = natural.at(level);
            const QHash<qint32, qint32> &man = manual.at(level);
            const auto naturalLess = [&](qint32 a, qint32 b) {
                return nat.value(axis.nodes.at(a).item, 1 << 30) < nat.value(axis.nodes.at(b).item, 1 << 30);
            };
            if (settings.sort == PivotSort::Manual) {
                std::stable_sort(node.children.begin(), node.children.end(), [&](qint32 a, qint32 b) {
                    const qint32 ia = man.value(axis.nodes.at(a).item, (1 << 29) + nat.value(axis.nodes.at(a).item));
                    const qint32 ib = man.value(axis.nodes.at(b).item, (1 << 29) + nat.value(axis.nodes.at(b).item));
                    return ia < ib;
                });
                continue;
            }
            const bool ascending = settings.sort == PivotSort::Ascending;
            std::stable_sort(node.children.begin(), node.children.end(), [&](qint32 a, qint32 b) {
                if (settings.sortData >= 0 && settings.sortData < pivot.dataFields.size()) {
                    const Value va = total(rowsAxis, a, settings.sortData);
                    const Value vb = total(rowsAxis, b, settings.sortData);
                    const bool na = isNumber(va);
                    const bool nb = isNumber(vb);
                    if (na != nb) {
                        return na; // items with no number go last both ways
                    }
                    if (na && std::get<double>(va) != std::get<double>(vb)) {
                        return ascending ? std::get<double>(va) < std::get<double>(vb)
                                         : std::get<double>(va) > std::get<double>(vb);
                    }
                    return naturalLess(a, b);
                }
                return ascending ? naturalLess(a, b) : naturalLess(b, a);
            });
        }
    }
};

// The axis's real fields (an index the cache or the pivot lacks is left
// out) and where Σ Values sits among its positions.
void setupAxis(const QList<qint32> &fields, qint32 dataCount, qsizetype fieldCount, PivotAxis &axis)
{
    for (const qint32 f : fields) {
        if (f == kPivotValuesField) {
            axis.valuesAt = dataCount >= 2 ? qint32(axis.fields.size()) : -1;
        } else if (f >= 0 && f < fieldCount) {
            axis.fields.append(f);
        }
    }
    if (axis.valuesAt > axis.fields.size()) {
        axis.valuesAt = qint32(axis.fields.size());
    }
}

} // namespace

PivotResult computePivot(const PivotCache &cache, const PivotTable &pivot)
{
    PivotResult result;
    result.dataCount = qint32(pivot.dataFields.size());
    Builder builder{cache, pivot, result, {}, {}, {}, {}, {}};
    const qsizetype fieldCount = qMin(cache.fields.size(), pivot.fields.size());
    setupAxis(pivot.rowFields, result.dataCount, fieldCount, result.rows);
    setupAxis(pivot.colFields, result.dataCount, fieldCount, result.cols);
    if (result.dataCount >= 2 && result.rows.valuesAt < 0 && result.cols.valuesAt < 0) {
        result.cols.valuesAt = qint32(result.cols.fields.size()); // Σ Values joins the columns, as in Excel
    }
    QList<qint32> pages;
    for (const qint32 f : pivot.pageFields) {
        if (f >= 0 && f < fieldCount) {
            pages.append(f);
        }
    }
    const QList<qint32> used = result.rows.fields + result.cols.fields + pages;
    for (const qint32 f : used) {
        if (!builder.items.contains(f)) {
            builder.items.insert(f, pivotRecordItems(cache, f));
        }
    }
    builder.prepareData();
    // Page fields, hidden items and label filters drop records first.
    result.included = QVector<bool>(cache.recordCount, true);
    const auto dropItems = [&](qint32 f, const QSet<qint32> &dropped) {
        const QVector<qint32> records = builder.items.value(f);
        for (qint32 r = 0; r < cache.recordCount && r < records.size(); ++r) {
            if (dropped.contains(records.at(r))) {
                result.included[r] = false;
            }
        }
    };
    for (const qint32 f : std::as_const(pages)) {
        const PivotFieldSettings &settings = pivot.fields.at(f);
        if (settings.multipleSelection) {
            dropItems(f, settings.hidden);
        } else if (settings.pageItem >= 0) {
            QSet<qint32> others;
            for (qsizetype i = 0; i < pivotDisplayItems(cache, f).size(); ++i) {
                if (qint32(i) != settings.pageItem) {
                    others.insert(qint32(i));
                }
            }
            dropItems(f, others);
        }
    }
    for (const qint32 f : result.rows.fields + result.cols.fields) {
        const PivotFieldSettings &settings = pivot.fields.at(f);
        QSet<qint32> dropped = settings.hidden;
        const QList<Value> items = pivotDisplayItems(cache, f);
        for (const PivotFilter &filter : settings.filters) {
            if (filter.kind != PivotFilter::Kind::Label && filter.kind != PivotFilter::Kind::Date) {
                continue;
            }
            for (qsizetype i = 0; i < items.size(); ++i) {
                if (!(filter.kind == PivotFilter::Kind::Date ? pivotDateAccepts(filter, items.at(i))
                        : pivotLabelAccepts(filter, items.at(i), pivotItemCaption(cache, f, qint32(i))))) {
                    dropped.insert(qint32(i));
                }
            }
        }
        dropItems(f, dropped);
    }
    const auto rebuild = [&] {
        builder.build(result.rows, builder.rowLeaf);
        builder.build(result.cols, builder.colLeaf);
        builder.aggregate();
    };
    rebuild();
    // Value and Top 10 filters, each once, outermost first.
    for (const bool rowsAxis : {true, false}) {
        const PivotAxis &axis = rowsAxis ? result.rows : result.cols;
        const QList<qint32> fields = axis.fields;
        for (qsizetype level = 0; level < fields.size(); ++level) {
            for (const PivotFilter &filter : pivot.fields.at(fields.at(level)).filters) {
                if ((filter.kind == PivotFilter::Kind::Value || filter.kind == PivotFilter::Kind::Top) && builder.applyValueFilter(rowsAxis, qint32(level), filter)) {
                    rebuild();
                }
            }
        }
    }
    builder.addEmptyItems(result.rows);
    builder.addEmptyItems(result.cols);
    builder.sort(result.rows, true);
    builder.sort(result.cols, false);
    for (PivotAxis *axis : {&result.rows, &result.cols}) {
        for (qsizetype n = 0; n < axis->nodes.size(); ++n) {
            axis->byPath.insert(axis->nodes.at(n).path, qint32(n));
        }
    }
    for (auto it = result.accs.constBegin(); it != result.accs.constEnd(); ++it) {
        QVector<Value> values(it->size());
        for (qsizetype d = 0; d < it->size(); ++d) {
            values[d] = pivotFinalize(it->at(d), pivot.dataFields.at(d).function);
        }
        result.raw.insert(it.key(), values);
    }
    applyPivotShowAs(result, cache, pivot);
    return result;
}

} // namespace QindaOffice::Calc
