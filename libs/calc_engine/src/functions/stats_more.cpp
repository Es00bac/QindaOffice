// SPDX-License-Identifier: GPL-3.0-or-later
// Statistical functions added for Excel interchange (M61, D-069): the
// Excel 2010 spellings (PERCENTILE.INC/EXC, QUARTILE.INC/EXC, MODE.SNGL,
// RANK.AVG, COVARIANCE.P/S), DEVSQ, and the normal distribution family
// (NORM.DIST, NORM.S.DIST, NORM.INV, NORM.S.INV, STANDARDIZE and their
// pre-2010 names). Registered after statistical.cpp, so MODE here replaces
// the old one, whose tie-break followed hash order instead of the data.

#include "functions.h"

#include "../eval_array.h"

#include <cmath>
#include <numeric>

namespace QindaOffice::Calc {

namespace {

// Every number the arguments carry, in data order. Text is skipped (the
// engine's rule for aggregates, shared with SUM); the first error wins.
Value orderedNumbers(const EvalContext &ctx, const QVector<Arg> &args, qsizetype first, qsizetype last,
                     QVector<double> *out)
{
    for (qsizetype i = first; i <= last && i < args.size(); ++i) {
        QVector<Value> values;
        if (const Value err = flatArgValues(ctx, args.at(i), &values); isErr(err)) {
            return err;
        }
        for (const Value &v : std::as_const(values)) {
            if (isBlank(v) || isText(v)) {
                continue;
            }
            if (const auto d = toNumber(v)) {
                out->append(*d);
            }
        }
    }
    return {};
}

// Linear interpolation at a fractional 0-based rank of sorted data.
double interpolate(const QVector<double> &sorted, double rank)
{
    const auto lower = qsizetype(std::floor(rank));
    const qsizetype upper = qMin(lower + 1, sorted.size() - 1);
    return sorted.at(lower) + (sorted.at(upper) - sorted.at(lower)) * (rank - double(lower));
}

// PERCENTILE.EXC rank rule: k*(n+1) must land inside 1..n.
Value percentileExclusive(QVector<double> nums, double k)
{
    std::sort(nums.begin(), nums.end());
    const double n = double(nums.size());
    if (nums.isEmpty() || k <= 0.0 || k >= 1.0) return Err::Num;
    const double rank = k * (n + 1.0);
    if (rank < 1.0 || rank > n) return Err::Num;
    return interpolate(nums, rank - 1.0);
}

FunctionSpec exclusive(bool quartile)
{
    return FunctionSpec{2, 2, false, [quartile](EvalContext &ctx, const QVector<Arg> &args) -> Value {
        QVector<double> nums;
        if (const Value err = orderedNumbers(ctx, args, 0, 0, &nums); isErr(err)) return err;
        const Value kValue = scalarOrValue(args.at(1));
        if (isErr(kValue)) return kValue;
        const auto k = toNumber(kValue);
        if (!k) return Err::Value;
        if (!quartile) return percentileExclusive(nums, *k);
        const double quart = std::floor(*k);
        if (quart <= 0.0 || quart >= 4.0) return Err::Num;
        return percentileExclusive(nums, quart / 4.0);
    }};
}

// Pairs of numbers from two equally shaped arguments, skipping any pair
// where either side is not a number (Excel's rule for COVARIANCE/CORREL).
Value numberPairs(const EvalContext &ctx, const Arg &a, const Arg &b, QVector<double> *xs,
                  QVector<double> *ys)
{
    QVector<Value> av;
    QVector<Value> bv;
    if (const Value err = flatArgValues(ctx, a, &av); isErr(err)) return err;
    if (const Value err = flatArgValues(ctx, b, &bv); isErr(err)) return err;
    if (av.size() != bv.size()) return Err::NA;
    for (qsizetype i = 0; i < av.size(); ++i) {
        if (isNumber(av.at(i)) && isNumber(bv.at(i))) {
            xs->append(std::get<double>(av.at(i)));
            ys->append(std::get<double>(bv.at(i)));
        }
    }
    return {};
}

FunctionSpec covariance(bool sample)
{
    return FunctionSpec{2, 2, false, [sample](EvalContext &ctx, const QVector<Arg> &args) -> Value {
        QVector<double> xs;
        QVector<double> ys;
        if (const Value err = numberPairs(ctx, args.at(0), args.at(1), &xs, &ys); isErr(err)) return err;
        const double n = double(xs.size());
        if (n < (sample ? 2.0 : 1.0)) return Err::Div0;
        const double mx = std::accumulate(xs.begin(), xs.end(), 0.0) / n;
        const double my = std::accumulate(ys.begin(), ys.end(), 0.0) / n;
        double acc = 0.0;
        for (qsizetype i = 0; i < xs.size(); ++i) {
            acc += (xs.at(i) - mx) * (ys.at(i) - my);
        }
        return acc / (sample ? n - 1.0 : n);
    }};
}

// Wichura's AS241 (PPND16): the inverse standard normal to about 1e-16,
// the algorithm R's qnorm uses.
double inverseStandardNormal(double p)
{
    const double q = p - 0.5;
    if (std::abs(q) <= 0.425) {
        const double r = 0.180625 - q * q;
        return q * (((((((r * 2509.0809287301226727 + 33430.575583588128105) * r + 67265.770927008700853) * r
                         + 45921.953931549871457) * r + 13731.693765509461125) * r + 1971.5909503065514427) * r
                     + 133.14166789178437745) * r + 3.387132872796366608)
            / (((((((r * 5226.495278852545925 + 28729.085735721942674) * r + 39307.89580009271061) * r
                   + 21213.794301586595867) * r + 5394.1960214247511077) * r + 687.1870074920579083) * r
                 + 42.313330701600911252) * r + 1.0);
    }
    double r = std::sqrt(-std::log(q < 0.0 ? p : 1.0 - p));
    double value = 0.0;
    if (r <= 5.0) {
        r -= 1.6;
        value = (((((((r * 7.7454501427834140764e-4 + 0.0227238449892691845833) * r + 0.24178072517745061177) * r
                     + 1.27045825245236838258) * r + 3.64784832476320460504) * r + 5.7694972214606914055) * r
                  + 4.6303378461565452959) * r + 1.42343711074968357734)
            / (((((((r * 1.05075007164441684324e-9 + 5.475938084995344946e-4) * r + 0.0151986665636164571966) * r
                   + 0.14810397642748007459) * r + 0.68976733498510000455) * r + 1.6763848301838038494) * r
                 + 2.05319162663775882187) * r + 1.0);
    } else {
        r -= 5.0;
        value = (((((((r * 2.01033439929228813265e-7 + 2.71155556874348757815e-5) * r + 0.0012426609473880784386) * r
                     + 0.026532189526576123093) * r + 0.29656057182850489123) * r + 1.7848265399172913358) * r
                  + 5.4637849111641143699) * r + 6.6579046435011037772)
            / (((((((r * 2.04426310338993978564e-15 + 1.4215117583164458887e-7) * r + 1.8463183175100546818e-5) * r
                   + 7.868691311456132591e-4) * r + 0.0148753612908506148525) * r + 0.13692988092273580531) * r
                 + 0.59983220655588793769) * r + 1.0);
    }
    return q < 0.0 ? -value : value;
}

double standardNormal(double z, bool cumulative)
{
    return cumulative ? 0.5 * std::erfc(-z / std::sqrt(2.0)) : std::exp(-0.5 * z * z) / std::sqrt(2.0 * M_PI);
}

// The leading arguments as numbers (errors propagate, text is #VALUE!).
Value scalars(const QVector<Arg> &args, int count, double *out)
{
    for (int i = 0; i < count; ++i) {
        const Value v = scalarOrValue(args.at(i));
        if (isErr(v)) return v;
        const auto d = toNumber(v);
        if (!d) return Err::Value;
        out[i] = *d;
    }
    return {};
}

FunctionSpec normalDistribution(bool standard, bool cumulativeArgument)
{
    const int count = (standard ? 1 : 3) + (cumulativeArgument ? 1 : 0);
    return FunctionSpec{count, count, false, [standard, cumulativeArgument](EvalContext &, const QVector<Arg> &args) -> Value {
        double v[4] = {0, 0, 1, 1};
        if (const Value err = scalars(args, int(args.size()), v); isErr(err)) return err;
        const double mean = standard ? 0.0 : v[1];
        const double sd = standard ? 1.0 : v[2];
        const bool cumulative = !cumulativeArgument || v[args.size() - 1] != 0.0;
        if (sd <= 0.0) return Err::Num;
        const double density = standardNormal((v[0] - mean) / sd, cumulative);
        return cumulative ? density : density / sd;
    }};
}

FunctionSpec normalInverse(bool standard)
{
    const int count = standard ? 1 : 3;
    return FunctionSpec{count, count, false, [standard](EvalContext &, const QVector<Arg> &args) -> Value {
        double v[3] = {0, 0, 1};
        if (const Value err = scalars(args, int(args.size()), v); isErr(err)) return err;
        const double mean = standard ? 0.0 : v[1];
        const double sd = standard ? 1.0 : v[2];
        if (v[0] <= 0.0 || v[0] >= 1.0 || sd <= 0.0) return Err::Num;
        return mean + sd * inverseStandardNormal(v[0]);
    }};
}

} // namespace

void registerStatisticalMoreFunctions(QHash<QString, FunctionSpec> &r)
{
    r.insert(QStringLiteral("PERCENTILE.INC"), r.value(QStringLiteral("PERCENTILE")));
    r.insert(QStringLiteral("QUARTILE.INC"), r.value(QStringLiteral("QUARTILE")));
    r.insert(QStringLiteral("PERCENTILE.EXC"), exclusive(false));
    r.insert(QStringLiteral("QUARTILE.EXC"), exclusive(true));
    // MODE: the most frequent value; on a tie, the one met first in the
    // data (Excel's rule — the old version's choice followed hash order).
    const FunctionSpec mode{1, 255, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
        QVector<double> nums;
        if (const Value err = orderedNumbers(ctx, args, 0, args.size() - 1, &nums); isErr(err)) return err;
        QHash<double, int> counts;
        for (const double x : std::as_const(nums)) {
            ++counts[x];
        }
        int best = 1;
        std::optional<double> winner;
        for (const double x : std::as_const(nums)) {
            if (counts.value(x) > best) {
                best = counts.value(x);
                winner = x;
            }
        }
        return winner.has_value() ? Value(*winner) : Value(Err::NA);
    }};
    r.insert(QStringLiteral("MODE"), mode);
    r.insert(QStringLiteral("MODE.SNGL"), mode);
    // RANK.AVG(number, ref, [order]): ties share the average of their ranks.
    r.insert(QStringLiteral("RANK.AVG"),
             FunctionSpec{2, 3, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                 const Value xValue = scalarOrValue(args.at(0));
                 if (isErr(xValue)) return xValue;
                 const auto x = toNumber(xValue);
                 if (!x) return Err::Value;
                 QVector<double> nums;
                 if (const Value err = orderedNumbers(ctx, args, 1, 1, &nums); isErr(err)) return err;
                 const bool ascending = args.size() > 2 && scalarNumber(args.at(2)).value_or(0.0) != 0.0;
                 double before = 0.0;
                 double equal = 0.0;
                 for (const double n : std::as_const(nums)) {
                     if (n == *x) {
                         equal += 1.0;
                     } else if (ascending ? n < *x : n > *x) {
                         before += 1.0;
                     }
                 }
                 if (equal == 0.0) return Err::NA;
                 return before + (equal + 1.0) / 2.0;
             }});
    r.insert(QStringLiteral("COVARIANCE.P"), covariance(false));
    r.insert(QStringLiteral("COVAR"), covariance(false));
    r.insert(QStringLiteral("COVARIANCE.S"), covariance(true));
    // DEVSQ: the sum of squared deviations from the mean.
    r.insert(QStringLiteral("DEVSQ"),
             FunctionSpec{1, 255, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                 QVector<double> nums;
                 if (const Value err = orderedNumbers(ctx, args, 0, args.size() - 1, &nums); isErr(err)) return err;
                 if (nums.isEmpty()) return Err::Num;
                 const double mean = std::accumulate(nums.begin(), nums.end(), 0.0) / double(nums.size());
                 double acc = 0.0;
                 for (const double x : std::as_const(nums)) {
                     acc += (x - mean) * (x - mean);
                 }
                 return acc;
             }});
    r.insert(QStringLiteral("NORM.DIST"), normalDistribution(false, true));
    r.insert(QStringLiteral("NORMDIST"), normalDistribution(false, true));
    r.insert(QStringLiteral("NORM.S.DIST"), normalDistribution(true, true));
    r.insert(QStringLiteral("NORMSDIST"), normalDistribution(true, false));
    r.insert(QStringLiteral("NORM.INV"), normalInverse(false));
    r.insert(QStringLiteral("NORMINV"), normalInverse(false));
    r.insert(QStringLiteral("NORM.S.INV"), normalInverse(true));
    r.insert(QStringLiteral("NORMSINV"), normalInverse(true));
    r.insert(QStringLiteral("STANDARDIZE"),
             FunctionSpec{3, 3, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                 double v[3] = {0, 0, 0};
                 if (const Value err = scalars(args, 3, v); isErr(err)) return err;
                 if (v[2] <= 0.0) return Err::Num;
                 return (v[0] - v[1]) / v[2];
             }});
}

} // namespace QindaOffice::Calc
