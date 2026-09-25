// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Exact decimal factors (quantities, prices per unit, tax and discount
// rates) for the money arithmetic in money.h. A Decimal is mantissa ×
// 10^-scale, parsed from text — "1.5", "8.25%", "-0.125" — never from a
// binary floating-point value.
//
// AGENT-GUARD: nothing in books_engine may use double/float for an amount
// or a factor applied to one; tst_books_money greps the sources for it.
// Rounding happens in exactly one place, roundedDivide(), with the mode
// the caller names.

#include <QString>
#include <QtGlobal>

#include <optional>

namespace QindaOffice::Books {

enum class RoundingMode {
    HalfUp = 0,   // ties away from zero (the commercial default)
    HalfEven = 1, // banker's rounding, only where a tax rule says so
    TowardZero = 2,
    AwayFromZero = 3,
};

class Decimal {
public:
    static constexpr int kMaxScale = 12;

    constexpr Decimal() = default;
    constexpr Decimal(qint64 mantissa, int scale)
        : m_mantissa(mantissa)
        , m_scale(scale)
    {
    }
    static constexpr Decimal fromInteger(qint64 value) { return Decimal(value, 0); }

    // Parses "12", "-1.5", "0.0825", "8.25%" (percent divides by 100 by
    // raising the scale). Surrounding spaces are allowed; anything else —
    // exponents, thousands separators, more than kMaxScale fraction digits
    // — is refused with nullopt.
    [[nodiscard]] static std::optional<Decimal> parse(const QString &text);

    [[nodiscard]] qint64 mantissa() const { return m_mantissa; }
    [[nodiscard]] int scale() const { return m_scale; }
    [[nodiscard]] bool isZero() const { return m_mantissa == 0; }
    [[nodiscard]] bool isNegative() const { return m_mantissa < 0; }
    // Canonical text with trailing fraction zeros removed ("1.50" -> "1.5").
    [[nodiscard]] QString toString() const;
    // As a percentage ("0.0825" -> "8.25%").
    [[nodiscard]] QString toPercentString() const;
    [[nodiscard]] Decimal normalized() const;

    // Value compare, independent of scale (1.50 == 1.5).
    friend bool operator==(const Decimal &a, const Decimal &b);

private:
    qint64 m_mantissa = 0;
    int m_scale = 0;
};

// value × numerator / denominator rounded by `mode`, in 128-bit
// intermediate precision. Returns nullopt on a zero denominator or when
// the result does not fit in 64 bits.
[[nodiscard]] std::optional<qint64> scaleExact(qint64 value, qint64 numerator,
                                               qint64 denominator, RoundingMode mode);

[[nodiscard]] qint64 pow10i(int exponent);

} // namespace QindaOffice::Books
