// SPDX-License-Identifier: GPL-3.0-or-later
// M69 (D-078): the engineering functions business workbooks use — base
// conversions (DEC2BIN … HEX2OCT, with Excel's ten-digit two's complement
// for negatives), DELTA, GESTEP, BITAND, BITOR, BITXOR, BITLSHIFT,
// BITRSHIFT — and DOLLAR. Expected values from headless LibreOffice where
// it agrees with Microsoft's documentation; DOLLAR's negative spelling is
// Excel's ("($1,234.57)") and asserted by hand.

#include "functions.h"

#include <cmath>

namespace QindaOffice::Calc {

namespace {

struct Base {
    int radix;
    int bits;      // the two's complement width: 10 digits of the base
    double low;    // the smallest number the base's DEC2X accepts
    double high;
};

const Base kBin{2, 10, -512, 511};
const Base kOct{8, 30, -536870912, 536870911};
const Base kHex{16, 40, -549755813888.0, 549755813887.0};

std::optional<double> number(const Arg &arg, Value *error)
{
    const Value v = scalarOrValue(arg);
    if (isErr(v)) {
        *error = v;
        return std::nullopt;
    }
    if (isBlank(v)) {
        return 0.0;
    }
    const auto n = toNumber(v);
    if (!n) {
        *error = Err::Value;
    }
    return n;
}

// A decimal written in `to`: negatives as ten digits of two's complement,
// positives padded to `places` (more digits than places is #NUM!).
Value toBase(double value, const Base &to, const Arg *placesArg)
{
    const double n = std::trunc(value);
    if (n < to.low || n > to.high) {
        return Err::Num;
    }
    const qint64 bits = n < 0 ? qint64(std::ldexp(1.0, to.bits)) + qint64(n) : qint64(n);
    QString digits = QString::number(bits, to.radix).toUpper();
    if (placesArg != nullptr && n >= 0) {
        Value error;
        const auto places = number(*placesArg, &error);
        if (!places) {
            return error;
        }
        const int width = int(std::trunc(*places));
        if (width <= 0 || width < digits.size()) {
            return Err::Num;
        }
        digits = digits.rightJustified(width, QLatin1Char('0'));
    }
    return digits;
}

// A number written in `from` (text, or a number whose digits are the
// text): ten digits with the top one set are negative.
Value fromBase(const Arg &arg, const Base &from)
{
    const Value v = scalarOrValue(arg);
    if (isErr(v)) {
        return v;
    }
    QString text = isNumber(v) ? QString::number(qint64(std::get<double>(v))) : toText(v).trimmed();
    if (text.isEmpty()) {
        return 0.0;
    }
    if (text.size() > 10) {
        return Err::Num;
    }
    bool ok = false;
    const qint64 raw = text.toLongLong(&ok, from.radix);
    if (!ok || raw < 0) {
        return Err::Num;
    }
    const qint64 top = qint64(1) << (from.bits - 1);
    return double(text.size() == 10 && raw >= top ? raw - (top << 1) : raw);
}

FunctionSpec convert(const Base *from, const Base &to)
{
    return FunctionSpec{1, 2, false, [from, &to](EvalContext &, const QVector<Arg> &args) -> Value {
        const Arg *places = args.size() > 1 ? &args.at(1) : nullptr;
        if (from == nullptr) {
            Value error;
            const auto n = number(args.at(0), &error);
            return n ? toBase(*n, to, places) : error;
        }
        const Value decimal = fromBase(args.at(0), *from);
        return isNumber(decimal) ? toBase(std::get<double>(decimal), to, places) : decimal;
    }};
}

// BITAND/BITOR/BITXOR and the shifts: whole numbers from 0 to 2^48 - 1.
std::optional<qint64> bitNumber(const Arg &arg, Value *error)
{
    const auto n = number(arg, error);
    if (!n) {
        return std::nullopt;
    }
    if (*n < 0 || *n != std::trunc(*n) || *n >= std::ldexp(1.0, 48)) {
        *error = Err::Num;
        return std::nullopt;
    }
    return qint64(*n);
}

FunctionSpec bitwise(std::function<qint64(qint64, qint64)> op)
{
    return FunctionSpec{2, 2, false, [op](EvalContext &, const QVector<Arg> &args) -> Value {
        Value error;
        const auto a = bitNumber(args.at(0), &error);
        const auto b = a ? bitNumber(args.at(1), &error) : std::nullopt;
        return a && b ? Value(double(op(*a, *b))) : error;
    }};
}

FunctionSpec shift(int direction)
{
    return FunctionSpec{2, 2, false, [direction](EvalContext &, const QVector<Arg> &args) -> Value {
        Value error;
        const auto n = bitNumber(args.at(0), &error);
        const auto by = n ? number(args.at(1), &error) : std::nullopt;
        if (!n || !by) {
            return error;
        }
        const double amount = std::trunc(*by) * direction;
        if (std::abs(amount) > 53) {
            return Err::Num;
        }
        const double shifted = std::floor(std::ldexp(double(*n), int(amount)));
        return shifted >= std::ldexp(1.0, 48) ? Value(Err::Num) : Value(shifted);
    }};
}

// "$1,234.57", negatives in brackets — Excel's DOLLAR in an en-US locale.
QString dollarText(double value, int decimals)
{
    const double factor = std::pow(10.0, decimals);
    const double rounded = std::copysign(std::floor(std::abs(value) * factor + 0.5) / factor, value);
    QString digits = QString::number(std::abs(rounded), 'f', qMax(0, decimals));
    const qsizetype point = digits.indexOf(QLatin1Char('.'));
    for (qsizetype i = (point < 0 ? digits.size() : point) - 3; i > 0; i -= 3) {
        digits.insert(i, QLatin1Char(','));
    }
    const QString text = QLatin1Char('$') + digits;
    return rounded < 0 ? QLatin1Char('(') + text + QLatin1Char(')') : text;
}

} // namespace

void registerEngineeringFunctions(QHash<QString, FunctionSpec> &r)
{
    r.insert(QStringLiteral("DEC2BIN"), convert(nullptr, kBin));
    r.insert(QStringLiteral("DEC2OCT"), convert(nullptr, kOct));
    r.insert(QStringLiteral("DEC2HEX"), convert(nullptr, kHex));
    r.insert(QStringLiteral("BIN2OCT"), convert(&kBin, kOct));
    r.insert(QStringLiteral("BIN2HEX"), convert(&kBin, kHex));
    r.insert(QStringLiteral("OCT2BIN"), convert(&kOct, kBin));
    r.insert(QStringLiteral("OCT2HEX"), convert(&kOct, kHex));
    r.insert(QStringLiteral("HEX2BIN"), convert(&kHex, kBin));
    r.insert(QStringLiteral("HEX2OCT"), convert(&kHex, kOct));
    for (const auto &entry : {std::pair<const char *, const Base *>{"BIN2DEC", &kBin}, {"OCT2DEC", &kOct},
                              {"HEX2DEC", &kHex}}) {
        const Base *base = entry.second;
        r.insert(QString::fromLatin1(entry.first),
                 FunctionSpec{1, 1, false, [base](EvalContext &, const QVector<Arg> &args) -> Value {
                                  return fromBase(args.at(0), *base);
                              }});
    }
    const auto compareTo = [](bool greaterOrEqual) {
        return FunctionSpec{1, 2, false, [greaterOrEqual](EvalContext &, const QVector<Arg> &args) -> Value {
            Value error;
            const auto a = number(args.at(0), &error);
            const auto b = args.size() > 1 ? number(args.at(1), &error) : std::optional<double>(0.0);
            if (!a || !b) {
                return error;
            }
            return greaterOrEqual ? (*a >= *b ? 1.0 : 0.0) : (*a == *b ? 1.0 : 0.0);
        }};
    };
    r.insert(QStringLiteral("DELTA"), compareTo(false));
    r.insert(QStringLiteral("GESTEP"), compareTo(true));
    r.insert(QStringLiteral("BITAND"), bitwise([](qint64 a, qint64 b) { return a & b; }));
    r.insert(QStringLiteral("BITOR"), bitwise([](qint64 a, qint64 b) { return a | b; }));
    r.insert(QStringLiteral("BITXOR"), bitwise([](qint64 a, qint64 b) { return a ^ b; }));
    r.insert(QStringLiteral("BITLSHIFT"), shift(1));
    r.insert(QStringLiteral("BITRSHIFT"), shift(-1));
    r.insert(QStringLiteral("DOLLAR"),
             FunctionSpec{1, 2, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              Value error;
                              const auto value = number(args.at(0), &error);
                              const auto decimals = args.size() > 1 ? number(args.at(1), &error) : std::optional<double>(2);
                              if (!value || !decimals) {
                                  return error;
                              }
                              if (*decimals > 127) {
                                  return Err::Value;
                              }
                              return dollarText(*value, int(std::trunc(*decimals)));
                          }});
}

} // namespace QindaOffice::Calc
