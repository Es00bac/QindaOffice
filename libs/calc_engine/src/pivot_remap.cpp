// SPDX-License-Identifier: GPL-3.0-or-later
// What a refresh carries over (pivot_cache.h; M74, D-083): the old cache's
// groups re-applied to the fresh one (pivotRegroup), a pivot's settings
// moved onto the fresh cache by field name and item value (pivotRemap),
// and a pivot's field indexes kept right when a cache field is removed or
// a made group field is inserted (pivotFieldRemoved / pivotFieldInserted).

#include "pivot_cache.h"

#include "functions/functions.h"

#include <QHash>
#include <QSet>

namespace QindaOffice::Calc {

namespace {

QString keyOf(const Value &value)
{
    if (const auto *n = std::get_if<double>(&value)) {
        return QStringLiteral("n") + QString::number(*n, 'g', 17);
    }
    if (const auto *t = std::get_if<QString>(&value)) {
        return QStringLiteral("s") + t->toCaseFolded();
    }
    return QStringLiteral("o") + toText(value);
}

// display item index in `to` for each display item of `from` (-1: gone).
QVector<qint32> mapItems(const QList<Value> &from, const QList<Value> &to)
{
    QHash<QString, qint32> index;
    for (qsizetype i = 0; i < to.size(); ++i) {
        index.insert(keyOf(to.at(i)), qint32(i));
    }
    QVector<qint32> out(from.size(), -1);
    for (qsizetype i = 0; i < from.size(); ++i) {
        out[i] = index.value(keyOf(from.at(i)), -1);
    }
    return out;
}

qint32 fieldNamed(const PivotCache &cache, const QString &name)
{
    for (qsizetype f = 0; f < cache.fields.size(); ++f) {
        if (cache.fields.at(f).name.compare(name, Qt::CaseInsensitive) == 0) {
            return qint32(f);
        }
    }
    return -1;
}

PivotFieldSettings defaultSettings(const PivotCache &cache, qint32 field)
{
    PivotFieldSettings settings;
    settings.order = pivotNaturalOrder(cache, field);
    return settings;
}

} // namespace

PivotCache pivotRegroup(const PivotCache &old, PivotCache fresh)
{
    fresh.id = old.id;
    fresh.source = old.source;
    fresh.modelled = old.modelled;
    fresh.sourcePart = old.sourcePart;
    fresh.readFingerprint = old.readFingerprint;
    fresh.extensions = old.extensions;
    fresh.refreshOnLoad = false;
    for (qsizetype index = 0; index < old.fields.size(); ++index) {
        const PivotCacheField &field = old.fields.at(index);
        if (!field.formula.isEmpty()) { fresh.fields.append(field); continue; }
        if (!field.grouped()) {
            continue;
        }
        const qint32 oldBase = field.group.base >= 0 ? field.group.base : qint32(index);
        const qint32 base = oldBase >= 0 && oldBase < old.fields.size() ? fieldNamed(fresh, old.fields.at(oldBase).name)
                                                                         : -1;
        if (base < 0) {
            continue; // its base column is gone
        }
        PivotGroup group = field.group;
        group.base = base;
        if (group.by == PivotGroupBy::Discrete) {
            // Items keep their groups by value; new items stand alone.
            const QVector<qint32> map = mapItems(fresh.fields.at(base).items, old.fields.at(oldBase).items);
            group.discrete.clear();
            for (qsizetype i = 0; i < map.size(); ++i) {
                const qint32 was = map.at(i);
                if (was >= 0 && was < field.group.discrete.size()) {
                    group.discrete.append(field.group.discrete.at(was));
                } else {
                    group.names.append(pivotItemCaption(fresh, base, qint32(i)));
                    group.discrete.append(qint32(group.names.size() - 1));
                }
            }
        } else {
            pivotResolveGroup(group, fresh);
        }
        if (field.database) {
            fresh.fields[base].group = group;
        } else {
            PivotCacheField made = field;
            made.group = group;
            fresh.fields.append(made);
        }
    }
    return fresh;
}

PivotTable pivotRemap(const PivotTable &pivot, const PivotCache &old, const PivotCache &fresh)
{
    PivotTable out = pivot;
    QVector<qint32> fieldMap(old.fields.size(), -1);
    for (qsizetype f = 0; f < old.fields.size(); ++f) {
        fieldMap[f] = fieldNamed(fresh, old.fields.at(f).name);
    }
    const auto mapField = [&fieldMap](qint32 f) { return f >= 0 && f < fieldMap.size() ? fieldMap.at(f) : f; };
    out.fields.clear();
    QList<QVector<qint32>> itemMaps; // per fresh field: old display item → fresh display item
    for (qsizetype f = 0; f < fresh.fields.size(); ++f) {
        const qint32 from = qint32(fieldMap.indexOf(qint32(f)));
        if (from < 0 || from >= pivot.fields.size()) {
            out.fields.append(defaultSettings(fresh, qint32(f)));
            itemMaps.append(QVector<qint32>());
            continue;
        }
        const QVector<qint32> map = mapItems(pivotDisplayItems(old, from), pivotDisplayItems(fresh, qint32(f)));
        PivotFieldSettings settings = pivot.fields.at(from);
        QList<qint32> order;
        for (const qint32 item : std::as_const(settings.order)) {
            if (item >= 0 && item < map.size() && map.at(item) >= 0 && !order.contains(map.at(item))) {
                order.append(map.at(item));
            }
        }
        for (const qint32 item : pivotNaturalOrder(fresh, qint32(f))) {
            if (!order.contains(item)) {
                order.append(item); // a new item joins at the end
            }
        }
        settings.order = order;
        QSet<qint32> hidden;
        for (const qint32 item : std::as_const(settings.hidden)) {
            if (item >= 0 && item < map.size() && map.at(item) >= 0) {
                hidden.insert(map.at(item));
            }
        }
        settings.hidden = hidden;
        settings.pageItem = settings.pageItem >= 0 && settings.pageItem < map.size() ? map.at(settings.pageItem) : -1;
        out.fields.append(settings);
        itemMaps.append(map);
    }
    const auto mapAxis = [&](const QList<qint32> &axis) {
        QList<qint32> mapped;
        for (const qint32 f : axis) {
            const qint32 to = f == kPivotValuesField ? f : mapField(f);
            if (to == kPivotValuesField || to >= 0) {
                mapped.append(to);
            }
        }
        return mapped;
    };
    out.rowFields = mapAxis(pivot.rowFields);
    out.colFields = mapAxis(pivot.colFields);
    out.pageFields = mapAxis(pivot.pageFields);
    out.dataFields.clear();
    for (PivotDataField data : pivot.dataFields) {
        data.field = mapField(data.field);
        if (data.field < 0) {
            continue;
        }
        if (data.baseField >= 0) {
            const qint32 base = mapField(data.baseField);
            if (data.baseItem >= 0 && data.baseItem != kPivotPreviousItem && data.baseItem != kPivotNextItem) {
                const QVector<qint32> &map = base >= 0 ? itemMaps.at(base) : QVector<qint32>();
                data.baseItem = data.baseItem < map.size() ? map.at(data.baseItem) : -1;
            }
            data.baseField = base;
        }
        out.dataFields.append(data);
    }
    return out;
}

void pivotFieldRemoved(PivotTable &pivot, qint32 removed)
{
    if (removed >= 0 && removed < pivot.fields.size()) {
        pivot.fields.removeAt(removed);
    }
    const auto fix = [removed](QList<qint32> &axis) {
        axis.removeAll(removed);
        for (qint32 &f : axis) {
            f = f > removed ? f - 1 : f;
        }
    };
    fix(pivot.rowFields);
    fix(pivot.colFields);
    fix(pivot.pageFields);
    QVector<qint32> dataMap(pivot.dataFields.size(), -1);
    QList<PivotDataField> kept;
    for (qsizetype d = 0; d < pivot.dataFields.size(); ++d) {
        PivotDataField data = pivot.dataFields.at(d);
        if (data.field == removed) {
            continue;
        }
        data.field = data.field > removed ? data.field - 1 : data.field;
        if (data.baseField == removed) {
            data.baseField = -1;
            data.showAs = PivotShowAs::Normal;
        } else if (data.baseField > removed) {
            --data.baseField;
        }
        dataMap[d] = qint32(kept.size());
        kept.append(data);
    }
    pivot.dataFields = kept;
    for (PivotFieldSettings &settings : pivot.fields) {
        settings.sortData = settings.sortData >= 0 && settings.sortData < dataMap.size() ? dataMap.at(settings.sortData)
                                                                                         : -1;
        QList<PivotFilter> filters;
        for (PivotFilter filter : settings.filters) {
            if (filter.kind == PivotFilter::Kind::Value || filter.kind == PivotFilter::Kind::Top) {
                if (filter.dataField < 0 || filter.dataField >= dataMap.size() || dataMap.at(filter.dataField) < 0) {
                    continue;
                }
                filter.dataField = dataMap.at(filter.dataField);
            }
            filters.append(filter);
        }
        settings.filters = filters;
    }
    if (pivot.dataFields.size() < 2) {
        pivot.rowFields.removeAll(kPivotValuesField);
        pivot.colFields.removeAll(kPivotValuesField);
    }
}

void pivotFieldInserted(PivotTable &pivot, const PivotCache &cache, qint32 index)
{
    const auto fix = [index](QList<qint32> &axis) {
        for (qint32 &f : axis) {
            f = f >= index ? f + 1 : f;
        }
    };
    fix(pivot.rowFields);
    fix(pivot.colFields);
    fix(pivot.pageFields);
    for (PivotDataField &data : pivot.dataFields) {
        data.field = data.field >= index ? data.field + 1 : data.field;
        data.baseField = data.baseField >= index ? data.baseField + 1 : data.baseField;
    }
    pivot.fields.insert(qBound(qsizetype(0), qsizetype(index), pivot.fields.size()), defaultSettings(cache, index));
}

} // namespace QindaOffice::Calc
