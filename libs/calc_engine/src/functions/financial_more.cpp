// SPDX-License-Identifier: GPL-3.0-or-later
// Financial functions Excel workbooks reach for that the engine lacked
// (M61, D-069): the interest/principal split of an annuity payment (IPMT,
// PPMT, CUMIPMT, CUMPRINC), dated cash flows (XNPV, XIRR), MIRR, PDURATION
// and RRI. The annuity arithmetic follows LibreOffice's ScInterpreter, which
// matches Excel to the last digit; expected values in the function fixture
// come from headless LibreOffice (tools/lo_oracle.py).

#include "functions.h"

#include "../eval_array.h"

#include <cmath>

namespace QindaOffice::Calc {

namespace {

// Payment per period of an annuity (Excel's PMT), `type` 1 = in advance.
double annuityPayment(double rate, double nper, double pv, double fv, bool advance)
{
    if (rate == 0.0) {
        return -(pv + fv) / nper;
    }
    const double factor = std::pow(1.0 + rate, nper);
    return -(pv * factor + fv) * rate / ((1.0 + rate * (advance ? 1.0 : 0.0)) * (factor - 1.0));
}

// Future value after `nper` periods (Excel's FV).
double futureValue(double rate, double nper, double pmt, double pv, bool advance)
{
    if (rate == 0.0) {
        return -(pv + pmt * nper);
    }
    const double factor = std::pow(1.0 + rate, nper);
    return -(pv * factor + pmt * (1.0 + rate * (advance ? 1.0 : 0.0)) * (factor - 1.0) / rate);
}

// The interest part of payment `per` (1-based), given the payment.
double interestPart(double rate, double per, double pmt, double pv, bool advance)
{
    if (per == 1.0) {
        return advance ? 0.0 : -pv * rate;
    }
    return advance ? (futureValue(rate, per - 2.0, pmt, pv, true) - pmt) * rate
                   : futureValue(rate, per - 1.0, pmt, pv, false) * rate;
}

std::optional<double> optionalNumber(const QVector<Arg> &args, int index, double fallback)
{
    if (index >= args.size()) {
        return fallback;
    }
    const Value v = scalarOrValue(args.at(index));
    return isBlank(v) ? std::optional<double>(fallback) : toNumber(v);
}

// IPMT/PPMT(rate, per, nper, pv, [fv], [type]).
FunctionSpec paymentPart(bool interest)
{
    return FunctionSpec{4, 6, false, [interest](EvalContext &, const QVector<Arg> &args) -> Value {
        for (const Arg &arg : args) {
            if (const Value v = scalarOrValue(arg); isErr(v)) return v;
        }
        const auto rate = scalarNumber(args.at(0));
        const auto per = scalarNumber(args.at(1));
        const auto nper = scalarNumber(args.at(2));
        const auto pv = scalarNumber(args.at(3));
        const auto fv = optionalNumber(args, 4, 0.0);
        const auto type = optionalNumber(args, 5, 0.0);
        if (!rate || !per || !nper || !pv || !fv || !type) return Err::Value;
        if (*per < 1.0 || *per > *nper || *nper <= 0.0) return Err::Num;
        const bool advance = *type != 0.0;
        const double pmt = annuityPayment(*rate, *nper, *pv, *fv, advance);
        const double ipmt = interestPart(*rate, *per, pmt, *pv, advance);
        return interest ? ipmt : pmt - ipmt;
    }};
}

// CUMIPMT/CUMPRINC(rate, nper, pv, start, end, type): every argument is
// required and Excel refuses non-positive rates and values with #NUM!.
FunctionSpec cumulative(bool interest)
{
    return FunctionSpec{6, 6, false, [interest](EvalContext &, const QVector<Arg> &args) -> Value {
        double v[6];
        for (int i = 0; i < 6; ++i) {
            const Value value = scalarOrValue(args.at(i));
            if (isErr(value)) return value;
            const auto number = toNumber(value);
            if (!number) return Err::Value;
            v[i] = *number;
        }
        const double rate = v[0];
        const double nper = v[1];
        const double pv = v[2];
        const double start = std::floor(v[3]);
        const double end = std::floor(v[4]);
        const double type = v[5];
        if (rate <= 0.0 || nper <= 0.0 || pv <= 0.0 || start < 1.0 || end < start || end > nper
            || (type != 0.0 && type != 1.0)) {
            return Err::Num;
        }
        const bool advance = type == 1.0;
        const double pmt = annuityPayment(rate, nper, pv, 0.0, advance);
        double total = 0.0;
        for (double per = start; per <= end; per += 1.0) {
            const double ipmt = interestPart(rate, per, pmt, pv, advance);
            total += interest ? ipmt : pmt - ipmt;
        }
        return total;
    }};
}

// Values and dates of XNPV/XIRR: two equally long lists of numbers. Dates
// truncate to whole days; any date before the first one is #NUM!.
Value datedFlows(const EvalContext &ctx, const Arg &valuesArg, const Arg &datesArg,
                 QVector<double> *values, QVector<double> *days)
{
    QVector<Value> rawValues;
    QVector<Value> rawDates;
    if (const Value err = flatArgValues(ctx, valuesArg, &rawValues); isErr(err)) return err;
    if (const Value err = flatArgValues(ctx, datesArg, &rawDates); isErr(err)) return err;
    if (rawValues.size() != rawDates.size() || rawValues.isEmpty()) return Err::Num;
    for (qsizetype i = 0; i < rawValues.size(); ++i) {
        const auto value = toNumber(rawValues.at(i));
        const auto date = toNumber(rawDates.at(i));
        if (!value || !date || isText(rawValues.at(i)) || isText(rawDates.at(i))) return Err::Value;
        values->append(*value);
        days->append(std::floor(*date));
    }
    for (const double day : std::as_const(*days)) {
        if (day < days->first()) return Err::Num;
    }
    return {};
}

double xnpv(double rate, const QVector<double> &values, const QVector<double> &days)
{
    double sum = 0.0;
    for (qsizetype i = 0; i < values.size(); ++i) {
        sum += values.at(i) / std::pow(1.0 + rate, (days.at(i) - days.first()) / 365.0);
    }
    return sum;
}

} // namespace

void registerFinancialMoreFunctions(QHash<QString, FunctionSpec> &r)
{
    r.insert(QStringLiteral("IPMT"), paymentPart(true));
    r.insert(QStringLiteral("PPMT"), paymentPart(false));
    r.insert(QStringLiteral("CUMIPMT"), cumulative(true));
    r.insert(QStringLiteral("CUMPRINC"), cumulative(false));
    r.insert(QStringLiteral("XNPV"),
             FunctionSpec{3, 3, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                 const Value rateValue = scalarOrValue(args.at(0));
                 if (isErr(rateValue)) return rateValue;
                 const auto rate = toNumber(rateValue);
                 if (!rate) return Err::Value;
                 if (*rate <= -1.0) return Err::Num;
                 QVector<double> values;
                 QVector<double> days;
                 if (const Value err = datedFlows(ctx, args.at(1), args.at(2), &values, &days); isErr(err)) {
                     return err;
                 }
                 return xnpv(*rate, values, days);
             }});
    r.insert(QStringLiteral("XIRR"),
             FunctionSpec{2, 3, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                 QVector<double> values;
                 QVector<double> days;
                 if (const Value err = datedFlows(ctx, args.at(0), args.at(1), &values, &days); isErr(err)) {
                     return err;
                 }
                 bool positive = false;
                 bool negative = false;
                 for (const double v : std::as_const(values)) {
                     positive = positive || v > 0.0;
                     negative = negative || v < 0.0;
                 }
                 if (!positive || !negative) return Err::Num;
                 const auto guess = optionalNumber(args, 2, 0.1);
                 if (!guess) return Err::Value;
                 // Newton-Raphson on XNPV(rate) = 0, Excel's 1e-8 tolerance.
                 double rate = *guess;
                 for (int iteration = 0; iteration < 100; ++iteration) {
                     if (rate <= -1.0) return Err::Num;
                     double f = 0.0;
                     double df = 0.0;
                     for (qsizetype i = 0; i < values.size(); ++i) {
                         const double t = (days.at(i) - days.first()) / 365.0;
                         const double discount = std::pow(1.0 + rate, t);
                         f += values.at(i) / discount;
                         df -= t * values.at(i) / (discount * (1.0 + rate));
                     }
                     if (df == 0.0) return Err::Num;
                     const double next = rate - f / df;
                     if (std::abs(next - rate) < 1e-10) return next;
                     rate = next;
                 }
                 return Err::Num;
             }});
    // MIRR(values, finance_rate, reinvest_rate): numbers only, zeroes kept.
    r.insert(QStringLiteral("MIRR"),
             FunctionSpec{3, 3, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                 QVector<Value> raw;
                 if (const Value err = flatArgValues(ctx, args.at(0), &raw); isErr(err)) return err;
                 const auto finance = scalarNumber(args.at(1));
                 const auto reinvest = scalarNumber(args.at(2));
                 if (!finance || !reinvest) return Err::Value;
                 double npvPositive = 0.0;
                 double npvNegative = 0.0;
                 double n = 0.0;
                 for (const Value &v : std::as_const(raw)) {
                     if (!isNumber(v)) continue;
                     const double x = std::get<double>(v);
                     n += 1.0;
                     if (x > 0.0) {
                         npvPositive += x / std::pow(1.0 + *reinvest, n);
                     } else if (x < 0.0) {
                         npvNegative += x / std::pow(1.0 + *finance, n);
                     }
                 }
                 if (npvPositive == 0.0 || npvNegative == 0.0 || n < 2.0) return Err::Div0;
                 const double ratio = -npvPositive * std::pow(1.0 + *reinvest, n)
                     / (npvNegative * (1.0 + *finance));
                 return std::pow(ratio, 1.0 / (n - 1.0)) - 1.0;
             }});
    // PDURATION(rate, pv, fv): periods for pv to grow to fv.
    r.insert(QStringLiteral("PDURATION"),
             FunctionSpec{3, 3, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                 const auto rate = scalarNumber(args.at(0));
                 const auto pv = scalarNumber(args.at(1));
                 const auto fv = scalarNumber(args.at(2));
                 if (!rate || !pv || !fv) return Err::Value;
                 if (*rate <= 0.0 || *pv <= 0.0 || *fv <= 0.0) return Err::Num;
                 return (std::log(*fv) - std::log(*pv)) / std::log1p(*rate);
             }});
    // RRI(nper, pv, fv): the equivalent interest rate for a growth.
    r.insert(QStringLiteral("RRI"),
             FunctionSpec{3, 3, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                 const auto nper = scalarNumber(args.at(0));
                 const auto pv = scalarNumber(args.at(1));
                 const auto fv = scalarNumber(args.at(2));
                 if (!nper || !pv || !fv) return Err::Value;
                 if (*nper <= 0.0 || *pv == 0.0) return Err::Num;
                 const double rate = std::pow(*fv / *pv, 1.0 / *nper) - 1.0;
                 return std::isfinite(rate) ? Value(rate) : Value(Err::Num);
             }});
}

} // namespace QindaOffice::Calc
