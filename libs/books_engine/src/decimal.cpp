// SPDX-License-Identifier: GPL-3.0-or-later
#include "decimal.h"

#include <algorithm>
#include <limits>

namespace QindaOffice::Books {

namespace {

// AGENT-NOTE: __int128 is a GCC/Clang extension; `__extension__` keeps
// -Wpedantic quiet. Every product here is two qint64 values, so 128 bits
// never overflow before the final range check.
__extension__ typedef __int128 Wide;

Wide roundedDivide(Wide numerator, Wide denominator, RoundingMode mode)
{
    if (denominator < 0) {
        numerator = -numerator;
        denominator = -denominator;
    }
    Wide quotient = numerator / denominator; // truncates toward zero
    const Wide remainder = numerator % denominator;
    if (remainder == 0) {
        return quotient;
    }
    const bool negative = numerator < 0;
    const Wide absRemainder = remainder < 0 ? -remainder : remainder;
    const Wide twice = absRemainder * 2;
    bool awayFromZero = false;
    switch (mode) {
    case RoundingMode::TowardZero:
        awayFromZero = false;
        break;
    case RoundingMode::AwayFromZero:
        awayFromZero = true;
        break;
    case RoundingMode::HalfUp:
        awayFromZero = twice >= denominator;
        break;
    case RoundingMode::HalfEven: {
        const bool odd = (quotient % 2) != 0;
        awayFromZero = twice > denominator || (twice == denominator && odd);
        break;
    }
    }
    if (awayFromZero) {
        quotient += negative ? -1 : 1;
    }
    return quotient;
}

} // namespace

qint64 pow10i(int exponent)
{
    qint64 value = 1;
    for (int i = 0; i < exponent; ++i) {
        value *= 10;
    }
    return value;
}

std::optional<qint64> scaleExact(qint64 value, qint64 numerator, qint64 denominator,
                                 RoundingMode mode)
{
    if (denominator == 0) {
        return std::nullopt;
    }
    const Wide product = static_cast<Wide>(value) * static_cast<Wide>(numerator);
    const Wide result = roundedDivide(product, denominator, mode);
    if (result > std::numeric_limits<qint64>::max() || result < std::numeric_limits<qint64>::min()) {
        return std::nullopt;
    }
    return static_cast<qint64>(result);
}

std::optional<Decimal> Decimal::parse(const QString &input)
{
    QString text = input.trimmed();
    int extraScale = 0;
    if (text.endsWith(QLatin1Char('%'))) {
        text.chop(1);
        text = text.trimmed();
        extraScale = 2;
    }
    if (text.isEmpty()) {
        return std::nullopt;
    }
    bool negative = false;
    int pos = 0;
    if (text.at(0) == QLatin1Char('-') || text.at(0) == QLatin1Char('+')) {
        negative = text.at(0) == QLatin1Char('-');
        pos = 1;
    }
    qint64 mantissa = 0;
    int scale = 0;
    bool seenPoint = false;
    bool seenDigit = false;
    for (; pos < text.size(); ++pos) {
        const QChar c = text.at(pos);
        if (c == QLatin1Char('.')) {
            if (seenPoint) {
                return std::nullopt;
            }
            seenPoint = true;
            continue;
        }
        if (!c.isDigit() || c.unicode() > 127) {
            return std::nullopt;
        }
        seenDigit = true;
        if (mantissa > (std::numeric_limits<qint64>::max() - 9) / 10) {
            return std::nullopt;
        }
        mantissa = mantissa * 10 + (c.unicode() - '0');
        if (seenPoint) {
            ++scale;
        }
    }
    if (!seenDigit || scale + extraScale > kMaxScale) {
        return std::nullopt;
    }
    return Decimal(negative ? -mantissa : mantissa, scale + extraScale).normalized();
}

Decimal Decimal::normalized() const
{
    qint64 mantissa = m_mantissa;
    int scale = m_scale;
    while (scale > 0 && mantissa % 10 == 0) {
        mantissa /= 10;
        --scale;
    }
    return Decimal(mantissa, scale);
}

QString Decimal::toString() const
{
    const Decimal n = normalized();
    const bool negative = n.m_mantissa < 0;
    // Absolute value as text without overflow on INT64_MIN.
    QString digits = QString::number(static_cast<quint64>(negative ? -(n.m_mantissa + 1) : n.m_mantissa)
                                     + (negative ? 1u : 0u));
    if (n.m_scale > 0) {
        while (digits.size() <= n.m_scale) {
            digits.prepend(QLatin1Char('0'));
        }
        digits.insert(digits.size() - n.m_scale, QLatin1Char('.'));
    }
    return negative ? QLatin1Char('-') + digits : digits;
}

QString Decimal::toPercentString() const
{
    // ×100 is a scale shift, never a multiplication that could round.
    if (m_scale >= 2) {
        return Decimal(m_mantissa, m_scale - 2).toString() + QLatin1Char('%');
    }
    return Decimal(m_mantissa * pow10i(2 - m_scale), 0).toString() + QLatin1Char('%');
}

namespace {

// Both mantissas at the larger scale, in 128 bits (scales are ≤ kMaxScale,
// so 10^12 × a qint64 still fits).
struct Aligned {
    Wide a = 0;
    Wide b = 0;
    int scale = 0;
};

Aligned align(const Decimal &a, const Decimal &b)
{
    const int scale = std::max(a.scale(), b.scale());
    return Aligned{static_cast<Wide>(a.mantissa()) * pow10i(scale - a.scale()),
                   static_cast<Wide>(b.mantissa()) * pow10i(scale - b.scale()), scale};
}

std::optional<Decimal> fromWide(Wide mantissa, int scale)
{
    if (mantissa > std::numeric_limits<qint64>::max() || mantissa < std::numeric_limits<qint64>::min()) {
        return std::nullopt;
    }
    return Decimal(static_cast<qint64>(mantissa), scale).normalized();
}

} // namespace

std::optional<Decimal> Decimal::sum(const Decimal &a, const Decimal &b)
{
    const Aligned x = align(a, b);
    return fromWide(x.a + x.b, x.scale);
}

std::optional<Decimal> Decimal::difference(const Decimal &a, const Decimal &b)
{
    const Aligned x = align(a, b);
    return fromWide(x.a - x.b, x.scale);
}

int Decimal::compare(const Decimal &a, const Decimal &b)
{
    const Aligned x = align(a, b);
    return x.a < x.b ? -1 : x.a > x.b ? 1 : 0;
}

bool operator==(const Decimal &a, const Decimal &b)
{
    const Decimal x = a.normalized();
    const Decimal y = b.normalized();
    return x.m_mantissa == y.m_mantissa && x.m_scale == y.m_scale;
}

} // namespace QindaOffice::Books
