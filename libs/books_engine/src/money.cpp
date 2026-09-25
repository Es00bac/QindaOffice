// SPDX-License-Identifier: GPL-3.0-or-later
#include "money.h"

#include <algorithm>
#include <numeric>

namespace QindaOffice::Books {

namespace {

__extension__ typedef __int128 Wide;

struct CurrencyRow {
    const char *code;
    int exponent;
    const char *symbol; // UTF-8
};

// AGENT-NOTE: minor-unit exponents from ISO 4217 (the "E" column). The
// table covers the currencies a small business is likely to keep books
// in; anything else still works with exponent 2 (currencyInfo()).
constexpr CurrencyRow kCurrencies[] = {
    {"USD", 2, "$"},    {"EUR", 2, "€"},  {"GBP", 2, "£"},    {"CAD", 2, "CA$"},
    {"AUD", 2, "A$"},   {"NZD", 2, "NZ$"}, {"CHF", 2, "CHF"}, {"JPY", 0, "¥"},
    {"CNY", 2, "CN¥"},  {"INR", 2, "₹"},   {"KRW", 0, "₩"},   {"MXN", 2, "MX$"},
    {"BRL", 2, "R$"},   {"SEK", 2, "kr"},  {"NOK", 2, "kr"},  {"DKK", 2, "kr."},
    {"PLN", 2, "zł"},   {"CZK", 2, "Kč"},  {"HUF", 2, "Ft"},  {"ZAR", 2, "R"},
    {"SGD", 2, "S$"},   {"HKD", 2, "HK$"}, {"TWD", 2, "NT$"}, {"THB", 2, "฿"},
    {"PHP", 2, "₱"},    {"IDR", 2, "Rp"},  {"MYR", 2, "RM"},  {"VND", 0, "₫"},
    {"ILS", 2, "₪"},    {"TRY", 2, "₺"},   {"AED", 2, "AED"}, {"SAR", 2, "SAR"},
    {"BHD", 3, "BHD"},  {"KWD", 3, "KWD"}, {"OMR", 3, "OMR"}, {"JOD", 3, "JOD"},
    {"TND", 3, "TND"},  {"CLP", 0, "CLP$"}, {"COP", 2, "COL$"}, {"ARS", 2, "AR$"},
    {"PEN", 2, "S/"},   {"ISK", 0, "kr"},  {"RON", 2, "lei"}, {"BGN", 2, "лв"},
    {"UAH", 2, "₴"},    {"NGN", 2, "₦"},   {"KES", 2, "KSh"}, {"EGP", 2, "E£"},
    {"PKR", 2, "Rs"},   {"BDT", 2, "৳"},   {"XOF", 0, "CFA"}, {"XAF", 0, "FCFA"},
};

const CurrencyRow *findCurrency(const QString &code)
{
    const QByteArray upper = code.trimmed().toUpper().toLatin1();
    for (const CurrencyRow &row : kCurrencies) {
        if (upper == row.code) {
            return &row;
        }
    }
    return nullptr;
}

} // namespace

CurrencyInfo currencyInfo(const QString &code)
{
    if (const CurrencyRow *row = findCurrency(code)) {
        return {QString::fromLatin1(row->code), row->exponent, QString::fromUtf8(row->symbol)};
    }
    const QString upper = code.trimmed().toUpper();
    return {upper, 2, upper};
}

bool isKnownCurrency(const QString &code)
{
    return findCurrency(code) != nullptr;
}

QStringList knownCurrencyCodes()
{
    QStringList codes;
    for (const CurrencyRow &row : kCurrencies) {
        codes << QString::fromLatin1(row.code);
    }
    return codes;
}

Money::Money(qint64 minorUnits, const QString &currency)
    : m_minor(minorUnits)
    , m_currency(currency.trimmed().toUpper())
{
}

int Money::exponent() const
{
    return currencyInfo(m_currency).exponent;
}

bool Money::sameCurrency(const Money &other) const
{
    return m_currency == other.m_currency || m_currency.isEmpty() || other.m_currency.isEmpty();
}

QString Money::adoptCurrency(const Money &other) const
{
    Q_ASSERT_X(sameCurrency(other), "Money", "currency mismatch");
    return m_currency.isEmpty() ? other.m_currency : m_currency;
}

Money Money::operator+(const Money &other) const
{
    return Money(m_minor + other.m_minor, adoptCurrency(other));
}

Money Money::operator-(const Money &other) const
{
    return Money(m_minor - other.m_minor, adoptCurrency(other));
}

Money Money::operator-() const
{
    return Money(-m_minor, m_currency);
}

Money &Money::operator+=(const Money &other)
{
    *this = *this + other;
    return *this;
}

Money &Money::operator-=(const Money &other)
{
    *this = *this - other;
    return *this;
}

std::optional<Money> Money::checkedAdd(const Money &other) const
{
    if (!sameCurrency(other)) {
        return std::nullopt;
    }
    qint64 sum = 0;
    if (qAddOverflow(m_minor, other.m_minor, &sum)) {
        return std::nullopt;
    }
    return Money(sum, m_currency.isEmpty() ? other.m_currency : m_currency);
}

Money Money::multipliedBy(qint64 count) const
{
    return Money(m_minor * count, m_currency);
}

Money Money::multiplied(const Decimal &factor, RoundingMode mode) const
{
    const std::optional<qint64> value =
        scaleExact(m_minor, factor.mantissa(), pow10i(factor.scale()), mode);
    Q_ASSERT_X(value.has_value(), "Money::multiplied", "overflow");
    return Money(value.value_or(0), m_currency);
}

Money Money::scaled(qint64 numerator, qint64 denominator, RoundingMode mode) const
{
    const std::optional<qint64> value = scaleExact(m_minor, numerator, denominator, mode);
    return Money(value.value_or(0), m_currency);
}

QList<Money> Money::allocate(const QList<qint64> &weights) const
{
    QList<Money> parts;
    if (weights.isEmpty()) {
        return parts;
    }
    const Wide total = std::accumulate(weights.cbegin(), weights.cend(), Wide(0),
                                       [](Wide acc, qint64 w) { return acc + (w > 0 ? w : 0); });
    if (total == 0) {
        return split(int(weights.size()));
    }
    const bool negative = m_minor < 0;
    const Wide amount = negative ? -Wide(m_minor) : Wide(m_minor);
    QList<qint64> floors(weights.size());
    QList<Wide> remainders(weights.size());
    Wide assigned = 0;
    for (qsizetype i = 0; i < weights.size(); ++i) {
        const Wide w = weights.at(i) > 0 ? weights.at(i) : 0;
        const Wide exact = amount * w;
        floors[i] = static_cast<qint64>(exact / total);
        remainders[i] = exact % total;
        assigned += floors[i];
    }
    // Largest remainder first; std::stable_sort keeps ties in weight order.
    QList<qsizetype> order(weights.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(),
                     [&](qsizetype a, qsizetype b) { return remainders[a] > remainders[b]; });
    Wide left = amount - assigned;
    for (qsizetype k = 0; left > 0 && k < order.size(); ++k, --left) {
        floors[order[k]] += 1;
    }
    for (qint64 value : floors) {
        parts << Money(negative ? -value : value, m_currency);
    }
    return parts;
}

QList<Money> Money::split(int partsCount) const
{
    QList<Money> parts;
    if (partsCount <= 0) {
        return parts;
    }
    const qint64 base = m_minor / partsCount;
    qint64 left = m_minor - base * partsCount; // same sign as m_minor
    for (int i = 0; i < partsCount; ++i) {
        qint64 value = base;
        if (left > 0) {
            ++value;
            --left;
        } else if (left < 0) {
            --value;
            ++left;
        }
        parts << Money(value, m_currency);
    }
    return parts;
}

QString Money::toPlainString() const
{
    // Fixed fraction digits ("12.50", not "12.5"): Decimal does the digit
    // layout, then the missing trailing zeros are padded back.
    const int exp = exponent();
    QString text = Decimal(m_minor, exp).toString();
    if (exp == 0) {
        return text;
    }
    const qsizetype point = text.indexOf(QLatin1Char('.'));
    const qsizetype have = point < 0 ? 0 : text.size() - point - 1;
    if (point < 0) {
        text += QLatin1Char('.');
    }
    return text + QString(exp - have, QLatin1Char('0'));
}

QString Money::format(const QLocale &locale, bool withSymbol) const
{
    const int exp = exponent();
    const bool negative = m_minor < 0;
    const quint64 absValue = negative ? quint64(-(m_minor + 1)) + 1u : quint64(m_minor);
    const quint64 divisor = quint64(pow10i(exp));
    QString number = locale.toString(qulonglong(absValue / divisor));
    if (exp > 0) {
        QString fraction = QString::number(absValue % divisor);
        while (fraction.size() < exp) {
            fraction.prepend(QLatin1Char('0'));
        }
        number += locale.decimalPoint() + fraction;
    }
    if (!withSymbol) {
        return negative ? locale.negativeSign() + number : number;
    }
    // AGENT-NOTE: QLocale formats currency only for double/qlonglong. We
    // let it lay out an integer "7" (symbol side, spacing, negative form)
    // and splice our exact digits in place of the 7.
    const QString symbol = currencyInfo(m_currency).symbol;
    const QString pattern = locale.toCurrencyString(qlonglong(negative ? -7 : 7), symbol);
    const qsizetype at = pattern.lastIndexOf(QLatin1Char('7'));
    if (at < 0) {
        return (negative ? locale.negativeSign() : QString()) + symbol + number;
    }
    QString out = pattern;
    out.replace(at, 1, number);
    return out;
}

std::optional<Money> Money::parse(const QString &input, const QString &currency, const QLocale &locale)
{
    const CurrencyInfo info = currencyInfo(currency);
    QString text = input.trimmed();
    bool negative = false;
    if (text.startsWith(QLatin1Char('(')) && text.endsWith(QLatin1Char(')'))) {
        negative = true;
        text = text.mid(1, text.size() - 2).trimmed();
    }
    // Drop currency symbol/code and whitespace (incl. NBSP / narrow NBSP).
    text.remove(info.symbol);
    text.remove(info.code, Qt::CaseInsensitive);
    QString cleaned;
    const QString group = locale.groupSeparator();
    const QString point = locale.decimalPoint();
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c.isSpace() || c == QChar(0x00A0) || c == QChar(0x202F)) {
            continue;
        }
        if (text.mid(i, group.size()) == group && !group.isEmpty() && group != point) {
            i += group.size() - 1;
            continue;
        }
        if (text.mid(i, point.size()) == point) {
            cleaned += QLatin1Char('.');
            i += point.size() - 1;
            continue;
        }
        if (c == QLatin1Char('-') || c == QChar(0x2212) || c == locale.negativeSign().at(0)) {
            if (negative) {
                return std::nullopt;
            }
            negative = true;
            continue;
        }
        if (c == QLatin1Char('+')) {
            continue;
        }
        cleaned += c;
    }
    const std::optional<Decimal> value = Decimal::parse(cleaned);
    if (!value || value->isNegative()) {
        return std::nullopt;
    }
    const Decimal d = value->normalized();
    if (d.scale() > info.exponent) {
        return std::nullopt;
    }
    qint64 minor = 0;
    if (qMulOverflow(d.mantissa(), pow10i(info.exponent - d.scale()), &minor)) {
        return std::nullopt;
    }
    return Money(negative ? -minor : minor, info.code);
}

} // namespace QindaOffice::Books
