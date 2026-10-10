// SPDX-License-Identifier: GPL-3.0-or-later
// The report runner, part one: which group a row is in at each level (M86:
// Access's Group On intervals — prefix characters, a date part, a numeric
// range) and the order the rows print in. See base_report.h.
#include "base_report_p.h"

#include <QDateTime>
#include <QLocale>

#include <cmath>

namespace QindaOffice::Base::detail {

namespace {

QString s(const char *text)
{
    return QString::fromUtf8(text);
}

bool isNull(const QVariant &v)
{
    return !v.isValid() || v.isNull();
}

bool integral(FieldType t)
{
    return isWholeNumber(t) || t == FieldType::Currency || t == FieldType::YesNo;
}

QString valueKey(const QVariant &v)
{
    return v.metaType().id() == QMetaType::Double && std::floor(v.toDouble()) == v.toDouble()
               ? QString::number(qint64(v.toDouble()))
               : v.toString();
}

bool textual(FieldType t)
{
    return t == FieldType::Text || t == FieldType::LongText || t == FieldType::Hyperlink;
}

const QStringList kDateParts{s("year"), s("quarter"), s("month"), s("week"), s("day"), s("hour"), s("minute")};

// a / b rounded towards minus infinity (a range's lower bound).
qint64 floorDiv(qint64 a, qint64 b)
{
    const qint64 q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

QString plain(double value)
{
    return std::floor(value) == value && std::fabs(value) < 1e15 ? QString::number(qint64(value))
                                                                  : QString::number(value, 'f', 2);
}

} // namespace

void Accumulator::reset(int measures)
{
    rows = 0;
    values = QList<qint64>(measures, 0);
    exact = QList<qint64>(measures, 0);
    real = QList<double>(measures, 0.0);
    lowest = QList<QVariant>(measures);
    highest = QList<QVariant>(measures);
}

QString Runner::checkInterval(const ReportGroup &g, int column) const
{
    const ReportColumn &c = m_source.columns.at(column);
    const QString &by = g.interval.by;
    if (by == QLatin1String("value")) {
        return {};
    }
    if (by == QLatin1String("prefix")) {
        if (!textual(c.type) && c.lookup.isEmpty()) {
            return s("%1 is not text, so it is not grouped by its first characters; group it by its value or a "
                     "range.")
                .arg(c.label);
        }
        return g.interval.size >= 1 ? QString() : s("Group %1 by at least one character.").arg(c.label);
    }
    if (kDateParts.contains(by)) {
        return c.type == FieldType::Date || c.type == FieldType::DateTime
                   ? QString()
                   : s("%1 is not a date, so it is not grouped by %2.").arg(c.label, by);
    }
    if (by == QLatin1String("range")) {
        if (!isNumeric(c.type) || !c.lookup.isEmpty()) {
            return s("%1 is not a number, so it is not grouped by ranges.").arg(c.label);
        }
        return g.interval.size > 0 ? QString() : s("A range of %1 needs a width above 0.").arg(c.label);
    }
    return s("%1 cannot be grouped by \"%2\".").arg(c.label, by);
}

GroupKey Runner::keyOf(int level, const QVariant &value) const
{
    GroupKey k;
    if (isNull(value)) {
        k.null = true;
        k.id = s("\x01");
        k.text = s("(blank)");
        return k;
    }
    const int column = m_groupColumns.at(level);
    const ReportColumn &c = m_source.columns.at(column);
    const ReportInterval &interval = m_spec.groups.at(level).interval;
    const QString shown = m_source.display(column, value, m_currency);
    k.text = shown;
    k.id = valueKey(value);
    if (interval.by == QLatin1String("prefix")) {
        const QString prefix = shown.left(qMax(1, int(interval.size)));
        k.id = prefix.toLower();
        k.sort = prefix.toLower();
        k.text = prefix.size() == 1 ? prefix.toUpper() : prefix;
    } else if (kDateParts.contains(interval.by)) {
        const QString text = value.toString();
        QDateTime when = QDateTime::fromString(text.left(19), s("yyyy-MM-dd HH:mm:ss"));
        if (!when.isValid()) {
            when = QDateTime(QDate::fromString(text.left(10), Qt::ISODate), QTime(0, 0));
        }
        if (!when.isValid()) {
            return k; // not a date after all: the value itself
        }
        const QDate d = when.date();
        const QTime t = when.time();
        const QLocale c = QLocale::c();
        if (interval.by == QLatin1String("year")) {
            k.sort = qint64(d.year());
            k.text = QString::number(d.year());
        } else if (interval.by == QLatin1String("quarter")) {
            const int q = (d.month() - 1) / 3 + 1;
            k.sort = qint64(d.year()) * 10 + q;
            k.text = s("Q%1 %2").arg(q).arg(d.year());
        } else if (interval.by == QLatin1String("month")) {
            k.sort = qint64(d.year()) * 100 + d.month();
            k.text = s("%1 %2").arg(c.monthName(d.month())).arg(d.year());
        } else if (interval.by == QLatin1String("week")) {
            const QDate sunday = d.addDays(-(d.dayOfWeek() % 7)); // Access's weeks start on Sunday
            k.sort = sunday.toJulianDay();
            k.text = s("Week of %1").arg(displayText(FieldType::Date, sunday.toString(Qt::ISODate), m_currency));
        } else if (interval.by == QLatin1String("day")) {
            k.sort = d.toJulianDay();
            k.text = displayText(FieldType::Date, d.toString(Qt::ISODate), m_currency);
        } else if (interval.by == QLatin1String("hour")) {
            k.sort = d.toJulianDay() * 24 + t.hour();
            k.text = s("%1 %2:00").arg(d.toString(Qt::ISODate)).arg(t.hour(), 2, 10, QLatin1Char('0'));
        } else {
            k.sort = (d.toJulianDay() * 24 + t.hour()) * 60 + t.minute();
            k.text = s("%1 %2").arg(d.toString(Qt::ISODate), t.toString(s("HH:mm")));
        }
        k.id = k.sort.toString();
    } else if (interval.by == QLatin1String("range")) {
        if (integral(c.type) && value.metaType().id() != QMetaType::Double) {
            double unit = 1;
            for (int i = 0; c.type == FieldType::Currency && i < m_currency.decimals; ++i) {
                unit *= 10;
            }
            const qint64 step = qMax<qint64>(1, std::llround(interval.size * unit));
            const qint64 low = floorDiv(value.toLongLong(), step) * step;
            k.sort = low;
            k.id = QString::number(low);
            k.text = c.type == FieldType::Currency
                         ? s("%1 – %2").arg(formatMoney(low, m_currency.decimals, m_currency.symbol),
                                            formatMoney(low + step - 1, m_currency.decimals, m_currency.symbol))
                         : s("%1 – %2").arg(low).arg(low + step - 1);
        } else {
            const double low = std::floor(value.toDouble() / interval.size) * interval.size;
            k.sort = low;
            k.id = QString::number(low, 'g', 15);
            k.text = s("%1 to under %2").arg(plain(low), plain(low + interval.size));
        }
    }
    return k;
}

int Runner::compareValues(int column, const QVariant &a, const QVariant &b) const
{
    const ReportColumn &c = m_source.columns.at(column);
    if (!c.lookup.isEmpty()) {
        const int byName = m_source.display(column, a, m_currency).compare(m_source.display(column, b, m_currency),
                                                                           Qt::CaseInsensitive);
        if (byName != 0) {
            return byName < 0 ? -1 : 1;
        }
    }
    const bool numbers = isNumeric(c.type) || c.type == FieldType::YesNo;
    if (numbers && integral(c.type) && a.metaType().id() != QMetaType::Double && b.metaType().id() != QMetaType::Double) {
        const qint64 x = a.toLongLong();
        const qint64 y = b.toLongLong();
        return x < y ? -1 : x > y ? 1 : 0;
    }
    if (numbers) {
        const double x = a.toDouble();
        const double y = b.toDouble();
        return x < y ? -1 : x > y ? 1 : 0;
    }
    int cmp = a.toString().compare(b.toString(), Qt::CaseInsensitive);
    if (cmp == 0) {
        cmp = a.toString().compare(b.toString());
    }
    return cmp < 0 ? -1 : cmp > 0 ? 1 : 0;
}

int Runner::compareKeys(const GroupKey &a, const GroupKey &b)
{
    const bool numbers = a.sort.metaType().id() != QMetaType::QString && b.sort.metaType().id() != QMetaType::QString;
    if (numbers) {
        const double x = a.sort.toDouble();
        const double y = b.sort.toDouble();
        return x < y ? -1 : x > y ? 1 : 0;
    }
    const int cmp = a.sort.toString().compare(b.sort.toString(), Qt::CaseInsensitive);
    return cmp < 0 ? -1 : cmp > 0 ? 1 : 0;
}

QList<int> Runner::orderRows(QHash<int, QList<GroupKey>> *keys, QString *error) const
{
    // The designer's equality filter: by the stored value's edit text or
    // the printed text, case-blind ("Card", "25.00", a donor's name).
    QList<int> order;
    const int filterColumn = m_spec.filterField.isEmpty() ? -1 : m_source.columnIndex(m_spec.filterField);
    if (!m_spec.filterField.isEmpty() && filterColumn < 0) {
        *error = s("The report filters on %1, which is not in %2.").arg(m_spec.filterField, m_source.name);
        return {};
    }
    for (int r = 0; r < m_source.rows.size(); ++r) {
        if (filterColumn >= 0) {
            const QVariant v = m_source.rows.at(r).value(filterColumn);
            const FieldType type = m_source.columns.at(filterColumn).type;
            const QString wanted = m_spec.filterValue.trimmed();
            const bool match = editText(type, v, m_currency).compare(wanted, Qt::CaseInsensitive) == 0
                               || m_source.display(filterColumn, v, m_currency).compare(wanted, Qt::CaseInsensitive) == 0;
            if (!match) {
                continue;
            }
        }
        order.append(r);
        QList<GroupKey> rowKeys;
        for (int level = 0; level < m_groupColumns.size(); ++level) {
            rowKeys.append(keyOf(level, m_source.rows.at(r).value(m_groupColumns.at(level))));
        }
        keys->insert(r, rowKeys);
    }
    QList<int> sortColumns;
    for (const ReportSortKey &k : m_spec.sort) {
        const int c = m_source.columnIndex(k.field);
        if (c < 0) {
            *error = s("The report sorts on %1, which is not in %2.").arg(k.field, m_source.name);
            return {};
        }
        sortColumns.append(c);
    }
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const QList<GroupKey> &ka = keys->value(a);
        const QList<GroupKey> &kb = keys->value(b);
        for (int level = 0; level < m_groupColumns.size(); ++level) {
            const GroupKey &x = ka.at(level);
            const GroupKey &y = kb.at(level);
            if (x.null != y.null) {
                return y.null; // Nulls last, either direction
            }
            if (x.null) {
                continue;
            }
            const int c = m_groupColumns.at(level);
            const int cmp = m_spec.groups.at(level).interval.by == QLatin1String("value")
                                ? compareValues(c, m_source.rows.at(a).at(c), m_source.rows.at(b).at(c))
                                : compareKeys(x, y);
            if (cmp != 0) {
                return m_spec.groups.at(level).descending ? cmp > 0 : cmp < 0;
            }
        }
        for (int i = 0; i < sortColumns.size(); ++i) {
            const int c = sortColumns.at(i);
            const QVariant &x = m_source.rows.at(a).at(c);
            const QVariant &y = m_source.rows.at(b).at(c);
            if (isNull(x) != isNull(y)) {
                return isNull(y);
            }
            if (isNull(x)) {
                continue;
            }
            const int cmp = compareValues(c, x, y);
            if (cmp != 0) {
                return m_spec.sort.at(i).descending ? cmp > 0 : cmp < 0;
            }
        }
        return false;
    });
    return order;
}

} // namespace QindaOffice::Base::detail
