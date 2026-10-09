// SPDX-License-Identifier: GPL-3.0-or-later
#include "functions.h"

#include "../eval_array.h"
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
        QVector<Value> values;
        const Value err = sparseArgValues(ctx, arg, &values);
        if (isErr(err)) {
            return err;
        }
        for (const Value &v : values) {
            if (const auto d = toNumber(v); d.has_value() && !(skipTextAndBlank && isText(v))) {
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
                                  QVector<Value> values;
                                  const Value err = sparseArgValues(ctx, arg, &values);
                                  if (isErr(err)) return err;
                                  for (const Value &v : values) {
                                      if (!isBlank(v)) ++count;
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
    // MODE returns the most frequently occurring value; #N/A when no value
    // repeats (Excel rule). numberAgg flattens both ranges and scalars.
    r.insert(QStringLiteral("MODE"),
             FunctionSpec{1, 255, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                              return numberAgg(ctx, args,
                                               [](const QVector<double> &ns) -> Value {
                                                   if (ns.isEmpty()) return Err::NA;
                                                   QHash<double, int> freq;
                                                   for (double x : ns) ++freq[x];
                                                   int bestCount = 0;
                                                   double best = 0;
                                                   for (auto it = freq.constBegin();
                                                        it != freq.constEnd(); ++it) {
                                                       if (it.value() > bestCount) {
                                                           bestCount = it.value();
                                                           best = it.key();
                                                       }
                                                   }
                                                   if (bestCount < 2) return Err::NA;
                                                   return best;
                                               });
                          }});
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
                                const Value err = sparseArgValues(ctx, args.at(0), &values);
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
                                // Any shape (a single cell reaches here as
                                // its value), error cells kept: an error in
                                // the criteria range just does not match,
                                // and one in the sum range counts only when
                                // its row matches (M61, Excel's rule).
                                ArrayValue grid;
                                if (const Value err = arrayFromArg(ctx, args.at(0), &grid); isErr(err)) return err;
                                const QVector<Value> &values = grid.cells;
                                QVector<Value> sums;
                                if (args.size() > 2) {
                                    ArrayValue sumGrid;
                                    if (const Value err = arrayFromArg(ctx, args.at(2), &sumGrid); isErr(err)) return err;
                                    sums = sumGrid.cells;
                                }
                                const QString criteria =
                                    toText(scalarOrValue(args.at(1)));
                                double sum = 0;
                                int count = 0;
                                for (int i = 0; i < values.size(); ++i) {
                                    if (!criteriaMatches(values.at(i), criteria)) {
                                        continue;
                                    }
                                    // Without a sum_range the criteria
                                    // cell itself is summed; a shorter
                                    // sum_range extends with blanks (Excel
                                    // semantics) — the old qMin clamp
                                    // re-summed its last cell.
                                    const Value source = sums.isEmpty()
                                        ? values.at(i)
                                        : (i < sums.size() ? sums.at(i)
                                                           : Value(std::monostate{}));
                                    if (isErr(source)) return source;
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
    // COUNTIF reads a range sparsely (COUNTIF($A:$A, B2) down a column is
    // common): the non-blank cells are compared — errors included, they just
    // do not match — and the blanks count once, as a block, when the
    // criterion accepts a blank. A single cell or an array counts too (M61).
    r.insert(QStringLiteral("COUNTIF"),
             FunctionSpec{2, 2, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                              const QString criteria = toText(scalarOrValue(args.at(1)));
                              double count = 0;
                              if (const auto *view = std::get_if<RangeView>(&args.at(0))) {
                                  if (!view->isValid()) return Err::Ref;
                                  const auto cells = ctx.host->nonBlankCellsIn(view->sheet, view->range);
                                  for (const auto &[addr, v] : cells) {
                                      Q_UNUSED(addr);
                                      if (criteriaMatches(v, criteria)) ++count;
                                  }
                                  if (criteriaMatches(Value(), criteria)) {
                                      count += double(view->range.cellCount() - cells.size());
                                  }
                                  return count;
                              }
                              ArrayValue grid;
                              if (const Value err = arrayFromArg(ctx, args.at(0), &grid); isErr(err)) return err;
                              for (const Value &v : std::as_const(grid.cells)) {
                                  if (criteriaMatches(v, criteria)) ++count;
                              }
                              return count;
                          }});
    // M10: *A variants count text as 0 and logicals as 1/0 (Excel rule).
    const auto aggA = [](bool average, bool extrema, bool maxima) {
        return FunctionSpec{1, 255, false,
                            [average, extrema, maxima](EvalContext &ctx,
                                                       const QVector<Arg> &args) -> Value {
                                const auto *rangeView = std::get_if<RangeView>(&args.at(0));
                                if (!rangeView) return Err::Value;
                                QVector<Value> values;
                                const Value err = sparseRangeValues(ctx, *rangeView, &values);
                                if (isErr(err)) return err;
                                double result = 0;
                                bool any = false;
                                for (const Value &v : values) {
                                    double d = 0;
                                    if (isBool(v)) {
                                        d = std::get<bool>(v) ? 1.0 : 0.0;
                                    } else if (!isText(v)) {
                                        const auto n = toNumber(v);
                                        d = n.value_or(0.0);
                                    }
                                    if (!any) {
                                        result = d;
                                        any = true;
                                    } else if (average) {
                                        result += d;
                                    } else if ((maxima && d > result) || (!maxima && d < result)) {
                                        result = d;
                                    }
                                }
                                if (!any) return 0.0;
                                return average ? result / double(values.size()) : result;
                            }};
    };
    r.insert(QStringLiteral("AVERAGEA"), aggA(true, false, false));
    r.insert(QStringLiteral("MAXA"), aggA(false, true, true));
    r.insert(QStringLiteral("MINA"), aggA(false, true, false));
    const auto devA = [](bool variance) {
        return FunctionSpec{1, 255, false,
                            [variance](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                                const auto *rangeView = std::get_if<RangeView>(&args.at(0));
                                if (!rangeView) return Err::Value;
                                QVector<Value> values;
                                const Value err = sparseRangeValues(ctx, *rangeView, &values);
                                if (isErr(err)) return err;
                                if (values.size() < 2) return Err::Div0;
                                QVector<double> nums;
                                for (const Value &v : values) {
                                    if (isBool(v)) {
                                        nums.append(std::get<bool>(v) ? 1.0 : 0.0);
                                    } else if (!isText(v)) {
                                        const auto n = toNumber(v);
                                        nums.append(n.value_or(0.0));
                                    } else {
                                        nums.append(0.0);
                                    }
                                }
                                const double mean =
                                    std::accumulate(nums.begin(), nums.end(), 0.0)
                                    / double(nums.size());
                                double acc = 0;
                                for (double x : nums) acc += (x - mean) * (x - mean);
                                return variance ? acc / double(nums.size() - 1)
                                                : std::sqrt(acc / double(nums.size() - 1));
                            }};
    };
    r.insert(QStringLiteral("STDEVA"), devA(false));
    r.insert(QStringLiteral("VARA"), devA(true));
    // Percentile-style: sorted rank with linear interpolation (INC rule).
    const auto percentile = [](const QVector<double> &sorted, double k) -> Value {
        if (sorted.isEmpty() || k < 0.0 || k > 1.0) return Err::Num;
        const double rank = k * double(sorted.size() - 1);
        const int lower = int(std::floor(rank));
        const int upper = qMin(lower + 1, sorted.size() - 1);
        return sorted.at(lower)
            + (sorted.at(upper) - sorted.at(lower)) * (rank - lower);
    };
    r.insert(QStringLiteral("PERCENTILE"),
             FunctionSpec{2, 2, false, [percentile](EvalContext &ctx,
                                                    const QVector<Arg> &args) -> Value {
                              // A range or (M69) an array: AGGREGATE(16, …) and
                              // PERCENTILE(IF(…)) hand over arrays.
                              QVector<Value> values;
                              const Value err = sparseArgValues(ctx, args.at(0), &values);
                              if (isErr(err)) return err;
                              const auto k = scalarNumber(args.at(1));
                              if (!k) return Err::Value;
                              QVector<double> nums;
                              for (const Value &v : values) {
                                  if (const auto d = toNumber(v); d.has_value() && !isText(v)) {
                                      nums.append(*d);
                                  }
                              }
                              std::sort(nums.begin(), nums.end());
                              return percentile(nums, *k);
                          }});
    r.insert(QStringLiteral("QUARTILE"),
             FunctionSpec{2, 2, false, [percentile](EvalContext &ctx,
                                                    const QVector<Arg> &args) -> Value {
                              // A range or (M69) an array: AGGREGATE(16, …) and
                              // PERCENTILE(IF(…)) hand over arrays.
                              QVector<Value> values;
                              const Value err = sparseArgValues(ctx, args.at(0), &values);
                              if (isErr(err)) return err;
                              const auto q = scalarNumber(args.at(1));
                              if (!q) return Err::Value;
                              if (*q < 0 || *q > 4) return Err::Num;
                              QVector<double> nums;
                              for (const Value &v : values) {
                                  if (const auto d = toNumber(v); d.has_value() && !isText(v)) {
                                      nums.append(*d);
                                  }
                              }
                              std::sort(nums.begin(), nums.end());
                              return percentile(nums, *q / 4.0);
                          }});
    r.insert(QStringLiteral("RANK"),
             FunctionSpec{2, 3, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                              const auto *rangeView = std::get_if<RangeView>(&args.at(1));
                              if (!rangeView) return Err::Value;
                              QVector<Value> values;
                              const Value err = sparseRangeValues(ctx, *rangeView, &values);
                              if (isErr(err)) return err;
                              const auto x = scalarNumber(args.at(0));
                              if (!x) return Err::Value;
                              double order = 0;
                              if (args.size() > 2) {
                                  const auto o = scalarNumber(args.at(2));
                                  if (!o) return Err::Value;
                                  order = *o;
                              }
                              QVector<double> nums;
                              for (const Value &v : values) {
                                  if (const auto d = toNumber(v); d.has_value() && !isText(v)) {
                                      nums.append(*d);
                                  }
                              }
                              int rank = 1;
                              for (double n : nums) {
                                  if (order == 0 ? n > *x : n < *x) ++rank;
                              }
                              bool found = false;
                              for (double n : nums) {
                                  if (n == *x) found = true;
                              }
                              return found ? double(rank) : Value(Err::NA);
                          }});
    r.insert(QStringLiteral("GEOMEAN"),
             FunctionSpec{1, 255, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                              return numberAgg(ctx, args, [](const QVector<double> &ns) -> Value {
                                  if (ns.isEmpty()) return Err::Num;
                                  double logSum = 0;
                                  for (double x : ns) {
                                      if (x <= 0) return Value(Err::Num);
                                      logSum += std::log(x);
                                  }
                                  return std::exp(logSum / double(ns.size()));
                              });
                          }});
    r.insert(QStringLiteral("HARMEAN"),
             FunctionSpec{1, 255, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                              return numberAgg(ctx, args, [](const QVector<double> &ns) -> Value {
                                  if (ns.isEmpty()) return Err::Num;
                                  double acc = 0;
                                  for (double x : ns) {
                                      if (x <= 0) return Value(Err::Num);
                                      acc += 1.0 / x;
                                  }
                                  return double(ns.size()) / acc;
                              });
                          }});
    r.insert(QStringLiteral("AVEDEV"),
             FunctionSpec{1, 255, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                              return numberAgg(ctx, args, [](const QVector<double> &ns) -> Value {
                                  if (ns.isEmpty()) return Err::Div0;
                                  const double mean =
                                      std::accumulate(ns.begin(), ns.end(), 0.0)
                                      / double(ns.size());
                                  double acc = 0;
                                  for (double x : ns) acc += std::abs(x - mean);
                                  return acc / double(ns.size());
                              });
                          }});
    r.insert(QStringLiteral("TRIMMEAN"),
             FunctionSpec{2, 2, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                              const auto *rangeView = std::get_if<RangeView>(&args.at(0));
                              if (!rangeView) return Err::Value;
                              QVector<Value> values;
                              const Value err = sparseRangeValues(ctx, *rangeView, &values);
                              if (isErr(err)) return err;
                              const auto p = scalarNumber(args.at(1));
                              if (!p) return Err::Value;
                              if (*p < 0 || *p >= 1) return Err::Num;
                              QVector<double> nums;
                              for (const Value &v : values) {
                                  if (const auto d = toNumber(v); d.has_value() && !isText(v)) {
                                      nums.append(*d);
                                  }
                              }
                              std::sort(nums.begin(), nums.end());
                              const int trim = int(std::floor(*p / 2.0 * nums.size()));
                              if (nums.size() - 2 * trim <= 0) return Err::Num;
                              double acc = 0;
                              for (int i = trim; i < nums.size() - trim; ++i) acc += nums.at(i);
                              return acc / double(nums.size() - 2 * trim);
                          }});
}

} // namespace QindaOffice::Calc
