// SPDX-License-Identifier: GPL-3.0-or-later
// The PivotTable Fields list's four areas (pivot_edit.h; M74, D-083):
// Filters, Columns, Rows and Values, the way Excel's field list fills
// them — a field sits on one of Filters / Columns / Rows at a time, Values
// may summarize the same field more than once, and Σ Values joins an axis
// as soon as there are two data fields.

#include "pivot_edit.h"

#include "pivot_cache.h"

namespace QindaOffice::Calc {

namespace {

QList<qint32> *axisOf(PivotTable &pivot, PivotArea area)
{
    switch (area) {
    case PivotArea::Filters: return &pivot.pageFields;
    case PivotArea::Columns: return &pivot.colFields;
    case PivotArea::Rows: return &pivot.rowFields;
    case PivotArea::Values: return nullptr;
    }
    return nullptr;
}

// A date format names days or years outside quotes and [colour] tokens.
bool dateFormat(const QString &code)
{
    bool quoted = false;
    int bracket = 0;
    for (const QChar c : code) {
        if (c == QLatin1Char('"')) {
            quoted = !quoted;
        } else if (!quoted && c == QLatin1Char('[')) {
            ++bracket;
        } else if (!quoted && c == QLatin1Char(']')) {
            bracket = qMax(0, bracket - 1);
        } else if (!quoted && bracket == 0 && (c.toLower() == QLatin1Char('d') || c.toLower() == QLatin1Char('y'))) {
            return true;
        }
    }
    return false;
}

// Excel's defaults: a field of numbers is summed in Values; text, dates
// and anything mixed go to Rows (and are counted when put on Values).
bool numbersOnly(const PivotCache &cache, qint32 field)
{
    const PivotCacheField &f = cache.fields.at(field);
    if (!f.formula.isEmpty()) return true;
    if (f.grouped() || !f.database) {
        return false;
    }
    bool any = false;
    for (const Value &item : f.items) {
        if (isBlank(item)) {
            continue;
        }
        if (!isNumber(item)) {
            return false;
        }
        any = true;
    }
    return any;
}

// Data field indexes after a reorder or removal: `map` old → new (-1 gone).
void remapDataIndexes(PivotTable &pivot, const QVector<qint32> &map)
{
    for (PivotFieldSettings &settings : pivot.fields) {
        if (settings.sortData >= 0) {
            settings.sortData = settings.sortData < map.size() ? map.at(settings.sortData) : -1;
        }
        QList<PivotFilter> filters;
        for (PivotFilter filter : std::as_const(settings.filters)) {
            if (filter.kind == PivotFilter::Kind::Value || filter.kind == PivotFilter::Kind::Top) {
                filter.dataField = filter.dataField >= 0 && filter.dataField < map.size() ? map.at(filter.dataField) : -1;
                if (filter.dataField < 0) {
                    continue;
                }
            }
            filters.append(filter);
        }
        settings.filters = filters;
    }
}

void removeDataField(PivotTable &pivot, qsizetype position)
{
    QVector<qint32> map(pivot.dataFields.size(), -1);
    for (qsizetype d = 0, to = 0; d < map.size(); ++d) {
        if (d != position) {
            map[d] = qint32(to++);
        }
    }
    pivot.dataFields.removeAt(position);
    remapDataIndexes(pivot, map);
}

void insertDataField(PivotTable &pivot, const PivotCache &cache, qint32 field, qint32 position)
{
    PivotDataField data;
    data.field = field;
    data.function = (!cache.fields.at(field).formula.isEmpty() || numbersOnly(cache, field)) ? PivotFunction::Sum : PivotFunction::Count;
    data.name = pivotDefaultDataName(cache, pivot, field, data.function);
    const qsizetype at = position < 0 || position > pivot.dataFields.size() ? pivot.dataFields.size() : position;
    QVector<qint32> map(pivot.dataFields.size());
    for (qsizetype d = 0; d < map.size(); ++d) {
        map[d] = qint32(d >= at ? d + 1 : d);
    }
    pivot.dataFields.insert(at, data);
    remapDataIndexes(pivot, map);
}

} // namespace

QList<qint32> pivotAreaFields(const PivotTable &pivot, PivotArea area)
{
    switch (area) {
    case PivotArea::Filters: return pivot.pageFields;
    case PivotArea::Columns: return pivot.colFields;
    case PivotArea::Rows: return pivot.rowFields;
    case PivotArea::Values: {
        QList<qint32> fields;
        for (const PivotDataField &data : pivot.dataFields) {
            fields.append(data.field);
        }
        return fields;
    }
    }
    return {};
}

void pivotNormalize(PivotTable &pivot, const PivotCache &cache)
{
    const qsizetype count = cache.fields.size();
    while (pivot.fields.size() < count) {
        PivotFieldSettings settings;
        settings.order = pivotNaturalOrder(cache, qint32(pivot.fields.size()));
        pivot.fields.append(settings);
    }
    while (pivot.fields.size() > count) {
        pivot.fields.removeLast();
    }
    QSet<qint32> seen;
    for (QList<qint32> *axis : {&pivot.rowFields, &pivot.colFields, &pivot.pageFields}) {
        QList<qint32> kept;
        for (const qint32 f : std::as_const(*axis)) {
            const bool valid = f == kPivotValuesField ? axis != &pivot.pageFields : f >= 0 && f < count;
            if (valid && !seen.contains(f)) {
                kept.append(f);
                seen.insert(f);
            }
        }
        *axis = kept;
    }
    for (qsizetype d = pivot.dataFields.size() - 1; d >= 0; --d) {
        if (pivot.dataFields.at(d).field < 0 || pivot.dataFields.at(d).field >= count) {
            removeDataField(pivot, d);
        }
    }
    const bool shown = pivot.rowFields.contains(kPivotValuesField) || pivot.colFields.contains(kPivotValuesField);
    if (pivot.dataFields.size() >= 2 && !shown) {
        pivot.colFields.append(kPivotValuesField); // Excel puts Σ Values at the end of Columns
    } else if (pivot.dataFields.size() < 2) {
        pivot.rowFields.removeAll(kPivotValuesField);
        pivot.colFields.removeAll(kPivotValuesField);
    }
    for (qsizetype f = 0; f < pivot.fields.size(); ++f) {
        PivotFieldSettings &settings = pivot.fields[f];
        if (settings.order.isEmpty()) {
            settings.order = pivotNaturalOrder(cache, qint32(f));
        }
        if (!settings.customSubtotals.isEmpty()) {
            settings.subtotals = false; // custom subtotals replace Automatic (the file's defaultSubtotal="0")
        }
    }
    // A base field and item only where Show Values As reads them (the file
    // writes 0 for "none", so the model keeps -1 for it).
    for (PivotDataField &data : pivot.dataFields) {
        if (!cache.fields.at(data.field).formula.isEmpty()) data.function = PivotFunction::Sum;
        const PivotShowAs as = data.showAs;
        const bool item = as == PivotShowAs::PercentOf || as == PivotShowAs::Difference
            || as == PivotShowAs::PercentDifference;
        const bool field = item || as == PivotShowAs::PercentOfParent || as == PivotShowAs::RunningTotal
            || as == PivotShowAs::PercentRunningTotal || as == PivotShowAs::RankAscending
            || as == PivotShowAs::RankDescending;
        if (!field || data.baseField < 0 || data.baseField >= count) {
            data.baseField = -1;
        }
        if (!item || data.baseField < 0) {
            data.baseItem = -1;
        }
    }
}

bool pivotPlaceField(PivotTable &pivot, const PivotCache &cache, qint32 field, PivotArea area, qint32 position)
{
    pivotNormalize(pivot, cache);
    if (field == kPivotValuesField) {
        if (area == PivotArea::Values || area == PivotArea::Filters || pivot.dataFields.size() < 2) {
            return false; // Σ Values lives on Rows or Columns, with two data fields or more
        }
        pivot.rowFields.removeAll(kPivotValuesField);
        pivot.colFields.removeAll(kPivotValuesField);
        QList<qint32> *axis = axisOf(pivot, area);
        axis->insert(position < 0 || position > axis->size() ? axis->size() : position, kPivotValuesField);
        return true;
    }
    if (field < 0 || field >= cache.fields.size()) {
        return false;
    }
    if (area != PivotArea::Values && !cache.fields.at(field).formula.isEmpty()) return false;
    if (area == PivotArea::Values) {
        insertDataField(pivot, cache, field, position);
        pivotNormalize(pivot, cache);
        return true;
    }
    for (QList<qint32> *axis : {&pivot.rowFields, &pivot.colFields, &pivot.pageFields}) {
        axis->removeAll(field);
    }
    QList<qint32> *axis = axisOf(pivot, area);
    axis->insert(position < 0 || position > axis->size() ? axis->size() : position, field);
    if (area == PivotArea::Filters) {
        pivot.fields[field].pageItem = -1;
    }
    pivotNormalize(pivot, cache);
    return true;
}

bool pivotRemoveField(PivotTable &pivot, PivotArea area, qint32 position)
{
    if (area == PivotArea::Values) {
        if (position < 0 || position >= pivot.dataFields.size()) {
            return false;
        }
        removeDataField(pivot, position);
        if (pivot.dataFields.size() < 2) {
            pivot.rowFields.removeAll(kPivotValuesField);
            pivot.colFields.removeAll(kPivotValuesField);
        }
        return true;
    }
    QList<qint32> *axis = axisOf(pivot, area);
    if (position < 0 || position >= axis->size() || axis->at(position) == kPivotValuesField) {
        return false; // Σ Values goes with the data fields, not on its own
    }
    axis->removeAt(position);
    return true;
}

bool pivotMoveField(PivotTable &pivot, const PivotCache &cache, PivotArea from, qint32 fromPosition, PivotArea to,
                    qint32 toPosition)
{
    if (from == PivotArea::Values && to == PivotArea::Values) {
        if (fromPosition < 0 || fromPosition >= pivot.dataFields.size()) {
            return false;
        }
        const qsizetype target = qBound(qsizetype(0), qsizetype(toPosition < 0 ? pivot.dataFields.size() - 1 : toPosition),
                                        pivot.dataFields.size() - 1);
        QVector<qint32> order;
        for (qsizetype d = 0; d < pivot.dataFields.size(); ++d) {
            order.append(qint32(d));
        }
        order.move(fromPosition, target);
        QVector<qint32> map(order.size());
        for (qsizetype i = 0; i < order.size(); ++i) {
            map[order.at(i)] = qint32(i);
        }
        pivot.dataFields.move(fromPosition, target);
        remapDataIndexes(pivot, map);
        return true;
    }
    if (from == PivotArea::Values) {
        if (fromPosition < 0 || fromPosition >= pivot.dataFields.size()) {
            return false;
        }
        const qint32 field = pivot.dataFields.at(fromPosition).field;
        if (!cache.fields.at(field).formula.isEmpty()) return false;
        removeDataField(pivot, fromPosition);
        return pivotPlaceField(pivot, cache, field, to, toPosition);
    }
    const QList<qint32> source = pivotAreaFields(pivot, from);
    if (fromPosition < 0 || fromPosition >= source.size()) {
        return false;
    }
    const qint32 field = source.at(fromPosition);
    if (to == PivotArea::Values) {
        if (field == kPivotValuesField) {
            return false;
        }
        axisOf(pivot, from)->removeAt(fromPosition);
        return pivotPlaceField(pivot, cache, field, PivotArea::Values, toPosition);
    }
    return pivotPlaceField(pivot, cache, field, to, toPosition);
}

void pivotToggleField(PivotTable &pivot, const PivotCache &cache, qint32 field, bool on)
{
    if (field < 0 || field >= cache.fields.size()) {
        return;
    }
    if (on) {
        const bool shown = pivot.rowFields.contains(field) || pivot.colFields.contains(field)
            || pivot.pageFields.contains(field) || pivotAreaFields(pivot, PivotArea::Values).contains(field);
        if (!shown) {
            const bool values = numbersOnly(cache, field) && !dateFormat(cache.fields.at(field).numberFormat);
            pivotPlaceField(pivot, cache, field, values ? PivotArea::Values : PivotArea::Rows);
        }
        return;
    }
    for (QList<qint32> *axis : {&pivot.rowFields, &pivot.colFields, &pivot.pageFields}) {
        axis->removeAll(field);
    }
    for (qsizetype d = pivot.dataFields.size() - 1; d >= 0; --d) {
        if (pivot.dataFields.at(d).field == field) {
            removeDataField(pivot, d);
        }
    }
    pivotNormalize(pivot, cache);
}

} // namespace QindaOffice::Calc
