// SPDX-License-Identifier: GPL-3.0-or-later
// M69 (D-078): statistical functions Excel workbooks use that the engine
// lacked — AGGREGATE, FREQUENCY, PERCENTRANK(.INC/.EXC), KURT, SKEW,
// SKEW.P, PERMUT, PERMUTATIONA, COMBINA, CONFIDENCE(.NORM),
// BINOM.DIST/BINOMDIST, POISSON.DIST/POISSON, EXPON.DIST/EXPONDIST,
// GAMMA, GAMMALN(.PRECISE), FISHER, FISHERINV and PEARSON. Expected values
// come from headless LibreOffice where it agrees with Microsoft's
// documentation (tests/fixtures/functions/functions_m69.tsv).

#include "functions.h"
#include "aggregate_refs.h"

#include "../eval_array.h"

#include <cmath>
#include <numeric>

namespace QindaOffice::Calc {

namespace {

// Every number the arguments carry, in data order; text, booleans and
// blanks in ranges are skipped; the first error wins.
Value numbersOf(const EvalContext &ctx, const QVector<Arg> &args, qsizetype first, qsizetype last,
                QVector<double> *out)
{
    for (qsizetype i = first; i <= last && i < args.size(); ++i) {
        QVector<Value> values;
        if (const Value err = flatArgValues(ctx, args.at(i), &values); isErr(err)) {
            return err;
        }
        const bool direct = std::holds_alternative<Value>(args.at(i));
        for (const Value &v : std::as_const(values)) {
            if (isNumber(v)) {
                out->append(std::get<double>(v));
            } else if (direct && (isBool(v) || isText(v))) {
                // A value typed as an argument counts (Excel), text only
                // when it reads as a number.
                const auto n = toNumber(v);
                if (!n) {
                    return Err::Value;
                }
                out->append(*n);
            }
        }
    }
    return {};
}

std::optional<double> number(const Arg &arg, Value *error)
{
    const Value v = scalarOrValue(arg);
    if (isErr(v)) {
        *error = v;
        return std::nullopt;
    }
    const auto n = toNumber(v);
    if (!n) {
        *error = Err::Value;
    }
    return n;
}

double mean(const QVector<double> &xs)
{
    return std::accumulate(xs.begin(), xs.end(), 0.0) / double(xs.size());
}

double sampleStdev(const QVector<double> &xs, double m)
{
    double ss = 0;
    for (const double x : xs) {
        ss += (x - m) * (x - m);
    }
    return std::sqrt(ss / double(xs.size() - 1));
}

// n choose k as COMBIN computes it (a product, exact for the sizes
// workbooks use).
double combin(double n, double k)
{
    double out = 1;
    for (double i = 1; i <= k; ++i) {
        out = out * (n - k + i) / i;
    }
    return std::round(out);
}

// PERCENTRANK: the rank of x among sorted data, interpolated between
// neighbours, truncated to `digits` significant decimals.
Value percentRank(QVector<double> xs, double x, int digits, bool exclusive)
{
    if (xs.isEmpty() || digits < 1) {
        return Err::Num;
    }
    std::sort(xs.begin(), xs.end());
    if (x < xs.first() || x > xs.last()) {
        return Err::NA;
    }
    const double n = double(xs.size());
    const auto below = [&xs](double v) {
        return double(std::lower_bound(xs.begin(), xs.end(), v) - xs.begin());
    };
    double rank = 0;
    const auto hit = std::lower_bound(xs.begin(), xs.end(), x);
    if (*hit == x) {
        rank = exclusive ? (below(x) + 1) / (n + 1) : (n > 1 ? below(x) / (n - 1) : 1.0);
    } else {
        const double lo = *(hit - 1);
        const double hi = *hit;
        const double i = below(x) - 1; // index of lo
        const double position = i + (x - lo) / (hi - lo);
        rank = exclusive ? (position + 1) / (n + 1) : position / (n - 1);
    }
    const double scale = std::pow(10.0, digits);
    return std::floor(rank * scale + 1e-9) / scale;
}

FunctionSpec percentRankSpec(bool exclusive)
{
    return FunctionSpec{2, 3, false, [exclusive](EvalContext &ctx, const QVector<Arg> &args) -> Value {
        QVector<double> xs;
        if (const Value err = numbersOf(ctx, args, 0, 0, &xs); isErr(err)) {
            return err;
        }
        Value error;
        const auto x = number(args.at(1), &error);
        const auto digits = args.size() > 2 ? number(args.at(2), &error) : std::optional<double>(3);
        if (!x || !digits) {
            return error;
        }
        return percentRank(xs, *x, int(std::trunc(*digits)), exclusive);
    }};
}

// AGGREGATE(function, options, ref1, …) / AGGREGATE(14..19, options,
// array, k): the named aggregate over the arguments, leaving out nested
// SUBTOTAL/AGGREGATE cells (options 0–3) and error values (2, 3, 6, 7).
// Filtered rows are omitted; options 1, 3, 5, 7 also omit manually hidden rows.
Arg aggregate(EvalContext &ctx, const QVector<Arg> &args)
{
    static const char *const kNames[] = {"AVERAGE", "COUNT", "COUNTA", "MAX", "MIN", "PRODUCT", "STDEV.S",
                                         "STDEV.P", "SUM", "VAR.S", "VAR.P", "MEDIAN", "MODE.SNGL", "LARGE",
                                         "SMALL", "PERCENTILE.INC", "QUARTILE.INC", "PERCENTILE.EXC",
                                         "QUARTILE.EXC"};
    Value error;
    const auto which = number(args.at(0), &error);
    const auto options = number(args.at(1), &error);
    if (!which || !options) {
        return error;
    }
    const int fn = int(*which);
    const int option = int(*options);
    if (fn < 1 || fn > 19 || option < 0 || option > 7 || (fn >= 14 && args.size() != 4)) {
        return Value(Err::Value);
    }
    const bool skipErrors = option == 2 || option == 3 || option == 6 || option == 7;
    const bool skipNested = option <= 3;
    const qsizetype last = fn >= 14 ? 2 : args.size() - 1;
    ArrayValue kept{1, 0, {}};
    for (qsizetype i = 2; i <= last; ++i) {
        const auto keep = [&](const Value &v) -> std::optional<Value> {
            if (isErr(v) && !skipErrors) {
                return v;
            }
            if (!isErr(v) && !isBlank(v)) {
                kept.cells.append(v);
            }
            return std::nullopt;
        };
        if (const auto *view = std::get_if<RangeView>(&args.at(i))) {
            for (const auto &[cell, value] : ctx.host->nonBlankCellsIn(view->sheet, view->range)) {
                if (ctx.host->rowExcluded(view->sheet, cell.row, option % 2 == 1)
                    || (skipNested && nestedSubtotal(*ctx.host, view->sheet, cell))) continue;
                if (const auto err = keep(value)) return *err;
            }
            continue;
        }
        ArrayValue grid;
        if (const Value failed = arrayFromArg(ctx, args.at(i), &grid); isErr(failed)) {
            return failed;
        }
        for (const Value &v : std::as_const(grid.cells)) {
            if (const auto err = keep(v)) {
                return *err;
            }
        }
    }
    kept.cols = qint32(kept.cells.size());
    if (kept.cells.isEmpty()) {
        // Nothing left: what the function gives for no numbers.
        switch (fn) {
        case 2: case 3: case 4: case 5: case 6: case 9: return Value(0.0);
        case 13: return Value(Err::NA);
        case 14: case 15: case 16: case 17: case 18: case 19: return Value(Err::Num);
        default: return Value(Err::Div0);
        }
    }
    const FunctionSpec spec = functionRegistry().value(QString::fromLatin1(kNames[fn - 1]));
    QVector<Arg> inner{Arg(kept)};
    if (fn >= 14) {
        inner.append(args.at(3));
    }
    return spec.fn(ctx, inner);
}

} // namespace

void registerStatisticalM69Functions(QHash<QString, FunctionSpec> &r)
{
    r.insert(QStringLiteral("AGGREGATE"), FunctionSpec{3, 255, false, aggregate, true});
    r.insert(QStringLiteral("FREQUENCY"),
             FunctionSpec{2, 2, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Arg {
                              QVector<double> data;
                              QVector<double> bins;
                              if (const Value err = numbersOf(ctx, args, 0, 0, &data); isErr(err)) {
                                  return err;
                              }
                              if (const Value err = numbersOf(ctx, args, 1, 1, &bins); isErr(err)) {
                                  return err;
                              }
                              // Each bin counts what is above the next lower bin and up to
                              // itself; one more count for what is above every bin.
                              QVector<double> sorted = bins;
                              std::sort(sorted.begin(), sorted.end());
                              ArrayValue out{qint32(bins.size()) + 1, 1, {}};
                              for (const double bin : std::as_const(bins)) {
                                  const auto pos = std::lower_bound(sorted.begin(), sorted.end(), bin) - sorted.begin();
                                  const double lower = pos > 0 ? sorted.at(pos - 1) : -INFINITY;
                                  out.cells.append(double(std::count_if(data.begin(), data.end(), [&](double x) {
                                      return x > lower && x <= bin;
                                  })));
                              }
                              const double top = sorted.isEmpty() ? -INFINITY : sorted.last();
                              out.cells.append(double(std::count_if(data.begin(), data.end(), [top](double x) { return x > top; })));
                              return out;
                          }});
    r.insert(QStringLiteral("PERCENTRANK.INC"), percentRankSpec(false));
    r.insert(QStringLiteral("PERCENTRANK"), percentRankSpec(false));
    r.insert(QStringLiteral("PERCENTRANK.EXC"), percentRankSpec(true));
    const auto moment = [](bool kurtosis, bool population) {
        return FunctionSpec{1, 255, false, [kurtosis, population](EvalContext &ctx, const QVector<Arg> &args) -> Value {
            QVector<double> xs;
            if (const Value err = numbersOf(ctx, args, 0, args.size() - 1, &xs); isErr(err)) {
                return err;
            }
            const double n = double(xs.size());
            if (n < (kurtosis ? 4 : 3) && !population) {
                return Err::Div0;
            }
            if (n < 1) {
                return Err::Div0;
            }
            const double m = mean(xs);
            const double s = population ? std::sqrt(std::accumulate(xs.begin(), xs.end(), 0.0, [m](double acc, double x) {
                return acc + (x - m) * (x - m);
            }) / n)
                                        : sampleStdev(xs, m);
            if (s == 0.0) {
                return Err::Div0;
            }
            double sum = 0;
            for (const double x : std::as_const(xs)) {
                sum += std::pow((x - m) / s, kurtosis ? 4 : 3);
            }
            if (population) {
                return sum / n;
            }
            if (!kurtosis) {
                return n / ((n - 1) * (n - 2)) * sum;
            }
            return n * (n + 1) / ((n - 1) * (n - 2) * (n - 3)) * sum - 3 * (n - 1) * (n - 1) / ((n - 2) * (n - 3));
        }};
    };
    r.insert(QStringLiteral("KURT"), moment(true, false));
    r.insert(QStringLiteral("SKEW"), moment(false, false));
    r.insert(QStringLiteral("SKEW.P"), moment(false, true));
    const auto twoNumbers = [](std::function<Value(double, double)> f) {
        return FunctionSpec{2, 2, false, [f](EvalContext &, const QVector<Arg> &args) -> Value {
            Value error;
            const auto a = number(args.at(0), &error);
            const auto b = number(args.at(1), &error);
            return a && b ? f(*a, *b) : error;
        }};
    };
    r.insert(QStringLiteral("PERMUT"), twoNumbers([](double n, double k) -> Value {
                 n = std::trunc(n);
                 k = std::trunc(k);
                 if (n < 0 || k < 0 || n < k) return Err::Num;
                 double out = 1;
                 for (double i = 0; i < k; ++i) out *= n - i;
                 return out;
             }));
    r.insert(QStringLiteral("PERMUTATIONA"), twoNumbers([](double n, double k) -> Value {
                 n = std::trunc(n);
                 k = std::trunc(k);
                 if (n < 0 || k < 0) return Err::Num;
                 return std::pow(n, k);
             }));
    r.insert(QStringLiteral("COMBINA"), twoNumbers([](double n, double k) -> Value {
                 n = std::trunc(n);
                 k = std::trunc(k);
                 if (n < 0 || k < 0 || (n == 0 && k > 0)) return Err::Num;
                 return k == 0 ? 1.0 : combin(n + k - 1, k);
             }));
    const FunctionSpec confidence{3, 3, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
        Value error;
        const auto alpha = number(args.at(0), &error);
        const auto sd = number(args.at(1), &error);
        const auto size = number(args.at(2), &error);
        if (!alpha || !sd || !size) return error;
        if (*alpha <= 0 || *alpha >= 1 || *sd <= 0 || std::trunc(*size) < 1) return Err::Num;
        const Value z = scalarOrValue(functionRegistry().value(QStringLiteral("NORM.S.INV")).fn(ctx, {Value(1 - *alpha / 2)}));
        return isNumber(z) ? Value(std::get<double>(z) * *sd / std::sqrt(std::trunc(*size))) : z;
    }};
    r.insert(QStringLiteral("CONFIDENCE.NORM"), confidence);
    r.insert(QStringLiteral("CONFIDENCE"), confidence);
    const FunctionSpec binom{4, 4, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
        Value error;
        const auto k = number(args.at(0), &error);
        const auto n = number(args.at(1), &error);
        const auto p = number(args.at(2), &error);
        const auto cumulative = toBool(scalarOrValue(args.at(3)));
        if (!k || !n || !p) return error;
        if (!cumulative) return Err::Value;
        const double kk = std::trunc(*k);
        const double nn = std::trunc(*n);
        if (kk < 0 || kk > nn || *p < 0 || *p > 1) return Err::Num;
        const auto pmf = [&](double i) { return combin(nn, i) * std::pow(*p, i) * std::pow(1 - *p, nn - i); };
        if (!*cumulative) return pmf(kk);
        double sum = 0;
        for (double i = 0; i <= kk; ++i) sum += pmf(i);
        return sum;
    }};
    r.insert(QStringLiteral("BINOM.DIST"), binom);
    r.insert(QStringLiteral("BINOMDIST"), binom);
    const FunctionSpec poisson{3, 3, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
        Value error;
        const auto x = number(args.at(0), &error);
        const auto lambda = number(args.at(1), &error);
        const auto cumulative = toBool(scalarOrValue(args.at(2)));
        if (!x || !lambda) return error;
        if (!cumulative) return Err::Value;
        const double k = std::trunc(*x);
        if (k < 0 || *lambda < 0) return Err::Num;
        const auto pmf = [&](double i) { return std::exp(i * std::log(*lambda) - *lambda - std::lgamma(i + 1)); };
        if (*lambda == 0) return 1.0 * (k >= 0 && (*cumulative || k == 0));
        if (!*cumulative) return pmf(k);
        double sum = 0;
        for (double i = 0; i <= k; ++i) sum += pmf(i);
        return sum;
    }};
    r.insert(QStringLiteral("POISSON.DIST"), poisson);
    r.insert(QStringLiteral("POISSON"), poisson);
    const FunctionSpec expon{3, 3, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
        Value error;
        const auto x = number(args.at(0), &error);
        const auto lambda = number(args.at(1), &error);
        const auto cumulative = toBool(scalarOrValue(args.at(2)));
        if (!x || !lambda) return error;
        if (!cumulative) return Err::Value;
        if (*x < 0 || *lambda <= 0) return Err::Num;
        return *cumulative ? 1 - std::exp(-*lambda * *x) : *lambda * std::exp(-*lambda * *x);
    }};
    r.insert(QStringLiteral("EXPON.DIST"), expon);
    r.insert(QStringLiteral("EXPONDIST"), expon);
    const auto oneNumber = [](std::function<Value(double)> f) {
        return FunctionSpec{1, 1, false, [f](EvalContext &, const QVector<Arg> &args) -> Value {
            Value error;
            const auto x = number(args.at(0), &error);
            return x ? f(*x) : error;
        }};
    };
    const FunctionSpec gammaLn = oneNumber([](double x) -> Value { return x <= 0 ? Value(Err::Num) : Value(std::lgamma(x)); });
    r.insert(QStringLiteral("GAMMALN"), gammaLn);
    r.insert(QStringLiteral("GAMMALN.PRECISE"), gammaLn);
    r.insert(QStringLiteral("GAMMA"), oneNumber([](double x) -> Value {
                 return (x <= 0 && x == std::trunc(x)) ? Value(Err::Num) : Value(std::tgamma(x));
             }));
    r.insert(QStringLiteral("FISHER"), oneNumber([](double x) -> Value {
                 return std::abs(x) >= 1 ? Value(Err::Num) : Value(0.5 * std::log((1 + x) / (1 - x)));
             }));
    r.insert(QStringLiteral("FISHERINV"), oneNumber([](double y) -> Value { return std::tanh(y); }));
    r.insert(QStringLiteral("PEARSON"), r.value(QStringLiteral("CORREL")));
}

} // namespace QindaOffice::Calc
