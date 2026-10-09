// SPDX-License-Identifier: GPL-3.0-or-later
// The operations on a whole PivotTable (pivot_edit.h; M74, D-083): Clear
// All, delete, Move PivotTable, Change Data Source, Group and Ungroup —
// each prepared for every pivot it touches before any cell changes
// (pivot_edit_internal.h).

#include "pivot_edit.h"
#include "pivot_edit_internal.h"

#include "pivot_cache.h"
#include "sheet.h"
#include "workbook.h"

#include <QObject>

namespace QindaOffice::Calc {

using namespace PivotInternal;

bool clearPivotTable(Workbook &book, const PivotRef &ref, QString *error)
{
    const PivotTable *pivot = pivotOf(book, ref);
    const PivotCache *cache = pivot != nullptr ? pivotCacheOf(book, pivot->cacheId) : nullptr;
    if (cache == nullptr) {
        return false;
    }
    PivotTable cleared = *pivot;
    cleared.rowFields.clear();
    cleared.colFields.clear();
    cleared.pageFields.clear();
    cleared.dataFields.clear();
    for (qsizetype f = 0; f < cleared.fields.size(); ++f) {
        cleared.fields[f] = PivotFieldSettings{};
        cleared.fields[f].order = pivotNaturalOrder(*cache, qint32(f));
    }
    const std::optional<Prepared> prepared = prepare(book, ref, cleared, *cache, false, error);
    if (!prepared.has_value()) {
        return false;
    }
    commit(book, *prepared);
    return true;
}

bool removePivotTable(Workbook &book, const PivotRef &ref)
{
    const PivotTable *pivot = pivotOf(book, ref);
    if (pivot == nullptr) {
        return false;
    }
    Sheet &sheet = book.sheetAt(ref.sheet);
    const CellRange extent = pivot->extent;
    QSet<CellKey> cleared;
    for (qint32 row = extent.topLeft.row; extent.isValid() && row <= extent.bottomRight.row; ++row) {
        for (qint32 col = extent.topLeft.col; col <= extent.bottomRight.col; ++col) {
            if (sheet.clearCell({row, col})) {
                const CellKey key(ref.sheet, row, col);
                book.dependencies().updateCell(key, {});
                cleared.insert(key);
            }
            sheet.setStyleIdAt({row, col}, 0);
        }
    }
    sheet.mutablePivots().removeAt(ref.index);
    book.recalc(cleared);
    return true;
}

bool movePivotTable(Workbook &book, const PivotRef &ref, qint32 sheet, const CellAddr &at, QString *error)
{
    const PivotTable *pivot = pivotOf(book, ref);
    const PivotCache *cache = pivot != nullptr ? pivotCacheOf(book, pivot->cacheId) : nullptr;
    if (cache == nullptr || sheet < 0 || sheet >= book.sheetCount() || book.sheetAt(sheet).isChartSheet()
        || !at.isValid()) {
        if (error != nullptr) {
            *error = QObject::tr("Destination reference is not valid.");
        }
        return false;
    }
    PivotTable moved = *pivot;
    moved.location = at;
    const bool sameSheet = sheet == ref.sheet;
    PivotRef target = ref;
    if (!sameSheet) {
        // On another sheet nothing there is its own; it joins that sheet's
        // list first so the checks can tell it from the others.
        moved.extent = {};
        target = {sheet, qint32(book.sheetAt(sheet).pivots().size())};
        book.sheetAt(sheet).mutablePivots().append(moved);
    }
    const std::optional<Prepared> prepared = prepare(book, target, moved, *cache, false, error);
    if (!prepared.has_value()) {
        if (!sameSheet) {
            book.sheetAt(sheet).mutablePivots().removeLast();
        }
        return false;
    }
    if (!sameSheet) {
        removePivotTable(book, ref);
    }
    commit(book, *prepared);
    return true;
}

bool changePivotSource(Workbook &book, const PivotRef &ref, const PivotSource &source, QString *error)
{
    const PivotTable *pivot = pivotOf(book, ref);
    PivotCache *cache = pivot != nullptr ? mutableCache(book, pivot->cacheId) : nullptr;
    if (cache == nullptr || !cache->modelled) {
        return false;
    }
    if (!readPivotSource(book, source, error).has_value()) {
        return false;
    }
    // A new source is a cache of its own (the pivot leaves a shared one).
    PivotCache own = *cache;
    own.id = nextPivotCacheId(book);
    own.source = source;
    own.sourcePart.clear();
    own.readFingerprint.clear();
    own.extensions.clear();
    book.pivotCaches.append(own);
    const qint32 was = pivot->cacheId;
    book.sheetAt(ref.sheet).mutablePivots()[ref.index].cacheId = own.id;
    if (!refreshPivotCache(book, own.id, error)) {
        book.sheetAt(ref.sheet).mutablePivots()[ref.index].cacheId = was;
        book.pivotCaches.removeLast();
        return false;
    }
    return true;
}

bool pivotGroup(Workbook &book, const PivotRef &ref, qint32 field, const QList<PivotGroupBy> &parts, double start,
                double end, double interval, QString *error)
{
    const PivotTable *pivot = pivotOf(book, ref);
    PivotCache *cache = pivot != nullptr ? mutableCache(book, pivot->cacheId) : nullptr;
    if (cache == nullptr) {
        return false;
    }
    PivotCache grouped = *cache;
    const QList<qint32> shown = pivotGroupField(grouped, field, parts, start, end, interval, error);
    if (shown.isEmpty()) {
        return false;
    }
    // Every pivot of the cache: removed made fields go, new ones are added;
    // this one shows the new fields where the grouped one is.
    QList<Prepared> prepared;
    const QList<PivotRef> refs = pivotsOfCache(book, cache->id);
    for (const PivotRef &one : refs) {
        PivotTable changed = pivotRemap(*pivotOf(book, one), *cache, grouped);
        if (one == ref) {
            for (QList<qint32> *axis : {&changed.rowFields, &changed.colFields, &changed.pageFields}) {
                if (!axis->contains(field)) {
                    continue;
                }
                for (const qint32 f : shown) {
                    if (f != field) {
                        axis->removeAll(f);
                    }
                }
                const qsizetype base = axis->indexOf(field);
                for (qsizetype i = 0; i + 1 < shown.size(); ++i) {
                    axis->insert(base + i, shown.at(i));
                }
            }
        }
        pivotNormalize(changed, grouped);
        std::optional<Prepared> p = prepare(book, one, changed, grouped, false, error);
        if (!p.has_value()) {
            return false;
        }
        prepared.append(*p);
    }
    *cache = grouped;
    for (const Prepared &p : std::as_const(prepared)) {
        commit(book, p);
    }
    return true;
}

bool pivotGroupSelection(Workbook &book, const PivotRef &ref, qint32 field, const QList<qint32> &items, QString *error)
{
    const PivotTable *pivot = pivotOf(book, ref);
    PivotCache *cache = pivot != nullptr ? mutableCache(book, pivot->cacheId) : nullptr;
    if (cache == nullptr) {
        return false;
    }
    PivotCache grouped = *cache;
    const qint32 made = pivotGroupItems(grouped, field, items, error);
    if (made < 0) {
        return false;
    }
    QList<Prepared> prepared;
    for (const PivotRef &one : pivotsOfCache(book, cache->id)) {
        PivotTable changed = pivotRemap(*pivotOf(book, one), *cache, grouped);
        if (one == ref && !changed.rowFields.contains(made) && !changed.colFields.contains(made)) {
            for (QList<qint32> *axis : {&changed.rowFields, &changed.colFields}) {
                if (const qsizetype at = axis->indexOf(field); at >= 0) {
                    axis->insert(at, made); // the groups go right above their items
                }
            }
        }
        pivotNormalize(changed, grouped);
        std::optional<Prepared> p = prepare(book, one, changed, grouped, false, error);
        if (!p.has_value()) {
            return false;
        }
        prepared.append(*p);
    }
    *cache = grouped;
    for (const Prepared &p : std::as_const(prepared)) {
        commit(book, p);
    }
    return true;
}

bool pivotUngroup(Workbook &book, const PivotRef &ref, qint32 field, QString *error)
{
    const PivotTable *pivot = pivotOf(book, ref);
    PivotCache *cache = pivot != nullptr ? mutableCache(book, pivot->cacheId) : nullptr;
    if (cache == nullptr || field < 0 || field >= cache->fields.size() || !cache->fields.at(field).grouped()) {
        if (error != nullptr) {
            *error = QObject::tr("The field is not grouped.");
        }
        return false;
    }
    PivotCache ungrouped = *cache;
    const QList<qint32> removed = pivotUngroupField(ungrouped, field);
    QList<Prepared> prepared;
    for (const PivotRef &one : pivotsOfCache(book, cache->id)) {
        PivotTable changed = *pivotOf(book, one);
        for (const qint32 gone : removed) {
            pivotFieldRemoved(changed, gone);
        }
        if (field < changed.fields.size() && ungrouped.fields.at(field).database) {
            changed.fields[field].order = pivotNaturalOrder(ungrouped, field);
            changed.fields[field].hidden.clear();
            changed.fields[field].pageItem = -1;
        }
        pivotNormalize(changed, ungrouped);
        std::optional<Prepared> p = prepare(book, one, changed, ungrouped, false, error);
        if (!p.has_value()) {
            return false;
        }
        prepared.append(*p);
    }
    *cache = ungrouped;
    for (const Prepared &p : std::as_const(prepared)) {
        commit(book, p);
    }
    return true;
}

} // namespace QindaOffice::Calc
