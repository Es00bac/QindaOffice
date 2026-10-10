// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Instants, spans and money as text (plan_types.h has the conventions).
// Formatting is what the task sheet shows; parsing accepts what a user
// types there, including everything the formatter produces, so a cell can
// always be edited back to its own text.

#include "plan_types.h"

#include <QDateTime>
#include <QLocale>

#include <optional>

namespace QindaOffice::Plan {

// ---- instants ----
[[nodiscard]] Minutes toMinutes(const QDate &date, int minuteOfDay = 0);
[[nodiscard]] Minutes toMinutes(const QDateTime &wallClock);
[[nodiscard]] QDate dateOf(Minutes instant);
[[nodiscard]] int minuteOfDay(Minutes instant);
// The wall-clock time as a QDateTime (UTC spec, so arithmetic never shifts).
[[nodiscard]] QDateTime toDateTime(Minutes instant);
// "2026-03-02T08:00"; empty for kNoDate.
[[nodiscard]] QString toIso(Minutes instant);
// Accepts "yyyy-MM-dd" (at `defaultMinute`), "yyyy-MM-ddTHH:mm[:ss]" and
// "yyyy-MM-dd HH:mm"; kNoDate when empty or invalid.
[[nodiscard]] Minutes fromIso(const QString &text, int defaultMinute = 0);

// Sheet dates: "Mon 2 Mar 26" (the locale's day and month names).
[[nodiscard]] QString formatDate(Minutes instant, const QLocale &locale = QLocale());
[[nodiscard]] QString formatDateTime(Minutes instant, const QLocale &locale = QLocale());
// ISO forms, the sheet form, "2 Mar 2026", "3/2/2026" (M/d/yyyy) and
// "2.3.2026" (d.M.yyyy), each optionally followed by "HH:mm". A date
// without a time takes `defaultMinute`.
[[nodiscard]] std::optional<Minutes> parseDate(const QString &text, int defaultMinute,
                                               const QLocale &locale = QLocale());

// ---- spans ----
struct DurationFormat {
    int minutesPerDay = 480;
    int minutesPerWeek = 2400;
    int daysPerMonth = 20;
};
[[nodiscard]] DurationFormat durationFormatOf(const ProjectInfo &info);
// M83: an ELAPSED unit is wall-clock time, as in Project: 1 emin, 1 ehr = 60,
// 1 eday = 1440, 1 ewk = 10080, 1 emon = 30 edays.
[[nodiscard]] Minutes minutesPerUnit(DurationUnit unit, const DurationFormat &format, bool elapsed = false);

struct ParsedDuration {
    Minutes minutes = 0;
    DurationUnit unit = DurationUnit::Days;
    bool estimated = false;
    bool elapsed = false; // M83: "2ed", "3 ehrs", "1 ewk", "1 emon"
};
// "5 days", "1 day", "4 hrs", "30 mins", "2 wks", "1 mon", "1.5d", "0",
// "3?" (estimated); a bare number is days. M83: the elapsed forms "2ed",
// "2 edays", "4eh", "1 ewk", "1 emon". Negative only with allowNegative.
[[nodiscard]] std::optional<ParsedDuration> parseDuration(const QString &text, const DurationFormat &format,
                                                          bool allowNegative = false);
[[nodiscard]] QString formatDuration(Minutes minutes, DurationUnit unit, const DurationFormat &format,
                                     bool estimated = false, const QLocale &locale = QLocale(), bool elapsed = false);
// A task's duration as the sheet shows it (summary tasks in days).
[[nodiscard]] QString taskDurationText(const Task &task, const DurationFormat &format, const QLocale &locale = QLocale());
// A span in the unit that reads best (slack, variances): days when it is a
// whole number of hours per day, else hours.
[[nodiscard]] QString formatSpan(Minutes minutes, const DurationFormat &format, const QLocale &locale = QLocale());
// Work in hours: "40 hrs".
[[nodiscard]] QString formatWork(Minutes minutes, const QLocale &locale = QLocale());
// A lag: "+2 days", "-1 day", "+50%"; empty for a zero lag.
[[nodiscard]] QString formatLag(const Link &link, const DurationFormat &format, const QLocale &locale = QLocale());

// ---- money ----
[[nodiscard]] int currencyDecimals(const QString &currency);
[[nodiscard]] QString formatMoney(qint64 minor, const QString &currency, const QLocale &locale = QLocale());
// "1,234.50", "$1234.5", "1234" → minor units; nullopt when not a number
// or more decimals than the currency has.
[[nodiscard]] std::optional<qint64> parseMoney(const QString &text, const QString &currency,
                                               const QLocale &locale = QLocale());
// Rounds a/b half away from zero (b > 0).
[[nodiscard]] qint64 divRound(qint64 a, qint64 b);

} // namespace QindaOffice::Plan
