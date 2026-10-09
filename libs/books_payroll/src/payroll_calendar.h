// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Payroll due dates (M71, D-081): when federal and Colorado payroll taxes
// must be deposited and their returns filed, from the pay date and the
// company's deposit schedule.
//
// AGENT-NOTE (sources, docs/payroll-tax-tables.md): IRS Publication 15
// (2026) section 11 "Depositing Taxes" — monthly depositors deposit a
// month's taxes by the 15th of the next month; semiweekly depositors have
// at least 3 business days after the semiweekly period (Wednesday–Friday
// paydays: the following Wednesday; Saturday–Tuesday: the following
// Friday; one more business day per legal holiday among those days, which
// is exactly "the third business day after the period"); a deposit due on
// a day that is not a business day is timely on the next business day.
// Returns (Forms 941, 940, W-2/W-3) are due by the last day of the month
// after the period, moved past weekends and legal holidays (IRC 7503).
//
// The calendar never reads the clock: callers pass the dates.

#include "payroll_tables.h"

#include <QDate>
#include <QList>
#include <QSet>
#include <QString>

namespace QindaOffice::Books::Payroll {

class BusinessCalendar {
public:
    explicit BusinessCalendar(const QList<QDate> &holidays = {});
    [[nodiscard]] bool isHoliday(const QDate &date) const { return m_holidays.contains(date); }
    // Monday–Friday and not a listed legal holiday.
    [[nodiscard]] bool isBusinessDay(const QDate &date) const;
    // `date` itself when it is a business day, else the next one.
    [[nodiscard]] QDate onOrAfter(const QDate &date) const;
    // The `count`-th business day after `date` (count ≥ 1).
    [[nodiscard]] QDate addBusinessDays(const QDate &date, int count) const;

private:
    QSet<QDate> m_holidays;
};

enum class DepositSchedule { Monthly, Semiweekly };
[[nodiscard]] QString depositScheduleKey(DepositSchedule schedule);
[[nodiscard]] std::optional<DepositSchedule> depositScheduleFromKey(const QString &key);

// A period whose payroll taxes are deposited together, and when.
struct DuePeriod {
    QDate start;     // first pay date the period covers
    QDate end;       // last
    QDate due;       // the deposit or filing deadline (moved to a business day)
    QDate statutory; // the deadline before moving it (equal to `due` most of the time)
    QString label;   // "September 2026", "Paydays Sep 30 – Sep 30, 2026", "Q3 2026"
};

// The quarter (1–4) of a date and its first/last day.
[[nodiscard]] int quarterOf(const QDate &date);
[[nodiscard]] QDate quarterStart(int year, int quarter);
[[nodiscard]] QDate quarterEnd(int year, int quarter);
[[nodiscard]] QString quarterLabel(int year, int quarter); // "Q3 2026"

// Federal employment taxes (Form 941: withheld income tax, social
// security and Medicare): the deposit period `payDate` falls in. A
// semiweekly period never crosses a quarter end — the part in each
// quarter is its own deposit, due on the period's due date.
[[nodiscard]] DuePeriod federalDepositPeriod(const QDate &payDate, DepositSchedule schedule,
                                             const BusinessCalendar &calendar);
// The $100,000 next-day rule: the business day after the day the
// liability reached the threshold.
[[nodiscard]] QDate nextDayDepositDue(const QDate &payDate, const BusinessCalendar &calendar);

// Last day of the month after the quarter, moved to a business day: Form
// 941, FUTA deposits, Colorado UI and FAMLI wage reports and premiums.
[[nodiscard]] DuePeriod quarterlyReturnPeriod(int year, int quarter, const BusinessCalendar &calendar);
// January 31 after the year, moved: Form 940, W-2/W-3, DR 1093.
[[nodiscard]] DuePeriod annualReturnPeriod(int year, const BusinessCalendar &calendar);

// Colorado wage withholding (DR 1094) by filing frequency.
enum class ColoradoFrequency { Quarterly, Monthly, Weekly };
[[nodiscard]] QString coloradoFrequencyKey(ColoradoFrequency frequency);
[[nodiscard]] std::optional<ColoradoFrequency> coloradoFrequencyFromKey(const QString &key);
// The DR 1094 period `payDate` falls in and its due date: quarterly — the
// last day of the month after the quarter; monthly — the 15th of the next
// month; weekly — the third business day after the Friday that ends the
// week (Saturday–Friday).
[[nodiscard]] DuePeriod coloradoWithholdingPeriod(const QDate &payDate, ColoradoFrequency frequency,
                                                  const BusinessCalendar &calendar);
// The frequency DR 1094 prescribes for an annual withholding liability.
[[nodiscard]] ColoradoFrequency coloradoFrequencyFor(const Money &annualWithholding, const ColoradoTables &tables);

// A month's period (OPT filing, monthly reminders): the 1st to the last
// day, due the last day of the following month, moved.
[[nodiscard]] DuePeriod monthlyReturnPeriod(int year, int month, const BusinessCalendar &calendar);

} // namespace QindaOffice::Books::Payroll
