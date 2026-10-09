// SPDX-License-Identifier: GPL-3.0-or-later
#include "payroll_calendar.h"

#include <QLocale>

namespace QindaOffice::Books::Payroll {

namespace {

const QLocale &english()
{
    static const QLocale locale(QLocale::English, QLocale::UnitedStates);
    return locale;
}

QDate lastDayOfMonth(int year, int month)
{
    const QDate first(year, month, 1);
    return first.addDays(first.daysInMonth() - 1);
}

DuePeriod movedTo(DuePeriod period, const QDate &statutory, const BusinessCalendar &calendar)
{
    period.statutory = statutory;
    period.due = calendar.onOrAfter(statutory);
    return period;
}

QString dayRange(const QDate &start, const QDate &end)
{
    if (start == end) {
        return english().toString(start, QStringLiteral("MMM d, yyyy"));
    }
    return english().toString(start, QStringLiteral("MMM d")) + QStringLiteral(" – ")
        + english().toString(end, QStringLiteral("MMM d, yyyy"));
}

} // namespace

BusinessCalendar::BusinessCalendar(const QList<QDate> &holidays)
    : m_holidays(holidays.cbegin(), holidays.cend())
{
}

bool BusinessCalendar::isBusinessDay(const QDate &date) const
{
    return date.isValid() && date.dayOfWeek() <= 5 && !m_holidays.contains(date);
}

QDate BusinessCalendar::onOrAfter(const QDate &date) const
{
    QDate d = date;
    while (d.isValid() && !isBusinessDay(d)) {
        d = d.addDays(1);
    }
    return d;
}

QDate BusinessCalendar::addBusinessDays(const QDate &date, int count) const
{
    QDate d = date;
    int left = count;
    while (d.isValid() && left > 0) {
        d = d.addDays(1);
        if (isBusinessDay(d)) {
            --left;
        }
    }
    return d;
}

QString depositScheduleKey(DepositSchedule schedule)
{
    return schedule == DepositSchedule::Semiweekly ? QStringLiteral("semiweekly") : QStringLiteral("monthly");
}

std::optional<DepositSchedule> depositScheduleFromKey(const QString &key)
{
    if (key == QLatin1String("monthly")) {
        return DepositSchedule::Monthly;
    }
    if (key == QLatin1String("semiweekly")) {
        return DepositSchedule::Semiweekly;
    }
    return std::nullopt;
}

int quarterOf(const QDate &date)
{
    return (date.month() - 1) / 3 + 1;
}

QDate quarterStart(int year, int quarter)
{
    return QDate(year, (quarter - 1) * 3 + 1, 1);
}

QDate quarterEnd(int year, int quarter)
{
    return lastDayOfMonth(year, quarter * 3);
}

QString quarterLabel(int year, int quarter)
{
    return QStringLiteral("Q%1 %2").arg(quarter).arg(year);
}

DuePeriod federalDepositPeriod(const QDate &payDate, DepositSchedule schedule, const BusinessCalendar &calendar)
{
    DuePeriod period;
    if (schedule == DepositSchedule::Monthly) {
        period.start = QDate(payDate.year(), payDate.month(), 1);
        period.end = lastDayOfMonth(payDate.year(), payDate.month());
        period.label = english().toString(payDate, QStringLiteral("MMMM yyyy"));
        return movedTo(period, period.start.addMonths(1).addDays(14), calendar);
    }
    // Semiweekly: Wednesday–Friday paydays, or Saturday–Tuesday paydays.
    const int dow = payDate.dayOfWeek(); // Monday = 1 … Sunday = 7
    QDate start;
    if (dow >= 3 && dow <= 5) {
        start = payDate.addDays(3 - dow); // back to Wednesday
    } else {
        start = payDate.addDays(dow >= 6 ? 6 - dow : -(dow + 1)); // back to Saturday
    }
    const QDate end = start.addDays(dow >= 3 && dow <= 5 ? 2 : 3);
    // The third business day after the period: Wednesday after a Friday,
    // Friday after a Tuesday, one more for each legal holiday among them.
    const QDate due = calendar.addBusinessDays(end, 3);
    // A period never crosses a quarter end (Pub 15: separate deposits).
    const int q = quarterOf(payDate);
    period.start = qMax(start, quarterStart(payDate.year(), q));
    period.end = qMin(end, quarterEnd(payDate.year(), q));
    period.label = QStringLiteral("Paydays ") + dayRange(period.start, period.end);
    period.statutory = due;
    period.due = due;
    return period;
}

QDate nextDayDepositDue(const QDate &payDate, const BusinessCalendar &calendar)
{
    return calendar.addBusinessDays(payDate, 1);
}

DuePeriod quarterlyReturnPeriod(int year, int quarter, const BusinessCalendar &calendar)
{
    DuePeriod period;
    period.start = quarterStart(year, quarter);
    period.end = quarterEnd(year, quarter);
    period.label = quarterLabel(year, quarter);
    const QDate monthAfter = period.end.addDays(1);
    return movedTo(period, lastDayOfMonth(monthAfter.year(), monthAfter.month()), calendar);
}

DuePeriod annualReturnPeriod(int year, const BusinessCalendar &calendar)
{
    DuePeriod period;
    period.start = QDate(year, 1, 1);
    period.end = QDate(year, 12, 31);
    period.label = QString::number(year);
    return movedTo(period, QDate(year + 1, 1, 31), calendar);
}

QString coloradoFrequencyKey(ColoradoFrequency frequency)
{
    switch (frequency) {
    case ColoradoFrequency::Quarterly:
        return QStringLiteral("quarterly");
    case ColoradoFrequency::Monthly:
        return QStringLiteral("monthly");
    case ColoradoFrequency::Weekly:
        return QStringLiteral("weekly");
    }
    return {};
}

std::optional<ColoradoFrequency> coloradoFrequencyFromKey(const QString &key)
{
    for (const ColoradoFrequency f : {ColoradoFrequency::Quarterly, ColoradoFrequency::Monthly, ColoradoFrequency::Weekly}) {
        if (coloradoFrequencyKey(f) == key) {
            return f;
        }
    }
    return std::nullopt;
}

DuePeriod coloradoWithholdingPeriod(const QDate &payDate, ColoradoFrequency frequency, const BusinessCalendar &calendar)
{
    switch (frequency) {
    case ColoradoFrequency::Quarterly:
        return quarterlyReturnPeriod(payDate.year(), quarterOf(payDate), calendar);
    case ColoradoFrequency::Monthly: {
        DuePeriod period;
        period.start = QDate(payDate.year(), payDate.month(), 1);
        period.end = lastDayOfMonth(payDate.year(), payDate.month());
        period.label = english().toString(payDate, QStringLiteral("MMMM yyyy"));
        return movedTo(period, period.start.addMonths(1).addDays(14), calendar);
    }
    case ColoradoFrequency::Weekly:
        break;
    }
    // Weekly: the week Saturday–Friday, due the third business day after
    // the Friday. The year's last period ends on December 31 whatever the
    // day, due the third business day after it; the next starts January 1
    // (Colorado Wage Withholding Tax Guide, "Year-end weekly filing period").
    const int dow = payDate.dayOfWeek();
    DuePeriod period;
    period.start = payDate.addDays(dow == 6 ? 0 : (dow == 7 ? -1 : -(dow + 1)));
    period.end = period.start.addDays(6);
    const QDate yearEnd(payDate.year(), 12, 31);
    if (period.end.year() > payDate.year()) {
        period.end = yearEnd;
    }
    if (period.start.year() < payDate.year()) {
        period.start = QDate(payDate.year(), 1, 1);
    }
    period.label = QStringLiteral("Week ") + dayRange(period.start, period.end);
    period.statutory = calendar.addBusinessDays(period.end, 3);
    period.due = period.statutory;
    return period;
}

ColoradoFrequency coloradoFrequencyFor(const Money &annualWithholding, const ColoradoTables &tables)
{
    if (annualWithholding > tables.withholdingWeeklyAbove) {
        return ColoradoFrequency::Weekly;
    }
    if (annualWithholding >= tables.withholdingMonthlyFrom) {
        return ColoradoFrequency::Monthly;
    }
    return ColoradoFrequency::Quarterly;
}

DuePeriod monthlyReturnPeriod(int year, int month, const BusinessCalendar &calendar)
{
    DuePeriod period;
    period.start = QDate(year, month, 1);
    period.end = lastDayOfMonth(year, month);
    period.label = english().toString(period.start, QStringLiteral("MMMM yyyy"));
    const QDate next = period.end.addDays(1);
    return movedTo(period, lastDayOfMonth(next.year(), next.month()), calendar);
}

} // namespace QindaOffice::Books::Payroll
