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

void registerFinancialExtraFunctions(QHash<QString, FunctionSpec> &r)
{
    // RATE by Newton iteration with bisection fallback; matches the
    // converged root LibreOffice reports to the fixture tolerance.
    r.insert(QStringLiteral("RATE"),
             FunctionSpec{3, 6, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto nper = scalarNumber(args.at(0));
                              const auto pmt = scalarNumber(args.at(1));
                              const auto pv = scalarNumber(args.at(2));
                              if (!nper || !pmt || !pv || *nper <= 0) return Err::Num;
                              double fv = 0;
                              double type = 0;
                              double guess = 0.1;
                              if (args.size() > 3) {
                                  const auto v = scalarNumber(args.at(3));
                                  if (!v) return Err::Value;
                                  fv = *v;
                              }
                              if (args.size() > 4) {
                                  const auto v = scalarNumber(args.at(4));
                                  if (!v) return Err::Value;
                                  type = *v;
                              }
                              if (args.size() > 5) {
                                  const auto v = scalarNumber(args.at(5));
                                  if (!v) return Err::Value;
                                  guess = *v;
                              }
                              const auto f = [&](double rate) {
                                  const double growth = std::pow(1.0 + rate, *nper);
                                  return *pv * growth + *pmt * (1.0 + rate * type)
                                             * (growth - 1.0) / rate
                                         + fv;
                              };
                              // Analytic derivative: numerical
                              // differentiation collapses near r = 0 where
                              // (growth - 1) / r loses all precision.
                              const auto df = [&](double rate) {
                                  const double growth = std::pow(1.0 + rate, *nper);
                                  const double annity = (growth - 1.0) / rate;
                                  const double dAnnity =
                                      (*nper * std::pow(1.0 + rate, *nper - 1.0) * rate
                                       - (growth - 1.0))
                                      / (rate * rate);
                                  return *pv * *nper * std::pow(1.0 + rate, *nper - 1.0)
                                      + *pmt * (type * annity + (1.0 + rate * type) * dAnnity);
                              };
                              // Degenerate loan: pv + pmt*nper + fv == 0 has
                              // its root exactly at 0 (LibreOffice reports
                              // ~1e-9, well inside the fixture tolerance).
                              if (std::abs(*pv + *pmt * *nper + fv)
                                  <= 1e-9 * (std::abs(*pv) + std::abs(fv) + 1.0)) {
                                  return 0.0;
                              }
                              double rate = guess;
                              bool converged = false;
                              for (int i = 0; i < 60; ++i) {
                                  const double fx = f(rate);
                                  const double dfx = df(rate);
                                  if (!std::isfinite(fx) || dfx == 0.0) break;
                                  const double next = rate - fx / dfx;
                                  if (std::abs(next - rate) < 1e-13) {
                                      rate = next;
                                      converged = true;
                                      break;
                                  }
                                  rate = next;
                              }
                              if (!converged || !std::isfinite(rate)
                                  || std::abs(f(rate)) > 1e-7) {
                                  // Grid-scan for sign changes, bisect each,
                                  // keep the root nearest the guess (the
                                  // same basin a Newton start would land
                                  // in). Covers double-root cashflows.
                                  double best = 0;
                                  double bestDist = 0;
                                  bool found = false;
                                  double prevR = -0.9999;
                                  double prevF = f(prevR);
                                  for (int i = 1; i <= 400; ++i) {
                                      const double r = -0.9999 + (10.0 + 0.9999) * i / 400.0;
                                      const double fr = f(r);
                                      if (std::isfinite(prevF) && std::isfinite(fr)
                                          && prevF * fr < 0) {
                                          double lo = prevR;
                                          double hi = r;
                                          double flo = prevF;
                                          for (int k = 0; k < 200; ++k) {
                                              const double mid = (lo + hi) / 2.0;
                                              const double fm = f(mid);
                                              if (std::abs(fm) < 1e-13) {
                                                  lo = hi = mid;
                                                  break;
                                              }
                                              if (flo * fm <= 0) {
                                                  hi = mid;
                                              } else {
                                                  lo = mid;
                                                  flo = fm;
                                              }
                                          }
                                          const double root = (lo + hi) / 2.0;
                                          const double dist = std::abs(root - guess);
                                          if (!found || dist < bestDist) {
                                              best = root;
                                              bestDist = dist;
                                              found = true;
                                          }
                                      }
                                      prevR = r;
                                      prevF = fr;
                                  }
                                  if (!found) {
                                      return Err::Num;
                                  }
                                  rate = best;
                              }
                              return rate;
                          }});
    r.insert(QStringLiteral("EFFECT"),
             FunctionSpec{2, 2, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto nominal = scalarNumber(args.at(0));
                              const auto periods = scalarNumber(args.at(1));
                              if (!nominal || !periods) return Err::Value;
                              if (*nominal <= 0 || *periods < 1) return Err::Num;
                              return std::pow(1.0 + *nominal / int(*periods), int(*periods)) - 1.0;
                          }});
    r.insert(QStringLiteral("NOMINAL"),
             FunctionSpec{2, 2, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto effect = scalarNumber(args.at(0));
                              const auto periods = scalarNumber(args.at(1));
                              if (!effect || !periods) return Err::Value;
                              if (*effect <= 0 || *periods < 1) return Err::Num;
                              return (std::pow(1.0 + *effect, 1.0 / int(*periods)) - 1.0)
                                  * int(*periods);
                          }});
    r.insert(QStringLiteral("FVSCHEDULE"),
             FunctionSpec{2, 2, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                              const auto principal = scalarNumber(args.at(0));
                              if (!principal) return Err::Value;
                              QVector<Value> rates;
                              const Value err =
                                  flatRangeValues(ctx, std::get<RangeView>(args.at(1)), &rates);
                              if (isErr(err)) return err;
                              double value = *principal;
                              for (const Value &rate : rates) {
                                  const auto r = toNumber(rate);
                                  if (r.has_value() && !isText(rate)) {
                                      value *= 1.0 + *r;
                                  }
                              }
                              return value;
                          }});
    r.insert(QStringLiteral("SLN"),
             FunctionSpec{3, 3, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto cost = scalarNumber(args.at(0));
                              const auto salvage = scalarNumber(args.at(1));
                              const auto life = scalarNumber(args.at(2));
                              if (!cost || !salvage || !life) return Err::Value;
                              if (*life == 0) return Err::Div0;
                              return (*cost - *salvage) / *life;
                          }});
    r.insert(QStringLiteral("SYD"),
             FunctionSpec{4, 4, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto cost = scalarNumber(args.at(0));
                              const auto salvage = scalarNumber(args.at(1));
                              const auto life = scalarNumber(args.at(2));
                              const auto period = scalarNumber(args.at(3));
                              if (!cost || !salvage || !life || !period) return Err::Value;
                              if (*life <= 0 || *period < 1 || *period > *life) return Err::Num;
                              return ((*cost - *salvage) * (*life - *period + 1) * 2.0)
                                  / (*life * (*life + 1.0));
                          }});
    r.insert(QStringLiteral("DB"),
             FunctionSpec{4, 5, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto cost = scalarNumber(args.at(0));
                              const auto salvage = scalarNumber(args.at(1));
                              const auto life = scalarNumber(args.at(2));
                              const auto period = scalarNumber(args.at(3));
                              if (!cost || !salvage || !life || !period) return Err::Value;
                              if (*cost < 0 || *life <= 0 || *period < 1) return Err::Num;
                              double month = 12;
                              if (args.size() > 4) {
                                  const auto m = scalarNumber(args.at(4));
                                  if (!m) return Err::Value;
                                  month = *m;
                              }
                              // The fixed declining rate is rounded to
                              // three decimals (Excel/LibreOffice rule).
                              const double rate =
                                  std::round((1.0 - std::pow(*salvage / *cost, 1.0 / *life))
                                             * 1000.0)
                                  / 1000.0;
                              if (*period == 1) {
                                  return *cost * rate * month / 12.0;
                              }
                              double total = *cost * rate * month / 12.0;
                              double value = *cost - total;
                              for (int p = 2; p < int(*period); ++p) {
                                  total = value * rate;
                                  value -= total;
                              }
                              if (month == 12) {
                                  total = value * rate;
                              } else {
                                  total = value * rate * (12.0 - month) / 12.0;
                              }
                              return total;
                          }});
    r.insert(QStringLiteral("DDB"),
             FunctionSpec{4, 5, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto cost = scalarNumber(args.at(0));
                              const auto salvage = scalarNumber(args.at(1));
                              const auto life = scalarNumber(args.at(2));
                              const auto period = scalarNumber(args.at(3));
                              if (!cost || !salvage || !life || !period) return Err::Value;
                              double factor = 2.0;
                              if (args.size() > 4) {
                                  const auto f = scalarNumber(args.at(4));
                                  if (!f) return Err::Value;
                                  factor = *f;
                              }
                              double total = 0;
                              double value = *cost;
                              for (int p = 1; p <= int(*period); ++p) {
                                  const double dep =
                                      qMin(value * factor / *life, value - *salvage);
                                  if (dep < 0) return 0.0;
                                  total = dep;
                                  value -= dep;
                              }
                              return total;
                          }});
}

} // namespace QindaOffice::Calc
