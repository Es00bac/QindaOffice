// SPDX-License-Identifier: GPL-3.0-or-later
#include "functions.h"

#include <numeric>

namespace QindaOffice::Calc {

namespace {

Value pairs(EvalContext &ctx, const QVector<Arg> &args, QVector<double> *xs, QVector<double> *ys)
{
    const auto *xView = std::get_if<RangeView>(&args.at(0));
    const auto *yView = std::get_if<RangeView>(&args.at(1));
    if (!xView || !yView) return Err::Value;
    QVector<Value> xv;
    QVector<Value> yv;
    Value err = flatRangeValues(ctx, *xView, &xv);
    if (isErr(err)) return err;
    err = flatRangeValues(ctx, *yView, &yv);
    if (isErr(err)) return err;
    const int n = qMin(xv.size(), yv.size());
    for (int i = 0; i < n; ++i) {
        const auto x = toNumber(xv.at(i));
        const auto y = toNumber(yv.at(i));
        if (x.has_value() && y.has_value() && !isText(xv.at(i)) && !isText(yv.at(i))) {
            xs->append(*x);
            ys->append(*y);
        }
    }
    return {};
}

// output: 0 slope, 1 intercept, 2 correlation, 3 coefficient of
// determination (all share one least-squares pass over aligned pairs).
FunctionSpec linreg(int output)
{
    return FunctionSpec{2, 2, false,
                        [output](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                            // SLOPE/INTERCEPT/CORREL/RSQ take
                            // (known_ys, known_xs); FORECAST builds its own.
                            QVector<double> ys;
                            QVector<double> xs;
                            const Value err = pairs(ctx, {args.at(1), args.at(0)}, &xs, &ys);
                            if (isErr(err)) return err;
                            if (xs.size() < 2) return Err::Div0;
                            const double mx =
                                std::accumulate(xs.begin(), xs.end(), 0.0) / double(xs.size());
                            const double my =
                                std::accumulate(ys.begin(), ys.end(), 0.0) / double(ys.size());
                            double sxy = 0;
                            double sxx = 0;
                            double syy = 0;
                            for (int i = 0; i < xs.size(); ++i) {
                                sxy += (xs.at(i) - mx) * (ys.at(i) - my);
                                sxx += (xs.at(i) - mx) * (xs.at(i) - mx);
                                syy += (ys.at(i) - my) * (ys.at(i) - my);
                            }
                            if (sxx == 0 || syy == 0) return Err::Div0;
                            const double slope = sxy / sxx;
                            if (output == 0) return slope;
                            if (output == 1) return my - slope * mx;
                            if (output == 2) return sxy / std::sqrt(sxx * syy);
                            return (sxy * sxy) / (sxx * syy);
                        }};
}

} // namespace

void registerRegressionFunctions(QHash<QString, FunctionSpec> &r)
{
    r.insert(QStringLiteral("SLOPE"), linreg(0));
    r.insert(QStringLiteral("INTERCEPT"), linreg(1));
    r.insert(QStringLiteral("CORREL"), linreg(2));
    r.insert(QStringLiteral("RSQ"), linreg(3));
    r.insert(QStringLiteral("FORECAST"),
             FunctionSpec{3, 3, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                              QVector<double> ys;
                              QVector<double> xs;
                              const Value err = pairs(ctx, {args.at(1), args.at(2)}, &ys, &xs);
                              if (isErr(err)) return err;
                              const auto x = scalarNumber(args.at(0));
                              if (!x) return Err::Value;
                              if (xs.size() < 2) return Err::Div0;
                              const double mx =
                                  std::accumulate(xs.begin(), xs.end(), 0.0) / double(xs.size());
                              const double my =
                                  std::accumulate(ys.begin(), ys.end(), 0.0) / double(ys.size());
                              double sxy = 0;
                              double sxx = 0;
                              for (int i = 0; i < xs.size(); ++i) {
                                  sxy += (xs.at(i) - mx) * (ys.at(i) - my);
                                  sxx += (xs.at(i) - mx) * (xs.at(i) - mx);
                              }
                              if (sxx == 0) return Err::Div0;
                              return my + (sxy / sxx) * (*x - mx);
                          }});
}

} // namespace QindaOffice::Calc
