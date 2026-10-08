// SPDX-License-Identifier: GPL-3.0-or-later
// Math functions added for Excel interchange (M61, D-069): the Excel 2010/
// 2013 rounding family (CEILING.MATH, FLOOR.MATH, CEILING.PRECISE,
// FLOOR.PRECISE) and the sums of squares (SUMSQ, SUMX2MY2, SUMX2PY2,
// SUMXMY2).

#include "functions.h"

#include "../eval_array.h"

#include <cmath>

namespace QindaOffice::Calc {

namespace {

// x / step, snapped to the nearest integer when it is one up to rounding
// noise (0.3 / 0.1 is 2.9999999999999996 in binary, and Excel says 3).
double quotient(double x, double step)
{
    const double q = x / step;
    const double nearest = std::round(q);
    return std::abs(q - nearest) <= 1e-12 * qMax(1.0, std::abs(q)) ? nearest : q;
}

// CEILING.MATH / FLOOR.MATH(number, [significance], [mode]): the sign of
// the significance is ignored; for a negative number, mode 0 rounds
// CEILING toward zero and FLOOR away from it, any other mode the reverse.
// The .PRECISE twins are the same with mode fixed at 0 and no third argument.
FunctionSpec rounding(bool ceiling, bool precise)
{
    return FunctionSpec{1, precise ? 2 : 3, false, [ceiling](EvalContext &, const QVector<Arg> &args) -> Value {
        double v[3] = {0.0, 1.0, 0.0};
        for (int i = 0; i < args.size(); ++i) {
            const Value value = scalarOrValue(args.at(i));
            if (isErr(value)) return value;
            const auto d = toNumber(value); // a blank cell counts as 0, as in Excel
            if (!d) return Err::Value;
            v[i] = *d;
        }
        const double x = v[0];
        const double step = std::abs(v[1]);
        if (step == 0.0 || x == 0.0) return 0.0;
        const double q = quotient(x, step);
        if (x > 0.0 || v[2] == 0.0) {
            return (ceiling ? std::ceil(q) : std::floor(q)) * step;
        }
        // Negative number, mode set: the magnitude rounds the other way.
        return -(ceiling ? std::ceil(-q) : std::floor(-q)) * step;
    }};
}

// The paired sums: Σ f(x, y) over positions where both sides are numbers.
FunctionSpec pairedSum(double (*term)(double, double))
{
    return FunctionSpec{2, 2, false, [term](EvalContext &ctx, const QVector<Arg> &args) -> Value {
        QVector<Value> xs;
        QVector<Value> ys;
        if (const Value err = flatArgValues(ctx, args.at(0), &xs); isErr(err)) return err;
        if (const Value err = flatArgValues(ctx, args.at(1), &ys); isErr(err)) return err;
        if (xs.size() != ys.size()) return Err::NA;
        double sum = 0.0;
        for (qsizetype i = 0; i < xs.size(); ++i) {
            if (isNumber(xs.at(i)) && isNumber(ys.at(i))) {
                sum += term(std::get<double>(xs.at(i)), std::get<double>(ys.at(i)));
            }
        }
        return sum;
    }};
}

} // namespace

void registerMathMoreFunctions(QHash<QString, FunctionSpec> &r)
{
    r.insert(QStringLiteral("CEILING.MATH"), rounding(true, false));
    r.insert(QStringLiteral("FLOOR.MATH"), rounding(false, false));
    r.insert(QStringLiteral("CEILING.PRECISE"), rounding(true, true));
    r.insert(QStringLiteral("FLOOR.PRECISE"), rounding(false, true));
    // SUMSQ: the squares of every number the arguments carry (text skipped,
    // the rule SUM uses).
    r.insert(QStringLiteral("SUMSQ"),
             FunctionSpec{1, 255, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                 double sum = 0.0;
                 for (const Arg &arg : args) {
                     QVector<Value> values;
                     if (const Value err = sparseArgValues(ctx, arg, &values); isErr(err)) return err;
                     for (const Value &v : std::as_const(values)) {
                         if (isText(v)) continue;
                         if (const auto d = toNumber(v)) sum += *d * *d;
                     }
                 }
                 return sum;
             }});
    r.insert(QStringLiteral("SUMX2MY2"), pairedSum([](double x, double y) { return x * x - y * y; }));
    r.insert(QStringLiteral("SUMX2PY2"), pairedSum([](double x, double y) { return x * x + y * y; }));
    r.insert(QStringLiteral("SUMXMY2"), pairedSum([](double x, double y) { return (x - y) * (x - y); }));
}

} // namespace QindaOffice::Calc
