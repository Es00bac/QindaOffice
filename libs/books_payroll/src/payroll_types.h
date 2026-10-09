// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Shared vocabulary of books_payroll (M71, D-081): pay frequencies, the
// filing statuses of the federal forms, rate schedules, and the worksheet
// line every computation reports so a person can follow it line by line.
//
// AGENT-GUARD: books_payroll is pure arithmetic over Money/Decimal (exact
// integer cents, rates parsed from text). No double or float anywhere —
// tst_payroll_tables scans the sources. No rate or amount is written in
// code: every number comes from the tax-year data file (payroll_tables.h).

#include "decimal.h"
#include "money.h"

#include <QList>
#include <QString>

#include <optional>

namespace QindaOffice::Books::Payroll {

// US payroll is kept in US dollars; the app refuses payroll for a company
// kept in another currency.
inline const QString kCurrency = QStringLiteral("USD");
[[nodiscard]] inline Money usd(qint64 cents)
{
    return Money(cents, kCurrency);
}
[[nodiscard]] Money parseUsd(const QString &text); // "1234.56" → cents; refuses (zero) junk — tests and tables only
// "$2,500.00": an amount as a note or a worksheet line writes it — no
// locale lookup, so a note reads the same everywhere (and in the tests).
[[nodiscard]] QString usdText(const Money &amount);

// The pay periods of IRS Publication 15-T Table 3 (and DR 1098).
enum class PayFrequency { Daily, Weekly, Biweekly, Semimonthly, Monthly, Quarterly, Semiannual, Annual };
[[nodiscard]] QString payFrequencyKey(PayFrequency frequency); // "biweekly"
[[nodiscard]] QString payFrequencyLabel(PayFrequency frequency); // "Every other week (biweekly)"
[[nodiscard]] std::optional<PayFrequency> payFrequencyFromKey(const QString &key);
[[nodiscard]] QList<PayFrequency> allPayFrequencies();
// Pay periods in a year as the calendar counts them (260 working days, 52
// weeks, 26, 24, 12, 4, 2, 1) — for dividing an annual salary. The tax
// computations take their counts from the tables (Pub 15-T Table 3, DR 1098).
[[nodiscard]] int periodsPerYear(PayFrequency frequency);

// Form W-4 (2020 or later), Step 1(c).
enum class W4Status { SingleOrMarriedSeparately, MarriedJointly, HeadOfHousehold };
[[nodiscard]] QString w4StatusKey(W4Status status);
[[nodiscard]] std::optional<W4Status> w4StatusFromKey(const QString &key);

// Form W-4 (2019 or earlier), line 3. "Married, but withhold at higher
// Single rate" uses the Single table (Pub 15-T).
enum class W4LegacyStatus { Single, Married, MarriedWithholdAtSingleRate };
[[nodiscard]] QString w4LegacyStatusKey(W4LegacyStatus status);
[[nodiscard]] std::optional<W4LegacyStatus> w4LegacyStatusFromKey(const QString &key);

// Form 1040 filing status (the estimated-tax planner).
enum class FilingStatus { Single, MarriedJointly, MarriedSeparately, HeadOfHousehold, QualifyingSurvivingSpouse };
[[nodiscard]] QString filingStatusKey(FilingStatus status);
[[nodiscard]] QString filingStatusLabel(FilingStatus status);
[[nodiscard]] std::optional<FilingStatus> filingStatusFromKey(const QString &key);
[[nodiscard]] QList<FilingStatus> allFilingStatuses();

// One row of a percentage-method table or a tax rate schedule:
// for an amount at least `over` and less than `upTo` (none = no limit),
// tax = base + rate × (amount − over). Pub 15-T columns A, B, C, D (its
// column E "of the amount that exceeds" always equals A).
struct Bracket {
    Money over;
    std::optional<Money> upTo;
    Money base;
    Decimal rate;
};
using RateSchedule = QList<Bracket>;

// The row `amount` falls in (nullopt for a negative amount or an empty
// schedule) and the tax it gives, rounded half-up to the cent.
[[nodiscard]] std::optional<Bracket> bracketFor(const RateSchedule &schedule, const Money &amount);
[[nodiscard]] Money taxBySchedule(const RateSchedule &schedule, const Money &amount);

// amount × rate, rounded half-up to the cent (the only rounding the
// publications use for a percentage of a dollar amount).
[[nodiscard]] Money percentOf(const Money &amount, const Decimal &rate);
// amount ÷ count, rounded half-up to the cent.
[[nodiscard]] Money dividedBy(const Money &amount, qint64 count);
// To the nearest whole dollar, half-up (DR 1098 and the 1040-ES lines
// that say "round").
[[nodiscard]] Money roundToDollar(const Money &amount);
[[nodiscard]] Money maxMoney(const Money &a, const Money &b);
[[nodiscard]] Money minMoney(const Money &a, const Money &b);
[[nodiscard]] Money nonNegative(const Money &amount);

// One line of a worksheet as the publication numbers it ("1a", "2h"):
// what it is and its value — an amount, a count or a percentage.
struct WorksheetLine {
    enum class Kind { Amount, Count, Rate, Note };
    QString line;
    QString text;
    Kind kind = Kind::Amount;
    Money amount;
    qint64 count = 0;
    Decimal rate;

    [[nodiscard]] static WorksheetLine money(const QString &line, const QString &text, const Money &value);
    [[nodiscard]] static WorksheetLine number(const QString &line, const QString &text, qint64 value);
    [[nodiscard]] static WorksheetLine percent(const QString &line, const QString &text, const Decimal &value);
    [[nodiscard]] static WorksheetLine note(const QString &line, const QString &text);
    // "1,234.56", "26", "22%" or "" for a note.
    [[nodiscard]] QString valueText() const;
};
using Worksheet = QList<WorksheetLine>;

// The amount on worksheet line `line` (zero when absent) — tests and forms.
[[nodiscard]] Money worksheetAmount(const Worksheet &sheet, const QString &line);

} // namespace QindaOffice::Books::Payroll
