// SPDX-License-Identifier: GPL-3.0-or-later
#include "functions.h"

namespace QindaOffice::Calc {

void registerFinancialFunctions(QHash<QString, FunctionSpec> &r)
{
    // Shared annuity solver: rate per period, payment, present/future value.
    const auto pmtLike = [](bool isFv) {
        return FunctionSpec{
            3, 5, false,
            [isFv](EvalContext &, const QVector<Arg> &args) -> Value {
                const auto rate = scalarNumber(args.at(0));
                const auto nper = scalarNumber(args.at(1));
                const auto pmt = scalarNumber(args.at(2));
                const double pv = args.size() > 3 ? scalarNumber(args.at(3)).value_or(0.0) : 0.0;
                int type = 0;
                if (args.size() > 4) {
                    const auto t = scalarNumber(args.at(4));
                    if (t) type = int(*t);
                }
                if (!rate || !nper || !pmt) return Err::Value;
                if (*rate == 0) {
                    return -(pv + *pmt * *nper);
                }
                const double factor = std::pow(1.0 + *rate, *nper);
                if (isFv) {
                    return -(*pmt * (1.0 + *rate * type) * (factor - 1.0) / *rate + pv * factor);
                }
                return -(*pmt * (1.0 + *rate * type) * (factor - 1.0) / (*rate * factor)
                         + pv / factor);
            }};
    };
    r.insert(QStringLiteral("PV"), pmtLike(false));
    r.insert(QStringLiteral("FV"), pmtLike(true));
    r.insert(QStringLiteral("PMT"),
             FunctionSpec{3, 5, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto rate = scalarNumber(args.at(0));
                              const auto nper = scalarNumber(args.at(1));
                              const auto pv = scalarNumber(args.at(2));
                              const double fv = args.size() > 3
                                  ? scalarNumber(args.at(3)).value_or(0.0)
                                  : 0.0;
                              int type = 0;
                              if (args.size() > 4) {
                                  const auto t = scalarNumber(args.at(4));
                                  if (t) type = int(*t);
                              }
                              if (!rate || !nper || !pv) return Err::Value;
                              if (*rate == 0) {
                                  return -(*pv + fv) / *nper;
                              }
                              const double factor = std::pow(1.0 + *rate, *nper);
                              return -(*pv * factor + fv) * *rate
                                  / ((1.0 + *rate * type) * (factor - 1.0));
                          }});
    r.insert(QStringLiteral("NPER"),
             FunctionSpec{3, 5, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto rate = scalarNumber(args.at(0));
                              const auto pmt = scalarNumber(args.at(1));
                              const auto pv = scalarNumber(args.at(2));
                              const double fv = args.size() > 3
                                  ? scalarNumber(args.at(3)).value_or(0.0)
                                  : 0.0;
                              int type = 0;
                              if (args.size() > 4) {
                                  const auto t = scalarNumber(args.at(4));
                                  if (t) type = int(*t);
                              }
                              if (!rate || !pmt || !pv) return Err::Value;
                              if (*rate == 0) {
                                  return -(*pv + fv) / *pmt;
                              }
                              const double num = *pmt * (1.0 + *rate * type) - fv * *rate;
                              const double den = *pv * *rate + *pmt * (1.0 + *rate * type);
                              if (num / den <= 0) return Err::Num;
                              return std::log(num / den) / std::log(1.0 + *rate);
                          }});
    r.insert(QStringLiteral("NPV"),
             FunctionSpec{2, 255, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                              const auto rate = scalarNumber(args.at(0));
                              if (!rate) return Err::Value;
                              double npv = 0;
                              int period = 0;
                              for (int i = 1; i < args.size(); ++i) {
                                  if (std::holds_alternative<RangeView>(args.at(i))) {
                                      QVector<Value> values;
                                      const Value err =
                                          flatRangeValues(ctx, std::get<RangeView>(args.at(i)), &values);
                                      if (isErr(err)) return err;
                                      for (const Value &v : values) {
                                          const auto d = toNumber(v);
                                          if (d.has_value() && !isText(v)) {
                                              npv += *d / std::pow(1.0 + *rate, ++period);
                                          }
                                      }
                                  } else {
                                      const auto d = toNumber(std::get<Value>(args.at(i)));
                                      if (d.has_value() && !isText(std::get<Value>(args.at(i)))) {
                                          npv += *d / std::pow(1.0 + *rate, ++period);
                                      }
                                  }
                              }
                              return npv;
                          }});
    r.insert(QStringLiteral("IRR"),
             FunctionSpec{1, 2, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                              QVector<Value> values;
                              if (std::holds_alternative<RangeView>(args.at(0))) {
                                  const Value err =
                                      flatRangeValues(ctx, std::get<RangeView>(args.at(0)), &values);
                                  if (isErr(err)) return err;
                              } else {
                                  values.append(std::get<Value>(args.at(0)));
                              }
                              double guess = 0.1;
                              if (args.size() > 1) {
                                  const auto g = scalarNumber(args.at(1));
                                  if (g) guess = *g;
                              }
                              QVector<double> flows;
                              for (const Value &v : values) {
                                  const auto d = toNumber(v);
                                  flows.append(d.value_or(0.0));
                              }
                              // Newton-Raphson on the NPV polynomial.
                              double rate = guess;
                              for (int iter = 0; iter < 64; ++iter) {
                                  double npv = 0;
                                  double dNpv = 0;
                                  for (int i = 0; i < flows.size(); ++i) {
                                      const double factor = std::pow(1.0 + rate, i);
                                      npv += flows.at(i) / factor;
                                      dNpv -= i * flows.at(i) / (factor * (1.0 + rate));
                                  }
                                  if (std::abs(dNpv) < 1e-12) break;
                                  const double next = rate - npv / dNpv;
                                  if (std::abs(next - rate) < 1e-10) {
                                      return next;
                                  }
                                  rate = next;
                              }
                              return Err::Num;
                          }});
}

} // namespace QindaOffice::Calc
