// SPDX-License-Identifier: GPL-3.0-or-later
#include "functions.h"

#include <numeric>

namespace QindaOffice::Calc {

namespace {

// Rounds half away from zero, matching Excel.
double roundHalfAway(double value, int digits)
{
    const double factor = std::pow(10.0, digits);
    return std::copysign(std::floor(std::abs(value) * factor + 0.5) / factor, value);
}

Value numberAgg(const EvalContext &ctx, const QVector<Arg> &args,
                const std::function<Value(const QVector<double> &)> &finish,
                bool skipTextAndBlank = true)
{
    QVector<double> numbers;
    for (const Arg &arg : args) {
        if (std::holds_alternative<RangeView>(arg)) {
            QVector<Value> values;
            const Value err = sparseRangeValues(ctx, std::get<RangeView>(arg), &values);
            if (isErr(err)) {
                return err;
            }
            for (const Value &v : values) {
                if (const auto d = toNumber(v);
                    d.has_value() && !(skipTextAndBlank && isText(v))) {
                    numbers.append(*d);
                }
            }
        } else {
            const Value v = std::get<Value>(arg);
            if (const auto d = toNumber(v);
                d.has_value() && !(skipTextAndBlank && isText(v))) {
                numbers.append(*d);
            }
        }
    }
    return finish(numbers);
}

} // namespace

void registerStatisticalFunctions(QHash<QString, FunctionSpec> &r)
{
    r.insert(QStringLiteral("AVERAGE"),
             FunctionSpec{1, 255, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                              return numberAgg(ctx, args, [](const QVector<double> &ns) -> Value {
                                  if (ns.isEmpty()) return Err::Div0;
                                  return std::accumulate(ns.begin(), ns.end(), 0.0)
                                      / double(ns.size());
                              });
                          }});
    r.insert(QStringLiteral("COUNT"),
             FunctionSpec{1, 255, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                              return numberAgg(ctx, args, [](const QVector<double> &ns) -> Value {
                                  return double(ns.size());
                              });
                          }});
    r.insert(QStringLiteral("MAX"),
             FunctionSpec{1, 255, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                              return numberAgg(ctx, args, [](const QVector<double> &ns) -> Value {
                                  if (ns.isEmpty()) return 0.0;
                                  return *std::max_element(ns.begin(), ns.end());
                              });
                          }});
    r.insert(QStringLiteral("MIN"),
             FunctionSpec{1, 255, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                              return numberAgg(ctx, args, [](const QVector<double> &ns) -> Value {
                                  if (ns.isEmpty()) return 0.0;
                                  return *std::min_element(ns.begin(), ns.end());
                              });
                          }});
    r.insert(QStringLiteral("COUNTA"),
             FunctionSpec{1, 255, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                              double count = 0;
                              for (const Arg &arg : args) {
                                  if (std::holds_alternative<RangeView>(arg)) {
                                      QVector<Value> values;
                                      const Value err =
                                          sparseRangeValues(ctx, std::get<RangeView>(arg), &values);
                                      if (isErr(err)) return err;
                                      count += double(values.size());
                                  } else if (!isBlank(std::get<Value>(arg))) {
                                      ++count;
                                  }
                              }
                              return count;
                          }});
    r.insert(QStringLiteral("COUNTBLANK"),
             FunctionSpec{1, 1, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                              const auto *view = std::get_if<RangeView>(&args.at(0));
                              if (!view) return Err::Value;
                              const auto cells = ctx.host->nonBlankCellsIn(view->sheet, view->range);
                              return double(view->range.cellCount() - cells.size());
                          }});
    const auto medianLike = [](bool median) {
        return FunctionSpec{1, 255, false,
                            [median](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                                return numberAgg(ctx, args,
                                                 [median](const QVector<double> &ns) -> Value {
                                                     if (ns.isEmpty()) return Err::Div0;
                                                     QVector<double> sorted = ns;
                                                     std::sort(sorted.begin(), sorted.end());
                                                     const int mid = sorted.size() / 2;
                                                     if (median) {
                                                         return sorted.size() % 2
                                                             ? sorted.at(mid)
                                                             : (sorted.at(mid - 1) + sorted.at(mid))
                                                                   / 2.0;
                                                     }
                                                     return sorted.at(mid);
                                                 });
                            }};
    };
    r.insert(QStringLiteral("MEDIAN"), medianLike(true));
    r.insert(QStringLiteral("MODE"), medianLike(false));
    const auto dev = [](bool sample, bool variance) {
        return FunctionSpec{1, 255, false,
                            [sample, variance](EvalContext &ctx,
                                               const QVector<Arg> &args) -> Value {
                                return numberAgg(ctx, args,
                                                 [sample, variance](const QVector<double> &ns)
                                                     -> Value {
                                                     const int n = int(ns.size());
                                                     if (n < (sample ? 2 : 1)) return Err::Div0;
                                                     const double mean =
                                                         std::accumulate(ns.begin(), ns.end(), 0.0)
                                                         / double(n);
                                                     double acc = 0;
                                                     for (double x : ns) acc += (x - mean) * (x - mean);
                                                     const double denom = double(sample ? n - 1 : n);
                                                     return variance ? acc / denom
                                                                     : std::sqrt(acc / denom);
                                                 });
                            }};
    };
    r.insert(QStringLiteral("STDEV"), dev(true, false));
    r.insert(QStringLiteral("STDEV.S"), dev(true, false));
    r.insert(QStringLiteral("STDEVP"), dev(false, false));
    r.insert(QStringLiteral("STDEV.P"), dev(false, false));
    r.insert(QStringLiteral("VAR"), dev(true, true));
    r.insert(QStringLiteral("VAR.S"), dev(true, true));
    r.insert(QStringLiteral("VARP"), dev(false, true));
    r.insert(QStringLiteral("VAR.P"), dev(false, true));
    const auto nth = [](bool largest) {
        return FunctionSpec{2, 2, false,
                            [largest](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                                QVector<Value> values;
                                Value err;
                                if (std::holds_alternative<RangeView>(args.at(0))) {
                                    err = sparseRangeValues(ctx, std::get<RangeView>(args.at(0)),
                                                            &values);
                                } else {
                                    values.append(std::get<Value>(args.at(0)));
                                }
                                if (isErr(err)) return err;
                                const auto k = scalarNumber(args.at(1));
                                if (!k) return Err::Value;
                                QVector<double> nums;
                                for (const Value &v : values) {
                                    if (const auto d = toNumber(v); d.has_value() && !isText(v)) {
                                        nums.append(*d);
                                    }
                                }
                                if (*k < 1 || *k > nums.size()) return Err::Num;
                                std::sort(nums.begin(), nums.end());
                                return largest ? nums.at(nums.size() - int(*k))
                                               : nums.at(int(*k) - 1);
                            }};
    };
    r.insert(QStringLiteral("LARGE"), nth(true));
    r.insert(QStringLiteral("SMALL"), nth(false));
    // SUMIF/AVERAGEIF take criteria over range and - when a third range is
    // given - sum the aligned cells of that range instead.
    const auto ifAgg = [](std::function<double(double, double)> fold, bool average) {
        return FunctionSpec{2, 3, false,
                            [fold, average](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                                QVector<Value> values;
                                const Value err = flatRangeValues(
                                    ctx, std::get<RangeView>(args.at(0)), &values);
                                if (isErr(err)) return err;
                                QVector<Value> sums;
                                if (args.size() > 2) {
                                    const Value err2 = flatRangeValues(
                                        ctx, std::get<RangeView>(args.at(2)), &sums);
                                    if (isErr(err2)) return err2;
                                }
                                const QString criteria =
                                    toText(scalarOrValue(args.at(1)));
                                double sum = 0;
                                int count = 0;
                                for (int i = 0; i < values.size(); ++i) {
                                    if (!criteriaMatches(values.at(i), criteria)) {
                                        continue;
                                    }
                                    const Value &source =
                                        sums.isEmpty() ? values.at(i)
                                                       : sums.at(qMin(i, sums.size() - 1));
                                    const auto d = toNumber(source);
                                    if (d.has_value()) {
                                        sum = fold(sum, *d);
                                        ++count;
                                    }
                                }
                                if (count == 0) {
                                    return average ? Value(Err::Div0) : Value(0.0);
                                }
                                return average ? sum / double(count) : sum;
                            }};
    };
    r.insert(QStringLiteral("SUMIF"), ifAgg([](double a, double b) { return a + b; }, false));
    r.insert(QStringLiteral("AVERAGEIF"), ifAgg([](double a, double b) { return a + b; }, true));
    r.insert(QStringLiteral("COUNTIF"),
             FunctionSpec{2, 2, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                              QVector<Value> values;
                              const Value err =
                                  sparseRangeValues(ctx, std::get<RangeView>(args.at(0)), &values);
                              if (isErr(err)) return err;
                              const QString criteria = toText(scalarOrValue(args.at(1)));
                              double count = 0;
                              for (const Value &v : values) {
                                  if (criteriaMatches(v, criteria)) ++count;
                              }
                              return count;
                          }});
}

} // namespace QindaOffice::Calc
