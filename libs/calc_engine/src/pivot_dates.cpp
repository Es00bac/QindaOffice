// SPDX-License-Identifier: GPL-3.0-or-later
#include "pivot_dates.h"
#include "functions/functions.h"
#include <cmath>
namespace QindaOffice::Calc {
QStringList pivotDateTypes()
{
    return {QStringLiteral("dateBetween"), QStringLiteral("dateOlderThan"), QStringLiteral("dateNewerThan"),
            QStringLiteral("thisMonth"), QStringLiteral("lastMonth"), QStringLiteral("nextMonth"),
            QStringLiteral("thisQuarter"), QStringLiteral("lastQuarter"), QStringLiteral("nextQuarter"),
            QStringLiteral("thisYear"), QStringLiteral("lastYear"), QStringLiteral("nextYear"),
            QStringLiteral("yearToDate"), QStringLiteral("dateEqual"), QStringLiteral("dateNotEqual"),
            QStringLiteral("dateOlderThanOrEqual"), QStringLiteral("dateNewerThanOrEqual"),
            QStringLiteral("dateNotBetween"), QStringLiteral("today"), QStringLiteral("yesterday"),
            QStringLiteral("tomorrow"), QStringLiteral("thisWeek"), QStringLiteral("lastWeek"), QStringLiteral("nextWeek"),
            QStringLiteral("Q1"), QStringLiteral("Q2"), QStringLiteral("Q3"), QStringLiteral("Q4"),
            QStringLiteral("M1"), QStringLiteral("M2"), QStringLiteral("M3"), QStringLiteral("M4"),
            QStringLiteral("M5"), QStringLiteral("M6"), QStringLiteral("M7"), QStringLiteral("M8"),
            QStringLiteral("M9"), QStringLiteral("M10"), QStringLiteral("M11"), QStringLiteral("M12")};
}
bool pivotDateAccepts(const PivotFilter &filter, const Value &value, const QDate &today)
{
    const auto *number = std::get_if<double>(&value);
    if (number == nullptr || !std::isfinite(*number)) return false;
    const QDate date = dateFromSerial(qint64(std::floor(*number)));
    const QString &type = filter.dateType;
    if (type.startsWith(QLatin1String("date"))) {
        QDate a = QDate::fromString(filter.text1.left(10), Qt::ISODate);
        QDate b = QDate::fromString(filter.text2.left(10), Qt::ISODate);
        if (!a.isValid()) a = dateFromSerial(qint64(filter.value1));
        if (!b.isValid()) b = dateFromSerial(qint64(filter.value2));
        if (type == QLatin1String("dateEqual")) return date == a;
        if (type == QLatin1String("dateNotEqual")) return date != a;
        if (type == QLatin1String("dateOlderThan")) return date < a;
        if (type == QLatin1String("dateOlderThanOrEqual")) return date <= a;
        if (type == QLatin1String("dateNewerThan")) return date > a;
        if (type == QLatin1String("dateNewerThanOrEqual")) return date >= a;
        const bool between = date >= qMin(a, b) && date <= qMax(a, b);
        return type == QLatin1String("dateNotBetween") ? !between : between;
    }
    if (type.startsWith(QLatin1Char('M'))) return date.month() == type.mid(1).toInt();
    if (type.startsWith(QLatin1Char('Q'))) return (date.month() - 1) / 3 + 1 == type.mid(1).toInt();
    QDate start = today;
    QDate end = today.addDays(1);
    const int offset = type.startsWith(QLatin1String("last")) ? -1 : type.startsWith(QLatin1String("next")) ? 1 : 0;
    if (type.endsWith(QLatin1String("Month"))) {
        start = QDate(today.year(), today.month(), 1).addMonths(offset);
        end = start.addMonths(1);
    } else if (type.endsWith(QLatin1String("Quarter"))) {
        start = QDate(today.year(), (today.month() - 1) / 3 * 3 + 1, 1).addMonths(offset * 3);
        end = start.addMonths(3);
    } else if (type.endsWith(QLatin1String("Year"))) {
        start = QDate(today.year() + offset, 1, 1);
        end = start.addYears(1);
    } else if (type.endsWith(QLatin1String("Week"))) {
        start = today.addDays(-(today.dayOfWeek() % 7) + offset * 7);
        end = start.addDays(7);
    } else if (type == QLatin1String("yearToDate")) start = QDate(today.year(), 1, 1);
    else if (type == QLatin1String("yesterday")) { start = today.addDays(-1); end = today; }
    else if (type == QLatin1String("tomorrow")) { start = today.addDays(1); end = today.addDays(2); }
    return date >= start && date < end;
}
} // namespace QindaOffice::Calc
