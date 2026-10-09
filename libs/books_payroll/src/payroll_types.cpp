// SPDX-License-Identifier: GPL-3.0-or-later
#include "payroll_types.h"

#include <QLocale>

#include <array>

namespace QindaOffice::Books::Payroll {

namespace {

struct FrequencyRow {
    PayFrequency frequency;
    const char *key;
    const char *label;
    int periods;
};

constexpr std::array<FrequencyRow, 8> kFrequencies{{
    {PayFrequency::Daily, "daily", "Daily", 260},
    {PayFrequency::Weekly, "weekly", "Weekly", 52},
    {PayFrequency::Biweekly, "biweekly", "Every other week (biweekly)", 26},
    {PayFrequency::Semimonthly, "semimonthly", "Twice a month (semimonthly)", 24},
    {PayFrequency::Monthly, "monthly", "Monthly", 12},
    {PayFrequency::Quarterly, "quarterly", "Quarterly", 4},
    {PayFrequency::Semiannual, "semiannual", "Twice a year (semiannually)", 2},
    {PayFrequency::Annual, "annual", "Once a year (annually)", 1},
}};

struct FilingRow {
    FilingStatus status;
    const char *key;
    const char *label;
};

constexpr std::array<FilingRow, 5> kFilings{{
    {FilingStatus::Single, "single", "Single"},
    {FilingStatus::MarriedJointly, "marriedJointly", "Married filing jointly"},
    {FilingStatus::MarriedSeparately, "marriedSeparately", "Married filing separately"},
    {FilingStatus::HeadOfHousehold, "headOfHousehold", "Head of household"},
    {FilingStatus::QualifyingSurvivingSpouse, "qualifyingSurvivingSpouse", "Qualifying surviving spouse"},
}};

} // namespace

Money parseUsd(const QString &text)
{
    return Money::parse(text, kCurrency).value_or(usd(0));
}

QString usdText(const Money &amount)
{
    QString plain = amount.toPlainString();
    const bool negative = plain.startsWith(QLatin1Char('-'));
    if (negative) {
        plain.remove(0, 1);
    }
    const qsizetype dot = plain.indexOf(QLatin1Char('.'));
    QString whole = dot < 0 ? plain : plain.left(dot);
    for (qsizetype i = whole.size() - 3; i > 0; i -= 3) {
        whole.insert(i, QLatin1Char(','));
    }
    return (negative ? QStringLiteral("-$") : QStringLiteral("$")) + whole + (dot < 0 ? QString() : plain.mid(dot));
}

QString payFrequencyKey(PayFrequency frequency)
{
    for (const FrequencyRow &row : kFrequencies) {
        if (row.frequency == frequency) {
            return QString::fromLatin1(row.key);
        }
    }
    return {};
}

QString payFrequencyLabel(PayFrequency frequency)
{
    for (const FrequencyRow &row : kFrequencies) {
        if (row.frequency == frequency) {
            return QString::fromLatin1(row.label);
        }
    }
    return {};
}

std::optional<PayFrequency> payFrequencyFromKey(const QString &key)
{
    for (const FrequencyRow &row : kFrequencies) {
        if (key == QLatin1String(row.key)) {
            return row.frequency;
        }
    }
    return std::nullopt;
}

int periodsPerYear(PayFrequency frequency)
{
    for (const FrequencyRow &row : kFrequencies) {
        if (row.frequency == frequency) {
            return row.periods;
        }
    }
    return 1;
}

QList<PayFrequency> allPayFrequencies()
{
    QList<PayFrequency> out;
    for (const FrequencyRow &row : kFrequencies) {
        out << row.frequency;
    }
    return out;
}

QString w4StatusKey(W4Status status)
{
    switch (status) {
    case W4Status::SingleOrMarriedSeparately:
        return QStringLiteral("singleOrMarriedSeparately");
    case W4Status::MarriedJointly:
        return QStringLiteral("marriedJointly");
    case W4Status::HeadOfHousehold:
        return QStringLiteral("headOfHousehold");
    }
    return {};
}

std::optional<W4Status> w4StatusFromKey(const QString &key)
{
    for (const W4Status s : {W4Status::SingleOrMarriedSeparately, W4Status::MarriedJointly, W4Status::HeadOfHousehold}) {
        if (w4StatusKey(s) == key) {
            return s;
        }
    }
    return std::nullopt;
}

QString w4LegacyStatusKey(W4LegacyStatus status)
{
    switch (status) {
    case W4LegacyStatus::Single:
        return QStringLiteral("single");
    case W4LegacyStatus::Married:
        return QStringLiteral("married");
    case W4LegacyStatus::MarriedWithholdAtSingleRate:
        return QStringLiteral("marriedSingleRate");
    }
    return {};
}

std::optional<W4LegacyStatus> w4LegacyStatusFromKey(const QString &key)
{
    for (const W4LegacyStatus s :
         {W4LegacyStatus::Single, W4LegacyStatus::Married, W4LegacyStatus::MarriedWithholdAtSingleRate}) {
        if (w4LegacyStatusKey(s) == key) {
            return s;
        }
    }
    return std::nullopt;
}

QString filingStatusKey(FilingStatus status)
{
    for (const FilingRow &row : kFilings) {
        if (row.status == status) {
            return QString::fromLatin1(row.key);
        }
    }
    return {};
}

QString filingStatusLabel(FilingStatus status)
{
    for (const FilingRow &row : kFilings) {
        if (row.status == status) {
            return QString::fromLatin1(row.label);
        }
    }
    return {};
}

std::optional<FilingStatus> filingStatusFromKey(const QString &key)
{
    for (const FilingRow &row : kFilings) {
        if (key == QLatin1String(row.key)) {
            return row.status;
        }
    }
    return std::nullopt;
}

QList<FilingStatus> allFilingStatuses()
{
    QList<FilingStatus> out;
    for (const FilingRow &row : kFilings) {
        out << row.status;
    }
    return out;
}

std::optional<Bracket> bracketFor(const RateSchedule &schedule, const Money &amount)
{
    if (amount.isNegative()) {
        return std::nullopt;
    }
    for (const Bracket &row : schedule) {
        if (amount >= row.over && (!row.upTo || amount < *row.upTo)) {
            return row;
        }
    }
    return std::nullopt;
}

Money taxBySchedule(const RateSchedule &schedule, const Money &amount)
{
    const std::optional<Bracket> row = bracketFor(schedule, amount);
    if (!row) {
        return usd(0);
    }
    return row->base + percentOf(amount - row->over, row->rate);
}

Money percentOf(const Money &amount, const Decimal &rate)
{
    return amount.multiplied(rate, RoundingMode::HalfUp);
}

Money dividedBy(const Money &amount, qint64 count)
{
    return count <= 0 ? amount : amount.scaled(1, count, RoundingMode::HalfUp);
}

Money roundToDollar(const Money &amount)
{
    return amount.scaled(1, 100, RoundingMode::HalfUp).multipliedBy(100);
}

Money maxMoney(const Money &a, const Money &b)
{
    return a >= b ? a : b;
}

Money minMoney(const Money &a, const Money &b)
{
    return a <= b ? a : b;
}

Money nonNegative(const Money &amount)
{
    return amount.isNegative() ? Money(0, amount.currency()) : amount;
}

WorksheetLine WorksheetLine::money(const QString &line, const QString &text, const Money &value)
{
    WorksheetLine w;
    w.line = line;
    w.text = text;
    w.kind = Kind::Amount;
    w.amount = value;
    return w;
}

WorksheetLine WorksheetLine::number(const QString &line, const QString &text, qint64 value)
{
    WorksheetLine w;
    w.line = line;
    w.text = text;
    w.kind = Kind::Count;
    w.count = value;
    return w;
}

WorksheetLine WorksheetLine::percent(const QString &line, const QString &text, const Decimal &value)
{
    WorksheetLine w;
    w.line = line;
    w.text = text;
    w.kind = Kind::Rate;
    w.rate = value;
    return w;
}

WorksheetLine WorksheetLine::note(const QString &line, const QString &text)
{
    WorksheetLine w;
    w.line = line;
    w.text = text;
    w.kind = Kind::Note;
    return w;
}

QString WorksheetLine::valueText() const
{
    switch (kind) {
    case Kind::Amount:
        return amount.format(QLocale(QLocale::English, QLocale::UnitedStates), false);
    case Kind::Count:
        return QString::number(count);
    case Kind::Rate:
        return rate.toPercentString();
    case Kind::Note:
        break;
    }
    return {};
}

Money worksheetAmount(const Worksheet &sheet, const QString &line)
{
    for (const WorksheetLine &w : sheet) {
        if (w.line == line && w.kind == WorksheetLine::Kind::Amount) {
            return w.amount;
        }
    }
    return usd(0);
}

} // namespace QindaOffice::Books::Payroll
