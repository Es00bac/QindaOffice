// SPDX-License-Identifier: GPL-3.0-or-later
#include "report_table.h"

namespace QindaOffice::Books {

ReportCell ReportCell::ofText(const QString &text)
{
    ReportCell c;
    c.type = ColumnType::Text;
    c.empty = false;
    c.text = text;
    return c;
}

ReportCell ReportCell::ofMoney(qint64 minor)
{
    ReportCell c;
    c.type = ColumnType::Money;
    c.empty = false;
    c.value = minor;
    return c;
}

ReportCell ReportCell::ofNumber(qint64 value, int scale)
{
    ReportCell c;
    c.type = ColumnType::Number;
    c.empty = false;
    c.value = value;
    c.scale = scale;
    return c;
}

ReportCell ReportCell::ofPercent(qint64 value, int scale)
{
    ReportCell c = ofNumber(value, scale);
    c.type = ColumnType::Percent;
    return c;
}

ReportCell ReportCell::ofDate(const QDate &date)
{
    ReportCell c;
    c.type = ColumnType::Date;
    c.empty = !date.isValid();
    c.date = date;
    return c;
}

QString ReportCell::display(const QString &currency, const QLocale &locale) const
{
    if (empty) {
        return QString();
    }
    switch (type) {
    case ColumnType::Text: return text;
    case ColumnType::Money: return Money(value, currency).format(locale, false);
    case ColumnType::Number: return Decimal(value, scale).toString();
    case ColumnType::Percent: return Decimal(value, scale).toString() + QLatin1Char('%');
    case ColumnType::Date: return date.toString(Qt::ISODate);
    }
    return QString();
}

int ReportTable::columnIndex(const QString &columnId) const
{
    for (int i = 0; i < columns.size(); ++i) {
        if (columns[i].id == columnId) {
            return i;
        }
    }
    return -1;
}

const ReportRow *ReportTable::row(const QString &key) const
{
    for (const ReportRow &r : rows) {
        if (r.key == key) {
            return &r;
        }
    }
    return nullptr;
}

qint64 ReportTable::money(const QString &key, const QString &columnId) const
{
    const ReportRow *r = row(key);
    const int c = columnIndex(columnId);
    if (r == nullptr || c < 0 || c >= r->cells.size()) {
        return 0;
    }
    return r->cells[c].value;
}

QString ReportTable::text(const QString &key, const QString &columnId) const
{
    const ReportRow *r = row(key);
    const int c = columnIndex(columnId);
    if (r == nullptr || c < 0 || c >= r->cells.size()) {
        return QString();
    }
    return r->cells[c].display(currency);
}

} // namespace QindaOffice::Books
