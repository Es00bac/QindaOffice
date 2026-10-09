// SPDX-License-Identifier: GPL-3.0-or-later
// Grouping for PivotCaches (pivot_cache.h; M74, D-083): the group a value
// falls in, Excel's groupItems names, grouping a field by number ranges,
// date parts or chosen items, and ungrouping. What a refresh carries from
// the old cache and pivot to the new is pivot_remap.cpp.

#include "pivot_cache.h"

#include "functions/functions.h"
#include "number_format.h"

#include <QObject>

#include <algorithm>
#include <cmath>
#include <limits>

namespace QindaOffice::Calc {

namespace {

bool datePart(PivotGroupBy by)
{
    return by == PivotGroupBy::Seconds || by == PivotGroupBy::Minutes || by == PivotGroupBy::Hours
        || by == PivotGroupBy::Days || by == PivotGroupBy::Months || by == PivotGroupBy::Quarters
        || by == PivotGroupBy::Years;
}

QString partFieldName(PivotGroupBy by)
{
    switch (by) {
    case PivotGroupBy::Seconds: return QObject::tr("Seconds");
    case PivotGroupBy::Minutes: return QObject::tr("Minutes");
    case PivotGroupBy::Hours: return QObject::tr("Hours");
    case PivotGroupBy::Days: return QObject::tr("Days");
    case PivotGroupBy::Months: return QObject::tr("Months");
    case PivotGroupBy::Quarters: return QObject::tr("Quarters");
    case PivotGroupBy::Years: return QObject::tr("Years");
    default: return QObject::tr("Group");
    }
}

QString dateText(double serial)
{
    return formatWithCode(std::floor(serial), QStringLiteral("m/d/yyyy")).text;
}

QString numberText(double value)
{
    return formatWithCode(value, QStringLiteral("General")).text;
}

// Range buckets between start and end (Excel: floor((end-start)/step)+1).
qint32 rangeBuckets(const PivotGroup &group)
{
    if (group.interval <= 0 || group.end < group.start) {
        return 1;
    }
    return qint32(std::floor((group.end - group.start) / group.interval + 1e-9)) + 1;
}

// 1-based day in a leap-year calendar (29-Feb is day 60 every year).
qint32 leapDayOfYear(const QDate &date)
{
    static const int kBefore[12] = {0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335};
    return kBefore[date.month() - 1] + date.day();
}

QString uniqueFieldName(const PivotCache &cache, const QString &base)
{
    QString name = base;
    for (int n = 2;; ++n) {
        bool taken = false;
        for (const PivotCacheField &field : cache.fields) {
            taken = taken || field.name.compare(name, Qt::CaseInsensitive) == 0;
        }
        if (!taken) {
            return name;
        }
        name = base + QString::number(n);
    }
}

} // namespace

qint32 pivotGroupIndex(const PivotGroup &group, const Value &value, qint32 rawItem)
{
    if (group.by == PivotGroupBy::Discrete) {
        return rawItem >= 0 && rawItem < group.discrete.size() ? group.discrete.at(rawItem) : 0;
    }
    const auto *number = std::get_if<double>(&value);
    const qint32 last = qint32(group.names.size()) - 1;
    if (number == nullptr || last < 1) {
        return 0;
    }
    const double v = *number;
    if (group.by == PivotGroupBy::Range) {
        if (v < group.start) {
            return 0;
        }
        const qint32 bucket = qint32(std::floor((v - group.start) / group.interval + 1e-9));
        return bucket >= rangeBuckets(group) ? last : qMin(bucket + 1, last);
    }
    if (v < group.start) {
        return 0;
    }
    if (v > group.end) {
        return last;
    }
    const QDate date = dateFromSerial(std::floor(v));
    const qint64 seconds = qint64(std::llround((v - std::floor(v)) * 86400.0));
    qint32 part = 0;
    switch (group.by) {
    case PivotGroupBy::Years: part = date.year() - dateFromSerial(std::floor(group.start)).year() + 1; break;
    case PivotGroupBy::Quarters: part = (date.month() - 1) / 3 + 1; break;
    case PivotGroupBy::Months: part = date.month(); break;
    case PivotGroupBy::Days:
        part = group.interval > 1 ? qint32((std::floor(v) - std::floor(group.start)) / group.interval) + 1
                                  : leapDayOfYear(date);
        break;
    case PivotGroupBy::Hours: part = qint32(seconds / 3600 % 24) + 1; break;
    case PivotGroupBy::Minutes: part = qint32(seconds / 60 % 60) + 1; break;
    case PivotGroupBy::Seconds: part = qint32(seconds % 60) + 1; break;
    default: break;
    }
    return qBound(1, part, qMax(1, last - 1));
}

void pivotResolveGroup(PivotGroup &group, const PivotCache &cache)
{
    if (group.by == PivotGroupBy::None || group.by == PivotGroupBy::Discrete) {
        return;
    }
    const qint32 base = group.base;
    double lo = std::numeric_limits<double>::max();
    double hi = std::numeric_limits<double>::lowest();
    bool integers = true;
    if (base >= 0 && base < cache.fields.size()) {
        for (const Value &item : cache.fields.at(base).items) {
            if (const auto *n = std::get_if<double>(&item)) {
                lo = qMin(lo, *n);
                hi = qMax(hi, *n);
                integers = integers && std::floor(*n) == *n;
            }
        }
    }
    if (lo > hi) {
        lo = hi = 0;
    }
    const bool dates = datePart(group.by);
    if (group.autoStart) {
        group.start = dates ? std::floor(lo) : lo;
    }
    if (group.autoEnd) {
        // Excel ends a date grouping the day after the last date.
        group.end = dates ? std::floor(hi) + 1 : hi;
    }
    if (group.interval <= 0) {
        group.interval = 1;
    }
    QStringList names;
    names.append(QLatin1Char('<') + (dates ? dateText(group.start) : numberText(group.start)));
    switch (group.by) {
    case PivotGroupBy::Range: {
        integers = integers && std::floor(group.start) == group.start && std::floor(group.interval) == group.interval;
        const qint32 count = rangeBuckets(group);
        for (qint32 k = 0; k < count; ++k) {
            const double from = group.start + k * group.interval;
            const double to = integers ? from + group.interval - 1 : from + group.interval;
            names.append(numberText(from) + QLatin1Char('-') + numberText(to));
        }
        names.append(QLatin1Char('>') + numberText(group.start + count * group.interval));
        group.names = names;
        return;
    }
    case PivotGroupBy::Years:
        for (int y = dateFromSerial(group.start).year(); y <= dateFromSerial(group.end).year(); ++y) {
            names.append(QString::number(y));
        }
        break;
    case PivotGroupBy::Quarters:
        for (int q = 1; q <= 4; ++q) {
            names.append(QObject::tr("Qtr%1").arg(q));
        }
        break;
    case PivotGroupBy::Months:
        for (int m = 1; m <= 12; ++m) {
            names.append(formatWithCode(serialFromDate(QDate(2000, m, 1)), QStringLiteral("mmm")).text);
        }
        break;
    case PivotGroupBy::Days:
        if (group.interval > 1) {
            for (double d = std::floor(group.start); d <= group.end; d += group.interval) {
                names.append(dateText(d) + QStringLiteral(" - ") + dateText(d + group.interval - 1));
            }
        } else {
            for (QDate d(2000, 1, 1); d.year() == 2000; d = d.addDays(1)) {
                names.append(formatWithCode(serialFromDate(d), QStringLiteral("d-mmm")).text);
            }
        }
        break;
    case PivotGroupBy::Hours:
        for (int h = 0; h < 24; ++h) {
            names.append(QStringLiteral("%1 %2").arg(h % 12 == 0 ? 12 : h % 12).arg(h < 12 ? QStringLiteral("AM")
                                                                                         : QStringLiteral("PM")));
        }
        break;
    case PivotGroupBy::Minutes:
    case PivotGroupBy::Seconds:
        for (int m = 0; m < 60; ++m) {
            names.append(QStringLiteral(":%1").arg(m, 2, 10, QLatin1Char('0')));
        }
        break;
    default:
        break;
    }
    names.append(QLatin1Char('>') + dateText(group.end));
    group.names = names;
}

QList<qint32> pivotGroupField(PivotCache &cache, qint32 field, const QList<PivotGroupBy> &parts, double start,
                              double end, double interval, QString *error)
{
    const auto refuse = [error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return QList<qint32>();
    };
    if (field < 0 || field >= cache.fields.size() || !cache.fields.at(field).database || parts.isEmpty()) {
        return refuse(QObject::tr("Cannot group that selection."));
    }
    for (const Value &item : cache.fields.at(field).items) {
        if (!isNumber(item)) {
            return refuse(QObject::tr("Cannot group that selection: the field holds text or blank cells; only "
                                      "numbers and dates group."));
        }
    }
    // Regrouping replaces what was there.
    for (const qint32 removed : pivotUngroupField(cache, field)) {
        if (removed < field) {
            --field;
        }
    }
    QList<PivotGroupBy> ordered = parts;
    std::sort(ordered.begin(), ordered.end()); // Seconds … Years: finest first
    ordered.erase(std::unique(ordered.begin(), ordered.end()), ordered.end());
    const auto makeGroup = [&](PivotGroupBy by) {
        PivotGroup group;
        group.by = by;
        group.base = field;
        group.autoStart = std::isnan(start);
        group.autoEnd = std::isnan(end);
        group.start = group.autoStart ? 0 : start;
        group.end = group.autoEnd ? 0 : end;
        group.interval = (by == PivotGroupBy::Range || by == PivotGroupBy::Days) && interval > 0 ? interval : 1;
        pivotResolveGroup(group, cache);
        return group;
    };
    QList<qint32> shown{field};
    cache.fields[field].group = makeGroup(ordered.first());
    for (qsizetype i = 1; i < ordered.size(); ++i) {
        PivotCacheField made;
        made.name = uniqueFieldName(cache, partFieldName(ordered.at(i)));
        made.database = false;
        made.group = makeGroup(ordered.at(i));
        cache.fields.append(made);
        shown.prepend(qint32(cache.fields.size() - 1));
    }
    return shown;
}

qint32 pivotGroupItems(PivotCache &cache, qint32 field, const QList<qint32> &members, QString *error)
{
    if (field < 0 || field >= cache.fields.size() || cache.fields.at(field).grouped() || members.isEmpty()) {
        if (error != nullptr) {
            *error = QObject::tr("Cannot group that selection.");
        }
        return -1;
    }
    const PivotCacheField &base = cache.fields.at(field);
    // An existing manual grouping of the same field takes a new group.
    qint32 made = -1;
    for (qsizetype f = 0; f < cache.fields.size(); ++f) {
        const PivotGroup &g = cache.fields.at(f).group;
        if (!cache.fields.at(f).database && g.by == PivotGroupBy::Discrete && g.base == field) {
            made = qint32(f);
        }
    }
    QVector<qint32> groupOf(base.items.size(), -1);
    QStringList names;
    if (made >= 0) {
        const PivotGroup &old = cache.fields.at(made).group;
        names = old.names;
        for (qsizetype i = 0; i < base.items.size() && i < old.discrete.size(); ++i) {
            groupOf[i] = old.discrete.at(i);
        }
    }
    int next = 1;
    while (names.contains(QObject::tr("Group%1").arg(next))) {
        ++next;
    }
    const QString groupName = QObject::tr("Group%1").arg(next);
    // Names follow the items' order: the new group where its first member is.
    QStringList ordered;
    QVector<qint32> assigned(base.items.size(), -1);
    for (const qint32 item : pivotNaturalOrder(cache, field)) {
        const QString name = members.contains(item) ? groupName
            : groupOf.at(item) >= 0                  ? names.at(groupOf.at(item))
                                                     : pivotItemCaption(cache, field, item);
        if (!ordered.contains(name)) {
            ordered.append(name);
        }
        assigned[item] = qint32(ordered.indexOf(name));
    }
    PivotGroup group;
    group.by = PivotGroupBy::Discrete;
    group.base = field;
    group.names = ordered;
    group.discrete = QList<qint32>(assigned.begin(), assigned.end());
    if (made >= 0) {
        cache.fields[made].group = group;
        return made;
    }
    PivotCacheField field2;
    field2.name = uniqueFieldName(cache, base.name + QLatin1Char('2'));
    field2.database = false;
    field2.group = group;
    cache.fields.append(field2);
    return qint32(cache.fields.size() - 1);
}

QList<qint32> pivotUngroupField(PivotCache &cache, qint32 field)
{
    QList<qint32> removed;
    if (field < 0 || field >= cache.fields.size()) {
        return removed;
    }
    if (!cache.fields.at(field).database) {
        cache.fields.removeAt(field);
        removed.append(field);
    } else {
        const bool inPlaceDates = datePart(cache.fields.at(field).group.by) || cache.fields.at(field).group.by
                                                                                    == PivotGroupBy::Range;
        cache.fields[field].group = PivotGroup{};
        // The date parts made from the same field go with it.
        for (qsizetype f = cache.fields.size() - 1; inPlaceDates && f >= 0; --f) {
            const PivotCacheField &made = cache.fields.at(f);
            if (!made.database && made.group.base == field && datePart(made.group.by)) {
                cache.fields.removeAt(f);
                removed.append(qint32(f));
            }
        }
    }
    // Groupings based on a field after a removed one now name it one lower.
    for (const qint32 gone : std::as_const(removed)) {
        for (PivotCacheField &f : cache.fields) {
            if (f.group.base > gone) {
                --f.group.base;
            }
        }
    }
    return removed;
}

} // namespace QindaOffice::Calc
