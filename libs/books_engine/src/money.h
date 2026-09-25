// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Money: an amount in integer MINOR units (cents, pence, yen) of one ISO
// 4217 currency (D-045). Arithmetic is exact; the only operations that
// round are the ones that take a RoundingMode, and allocate() never loses
// or invents a minor unit.
//
// AGENT-GUARD: no double or float ever holds an amount. Rates, quantities
// and percentages are Decimal (decimal.h), parsed from text.
//
// AGENT-CONTRACT (storage): the .qbooks schema stores amounts as INTEGER
// minor units of the company currency (CompanySettings::currency). One
// company = one currency; multi-currency revaluation is out of scope
// (docs/books.md).

#include "decimal.h"

#include <QList>
#include <QLocale>
#include <QString>

#include <optional>

namespace QindaOffice::Books {

struct CurrencyInfo {
    QString code;    // ISO 4217 alphabetic code, upper case
    int exponent;    // minor-unit digits (USD 2, JPY 0, KWD 3)
    QString symbol;  // display symbol, falls back to the code
};

// Table lookup for the common currencies; unknown codes get exponent 2 and
// the code as symbol (a well-formed three-letter code is still accepted).
[[nodiscard]] CurrencyInfo currencyInfo(const QString &code);
[[nodiscard]] bool isKnownCurrency(const QString &code);
[[nodiscard]] QStringList knownCurrencyCodes();

class Money {
public:
    Money() = default;
    Money(qint64 minorUnits, const QString &currency);
    [[nodiscard]] static Money zero(const QString &currency) { return Money(0, currency); }

    [[nodiscard]] qint64 minor() const { return m_minor; }
    [[nodiscard]] QString currency() const { return m_currency; }
    [[nodiscard]] int exponent() const;
    [[nodiscard]] bool isZero() const { return m_minor == 0; }
    [[nodiscard]] bool isNegative() const { return m_minor < 0; }
    [[nodiscard]] bool isPositive() const { return m_minor > 0; }
    [[nodiscard]] bool sameCurrency(const Money &other) const;

    // Exact arithmetic. AGENT-GUARD: the operators assert matching
    // currencies (an empty currency is a neutral zero that adopts the
    // other side's); engine entry points check first and return
    // ErrorCode::CurrencyMismatch, so the assert only catches engine bugs.
    // Use checkedAdd() where overflow is possible (imported data).
    Money operator+(const Money &other) const;
    Money operator-(const Money &other) const;
    Money operator-() const;
    Money &operator+=(const Money &other);
    Money &operator-=(const Money &other);
    [[nodiscard]] std::optional<Money> checkedAdd(const Money &other) const;
    [[nodiscard]] Money abs() const { return m_minor < 0 ? -*this : *this; }

    // × an integer count (exact).
    [[nodiscard]] Money multipliedBy(qint64 count) const;
    // × a decimal factor (quantity "1.5", rate "8.25%"), rounded once.
    [[nodiscard]] Money multiplied(const Decimal &factor,
                                   RoundingMode mode = RoundingMode::HalfUp) const;
    // × numerator / denominator, rounded once (proportional shares).
    [[nodiscard]] Money scaled(qint64 numerator, qint64 denominator,
                               RoundingMode mode = RoundingMode::HalfUp) const;

    // Splits this amount across `weights` (any non-negative integers, e.g.
    // line amounts in minor units) by the largest-remainder method: the
    // parts sum to exactly this amount, each part differs from its exact
    // share by less than one minor unit, and ties go to the earliest
    // weight. All-zero weights split evenly.
    [[nodiscard]] QList<Money> allocate(const QList<qint64> &weights) const;
    [[nodiscard]] QList<Money> split(int parts) const;

    // "-1234.56" — the machine form (C locale, no symbol, no grouping).
    [[nodiscard]] QString toPlainString() const;
    // Locale form with grouping and, optionally, the currency symbol.
    [[nodiscard]] QString format(const QLocale &locale = QLocale::c(), bool withSymbol = true) const;

    // Parses a user- or file-supplied amount in `locale` conventions:
    // grouping separators, the currency symbol or code, a leading or
    // trailing minus and accounting parentheses are accepted. More
    // fraction digits than the currency has are refused (nullopt) unless
    // they are zeros — silently rounding typed money is a bug.
    [[nodiscard]] static std::optional<Money> parse(const QString &text, const QString &currency,
                                                    const QLocale &locale = QLocale::c());

    friend bool operator==(const Money &a, const Money &b)
    {
        return a.m_minor == b.m_minor && (a.m_currency == b.m_currency || a.m_minor == 0);
    }
    friend bool operator!=(const Money &a, const Money &b) { return !(a == b); }
    friend bool operator<(const Money &a, const Money &b) { return a.m_minor < b.m_minor; }
    friend bool operator>(const Money &a, const Money &b) { return a.m_minor > b.m_minor; }
    friend bool operator<=(const Money &a, const Money &b) { return a.m_minor <= b.m_minor; }
    friend bool operator>=(const Money &a, const Money &b) { return a.m_minor >= b.m_minor; }

private:
    [[nodiscard]] QString adoptCurrency(const Money &other) const;

    qint64 m_minor = 0;
    QString m_currency;
};

} // namespace QindaOffice::Books
