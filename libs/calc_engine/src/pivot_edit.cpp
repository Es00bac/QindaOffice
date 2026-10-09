// SPDX-License-Identifier: GPL-3.0-or-later
// PivotTables in a workbook (pivot_edit.h; M74, D-083): finding them,
// making one, writing a layout into the sheet, refreshing, and the
// operations on a whole pivot (clear, delete, move, change the source,
// group).
//
// AGENT-NOTE: writing a layout is two steps — prepare (compute, lay out,
// check the cells it would take) and commit (clear the old output, write
// the new) — so a refresh of a cache shared by several pivots is all or
// nothing: every pivot is prepared before any cell changes.

#include "pivot_edit.h"
#include "pivot_edit_internal.h"

#include "number_format.h"
#include "pivot_cache.h"
#include "pivot_compute.h"
#include "sheet.h"
#include "workbook.h"

#include <QObject>

#include <cmath>

namespace QindaOffice::Calc {

namespace PivotInternal {

PivotCache *mutableCache(Workbook &book, qint32 id)
{
    for (PivotCache &cache : book.pivotCaches) {
        if (cache.id == id) {
            return &cache;
        }
    }
    return nullptr;
}

} // namespace PivotInternal

namespace {

bool overlaps(const CellRange &a, const CellRange &b)
{
    return a.isValid() && b.isValid() && a.topLeft.row <= b.bottomRight.row && a.bottomRight.row >= b.topLeft.row
        && a.topLeft.col <= b.bottomRight.col && a.bottomRight.col >= b.topLeft.col;
}

QString rangeName(const Workbook &book, qint32 sheet, const CellRange &range)
{
    return quotedSheetName(book.sheetName(sheet)) + QLatin1Char('!') + addrText(range.topLeft) + QLatin1Char(':')
        + addrText(range.bottomRight);
}

// The style id for a cell holding only a number format (0 for General).
qint32 formatStyle(Workbook &book, const QString &format)
{
    if (format.isEmpty() || format.compare(QLatin1String("General"), Qt::CaseInsensitive) == 0) {
        return 0;
    }
    const CellStyle plain;
    for (qint32 id = 1; id < book.styleCount(); ++id) {
        const CellStyle *s = book.style(id);
        if (s != nullptr && s->numberFormat == format && s->fontFamily == plain.fontFamily && s->fontSize == 0
            && !s->bold && !s->italic && !s->underline && !s->strikeout && !s->fg && !s->bg && s->alignment == 0
            && s->vAlign == plain.vAlign && !s->wrap && s->borders == 0 && s->locked && !s->hiddenFormula) {
            return id;
        }
    }
    CellStyle style;
    style.numberFormat = format;
    return book.addStyle(style);
}

} // namespace

namespace PivotInternal {

std::optional<Prepared> prepare(const Workbook &book, const PivotRef &ref, const PivotTable &pivot,
                                const PivotCache &cache, bool replace, QString *error)
{
    const auto refuse = [error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return std::nullopt;
    };
    if (!pivot.modelled || !cache.modelled) {
        return refuse(QObject::tr("PivotTable “%1” is kept as it was read (%2); QindaCalc cannot change it.")
                          .arg(pivot.name, pivot.note));
    }
    auto result = std::make_shared<PivotResult>(computePivot(cache, pivot));
    auto render = std::make_shared<PivotRender>(layoutPivot(cache, pivot, *result, pivot.location));
    if (render->extent.bottomRight.row >= Sheet::kMaxRows || render->extent.bottomRight.col >= Sheet::kMaxCols) {
        return refuse(QObject::tr("The PivotTable does not fit on the sheet."));
    }
    const Sheet &sheet = book.sheetAt(ref.sheet);
    const CellRange &extent = render->extent;
    for (qsizetype i = 0; i < sheet.pivots().size(); ++i) {
        if (qint32(i) != ref.index && overlaps(sheet.pivots().at(i).extent, extent)) {
            return refuse(QObject::tr("A PivotTable report cannot overlap another PivotTable report."));
        }
    }
    for (const TableSpec &table : sheet.tables()) {
        if (overlaps(table.range, extent)) {
            return refuse(QObject::tr("A PivotTable report cannot overlap a table (“%1”).").arg(table.name));
        }
    }
    for (const CellRange &merge : sheet.merges()) {
        if (overlaps(merge, extent)) {
            return refuse(QObject::tr("A PivotTable report cannot go over merged cells (%1).")
                              .arg(rangeName(book, ref.sheet, merge)));
        }
    }
    // Cells with content that are not its own output are in the way: walk
    // whichever is smaller, the area or the sheet's cells.
    const auto inTheWay = [&](const CellAddr &addr, const Cell &cell) {
        return extent.contains(addr) && !(pivot.extent.isValid() && pivot.extent.contains(addr))
            && (!isBlank(cell.value) || cell.hasFormula());
    };
    bool blocked = false;
    if (!replace && extent.cellCount() < qint64(sheet.cells().size())) {
        for (qint32 row = extent.topLeft.row; !blocked && row <= extent.bottomRight.row; ++row) {
            for (qint32 col = extent.topLeft.col; !blocked && col <= extent.bottomRight.col; ++col) {
                const Cell *cell = sheet.cellAt({row, col});
                blocked = cell != nullptr && inTheWay({row, col}, *cell);
            }
        }
    } else if (!replace) {
        for (auto it = sheet.cells().constBegin(); !blocked && it != sheet.cells().constEnd(); ++it) {
            blocked = inTheWay({qint32(it.key() & 0xffffffffu), qint32(it.key() >> 32)}, it.value());
        }
    }
    if (blocked) {
        return refuse(QObject::tr("There's already data in %1. Move it, or place the PivotTable somewhere else.")
                          .arg(rangeName(book, ref.sheet, extent)));
    }
    PivotTable shown = pivot;
    shown.location = render->body.topLeft;
    shown.extent = render->extent;
    return Prepared{ref, shown, result, render};
}

// Clears the old output, writes the new one, widens columns; recalculates.
void commit(Workbook &book, const Prepared &prepared)
{
    Sheet &sheet = book.sheetAt(prepared.ref.sheet);
    const CellRange oldExtent = sheet.pivots().at(prepared.ref.index).extent;
    // AGENT-GUARD: the pivot takes its new result BEFORE any cell is
    // written, so the recalculation below finds GETPIVOTDATA's answers in it.
    PivotTable &target = sheet.mutablePivots()[prepared.ref.index];
    target = prepared.pivot;
    target.result = prepared.result;
    target.render = prepared.render;
    QSet<CellKey> cleared;
    const auto clearAt = [&](const CellAddr &addr) {
        const Cell *cell = sheet.cellAt(addr);
        if (cell != nullptr && (cell->hasFormula() || cell->arrayRange.isValid())) {
            book.setCellValue(prepared.ref.sheet, addr, Value{}); // lets go of formulas and arrays
        }
        if (sheet.clearCell(addr)) {
            const CellKey key(prepared.ref.sheet, addr.row, addr.col);
            book.dependencies().updateCell(key, {});
            cleared.insert(key);
        }
        sheet.setStyleIdAt(addr, 0);
    };
    const PivotRender &render = *prepared.render;
    for (const CellRange &area : {oldExtent, render.extent}) {
        for (qint32 row = area.topLeft.row; area.isValid() && row <= area.bottomRight.row; ++row) {
            for (qint32 col = area.topLeft.col; col <= area.bottomRight.col; ++col) {
                if (render.cells.contains(CellAddr{row, col}.key())) {
                    continue;
                }
                clearAt({row, col});
            }
        }
    }
    QHash<qint32, qint32> widths; // column → characters wanted
    book.beginBatch();
    for (auto it = render.cells.constBegin(); it != render.cells.constEnd(); ++it) {
        const CellAddr addr{qint32(it.key() & 0xffffffffu), qint32(it.key() >> 32)};
        const Cell *cell = sheet.cellAt(addr);
        if (cell == nullptr || cell->hasFormula() || cell->value != it->value) {
            book.setCellValue(prepared.ref.sheet, addr, it->value);
        }
        sheet.setStyleIdAt(addr, formatStyle(book, it->numberFormat));
        if (it->kind != PivotCellKind::PageLabel || addr.row >= render.body.topLeft.row) {
            const QString text = formatWithCode(it->value, it->numberFormat.isEmpty() ? QStringLiteral("General")
                                                                                      : it->numberFormat)
                                     .text;
            widths[addr.col] = qMax(widths.value(addr.col), qint32(text.size()) + 2 * it->indent);
        }
    }
    book.endBatch();
    // AGENT-GUARD: every cell of the output counts as edited, written or
    // not — GETPIVOTDATA reads the pivot through its corner cell, whose
    // text ("Sum of Sales") does not change when the numbers do.
    for (const CellRange &area : {oldExtent, render.extent}) {
        for (qint32 row = area.topLeft.row; area.isValid() && row <= area.bottomRight.row; ++row) {
            for (qint32 col = area.topLeft.col; col <= area.bottomRight.col; ++col) {
                cleared.insert(CellKey(prepared.ref.sheet, row, col));
            }
        }
    }
    book.recalc(cleared);
    // Excel's "Autofit column widths on update", estimated from the text
    // (the app's font metrics are not here): a column only widens.
    for (auto it = widths.constBegin(); it != widths.constEnd(); ++it) {
        const qreal wanted = qMin<qreal>(300.0, it.value() * 7.0 + 14.0);
        if (wanted > sheet.columnWidth(it.key())) {
            sheet.setColumnWidth(it.key(), wanted);
        }
    }
}

QList<PivotRef> pivotsOfCache(const Workbook &book, qint32 cacheId)
{
    QList<PivotRef> refs;
    for (qint32 s = 0; s < book.sheetCount(); ++s) {
        const QList<PivotTable> &pivots = book.sheetAt(s).pivots();
        for (qsizetype i = 0; i < pivots.size(); ++i) {
            if (pivots.at(i).cacheId == cacheId) {
                refs.append({s, qint32(i)});
            }
        }
    }
    return refs;
}

} // namespace PivotInternal

using namespace PivotInternal;

PivotRef pivotAt(const Workbook &book, qint32 sheet, const CellAddr &addr)
{
    if (sheet < 0 || sheet >= book.sheetCount()) {
        return {};
    }
    const QList<PivotTable> &pivots = book.sheetAt(sheet).pivots();
    for (qsizetype i = 0; i < pivots.size(); ++i) {
        if (pivots.at(i).extent.contains(addr)) {
            return {sheet, qint32(i)};
        }
    }
    return {};
}

const PivotTable *pivotOf(const Workbook &book, const PivotRef &ref)
{
    if (!ref.isValid() || ref.sheet >= book.sheetCount() || ref.index >= book.sheetAt(ref.sheet).pivots().size()) {
        return nullptr;
    }
    return &book.sheetAt(ref.sheet).pivots().at(ref.index);
}

const PivotCache *pivotCacheOf(const Workbook &book, qint32 cacheId)
{
    for (const PivotCache &cache : book.pivotCaches) {
        if (cache.id == cacheId) {
            return &cache;
        }
    }
    return nullptr;
}

qint32 nextPivotCacheId(const Workbook &book)
{
    qint32 id = 0;
    for (const PivotCache &cache : book.pivotCaches) {
        id = qMax(id, cache.id);
    }
    return id + 1;
}

QString nextPivotName(const Workbook &book)
{
    QSet<QString> taken;
    for (qint32 s = 0; s < book.sheetCount(); ++s) {
        for (const PivotTable &pivot : book.sheetAt(s).pivots()) {
            taken.insert(pivot.name.toCaseFolded());
        }
    }
    for (int n = 1;; ++n) {
        const QString name = QObject::tr("PivotTable%1").arg(n);
        if (!taken.contains(name.toCaseFolded())) {
            return name;
        }
    }
}

PivotRef createPivotTable(Workbook &book, const PivotSource &source, qint32 sheet, const CellAddr &at, QString *error)
{
    if (sheet < 0 || sheet >= book.sheetCount() || book.sheetAt(sheet).isChartSheet() || !at.isValid()) {
        if (error != nullptr) {
            *error = QObject::tr("Destination reference is not valid.");
        }
        return {};
    }
    std::optional<PivotCache> cache = readPivotSource(book, source, error);
    if (!cache.has_value()) {
        return {};
    }
    cache->id = nextPivotCacheId(book);
    PivotTable pivot;
    pivot.name = nextPivotName(book);
    pivot.cacheId = cache->id;
    pivot.location = at;
    for (qsizetype f = 0; f < cache->fields.size(); ++f) {
        PivotFieldSettings settings;
        settings.order = pivotNaturalOrder(*cache, qint32(f));
        pivot.fields.append(settings);
    }
    const PivotRef ref{sheet, qint32(book.sheetAt(sheet).pivots().size())};
    const std::optional<Prepared> prepared = prepare(book, ref, pivot, *cache, false, error);
    if (!prepared.has_value()) {
        return {};
    }
    book.pivotCaches.append(*cache);
    book.sheetAt(sheet).mutablePivots().append(prepared->pivot);
    commit(book, *prepared);
    return ref;
}

bool renderPivotTable(Workbook &book, const PivotRef &ref, QString *error, bool replace)
{
    const PivotTable *pivot = pivotOf(book, ref);
    const PivotCache *cache = pivot != nullptr ? pivotCacheOf(book, pivot->cacheId) : nullptr;
    if (cache == nullptr) {
        if (error != nullptr) {
            *error = QObject::tr("The PivotTable has no data to show.");
        }
        return false;
    }
    const std::optional<Prepared> prepared = prepare(book, ref, *pivot, *cache, replace, error);
    if (!prepared.has_value()) {
        return false;
    }
    commit(book, *prepared);
    return true;
}

bool refreshPivotCache(Workbook &book, qint32 cacheId, QString *error)
{
    PivotCache *cache = mutableCache(book, cacheId);
    if (cache == nullptr || !cache->modelled) {
        if (error != nullptr) {
            *error = QObject::tr("This PivotTable is kept as it was read; QindaCalc cannot refresh it.");
        }
        return false;
    }
    const std::optional<PivotCache> fresh = readPivotSource(book, cache->source, error);
    if (!fresh.has_value()) {
        return false;
    }
    const PivotCache regrouped = pivotRegroup(*cache, *fresh);
    QList<Prepared> prepared;
    for (const PivotRef &ref : pivotsOfCache(book, cacheId)) {
        PivotTable remapped = pivotRemap(*pivotOf(book, ref), *cache, regrouped);
        pivotNormalize(remapped, regrouped);
        std::optional<Prepared> one = prepare(book, ref, remapped, regrouped, false, error);
        if (!one.has_value()) {
            return false;
        }
        prepared.append(*one);
    }
    *cache = regrouped;
    for (const Prepared &one : std::as_const(prepared)) {
        commit(book, one);
    }
    return true;
}

bool refreshAllPivots(Workbook &book, QStringList *errors)
{
    bool all = true;
    QList<qint32> ids;
    for (const PivotCache &cache : book.pivotCaches) {
        if (cache.modelled && !pivotsOfCache(book, cache.id).isEmpty()) {
            ids.append(cache.id);
        }
    }
    for (const qint32 id : std::as_const(ids)) {
        QString error;
        if (!refreshPivotCache(book, id, &error)) {
            all = false;
            if (errors != nullptr) {
                errors->append(error);
            }
        }
    }
    return all;
}

void rebuildPivotRender(Workbook &book, const PivotRef &ref)
{
    const PivotTable *pivot = pivotOf(book, ref);
    const PivotCache *cache = pivot != nullptr ? pivotCacheOf(book, pivot->cacheId) : nullptr;
    if (pivot == nullptr || cache == nullptr || !pivot->modelled || !cache->modelled) {
        return;
    }
    auto result = std::make_shared<PivotResult>(computePivot(*cache, *pivot));
    auto render = std::make_shared<PivotRender>(layoutPivot(*cache, *pivot, *result, pivot->location));
    PivotTable &target = book.sheetAt(ref.sheet).mutablePivots()[ref.index];
    target.result = result;
    target.render = render;
    target.location = render->body.topLeft;
    target.extent = render->extent;
}

void rebuildPivotRenders(Workbook &book)
{
    for (qint32 s = 0; s < book.sheetCount(); ++s) {
        for (qsizetype i = 0; i < book.sheetAt(s).pivots().size(); ++i) {
            rebuildPivotRender(book, {s, qint32(i)});
        }
    }
}

} // namespace QindaOffice::Calc
