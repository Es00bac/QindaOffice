// SPDX-License-Identifier: GPL-3.0-or-later
#include "functions.h"

namespace QindaOffice::Calc {

void registerConditionalAggFunctions(QHash<QString, FunctionSpec> &r)
{
    // One value range plus (criteria range, criterion) pairs.
    const auto ifsAgg = [](bool average, bool countOnly, bool maxima) {
        return FunctionSpec{3, 255, false,
                            [average, countOnly, maxima](EvalContext &ctx,
                                                         const QVector<Arg> &args) -> Value {
                                const auto *valueRangeView = std::get_if<RangeView>(&args.at(0));
                                if (!valueRangeView) return Err::Value;
                                QVector<Value> values;
                                Value err = flatRangeValues(ctx, *valueRangeView, &values);
                                if (isErr(err)) return err;
                                struct Pair {
                                    QVector<Value> cells;
                                    QString criteria;
                                };
                                QVector<Pair> pairs;
                                for (int i = 1; i + 1 < args.size(); i += 2) {
                                    const auto *critRangeView = std::get_if<RangeView>(&args.at(i));
                                    if (!critRangeView) return Err::Value;
                                    QVector<Value> cells;
                                    err = flatRangeValues(ctx, *critRangeView, &cells);
                                    if (isErr(err)) return err;
                                    pairs.append({cells,
                                                  toText(scalarOrValue(args.at(i + 1)))});
                                }
                                double result = 0;
                                int count = 0;
                                for (int i = 0; i < values.size(); ++i) {
                                    bool match = true;
                                    for (const Pair &pair : pairs) {
                                        if (i >= pair.cells.size()
                                            || !criteriaMatches(pair.cells.at(i),
                                                                pair.criteria)) {
                                            match = false;
                                            break;
                                        }
                                    }
                                    if (!match) continue;
                                    if (countOnly) {
                                        ++count;
                                        continue;
                                    }
                                    // Blank (and text) cells in the value
                                    // range contribute nothing — counting a
                                    // blank as 0 skewed MINIFS/AVERAGEIFS.
                                    if (isBlank(values.at(i))) continue;
                                    const auto d = toNumber(values.at(i));
                                    if (!d.has_value()) continue;
                                    if (count == 0 || (maxima && *d > result)
                                        || (!maxima && !average && *d < result)) {
                                        result = *d;
                                    } else if (average) {
                                        result += *d;
                                    }
                                    ++count;
                                }
                                if (countOnly) return double(count);
                                if (count == 0) return average ? Value(Err::Div0) : Value(0.0);
                                return average ? result / double(count) : result;
                            }};
    };
    // COUNTIFS counts over its own criteria ranges (range, crit) pairs,
    // starting at index 0 - there is no separate value range.
    r.insert(QStringLiteral("COUNTIFS"),
             FunctionSpec{2, 255, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                              struct Pair {
                                  QVector<Value> cells;
                                  QString criteria;
                              };
                              QVector<Pair> pairs;
                              for (int i = 0; i + 1 < args.size(); i += 2) {
                                  if (!std::holds_alternative<RangeView>(args.at(i))) {
                                      return Err::Value;
                                  }
                                  QVector<Value> cells;
                                  const Value err = flatRangeValues(
                                      ctx, std::get<RangeView>(args.at(i)), &cells);
                                  if (isErr(err)) return err;
                                  pairs.append({cells,
                                                toText(scalarOrValue(args.at(i + 1)))});
                              }
                              int span = 0;
                              for (const Pair &pair : pairs) {
                                  span = qMax(span, pair.cells.size());
                              }
                              double count = 0;
                              for (int i = 0; i < span; ++i) {
                                  bool match = true;
                                  for (const Pair &pair : pairs) {
                                      if (i >= pair.cells.size()
                                          || !criteriaMatches(pair.cells.at(i), pair.criteria)) {
                                          match = false;
                                          break;
                                      }
                                  }
                                  if (match) ++count;
                              }
                              return count;
                          }});
    r.insert(QStringLiteral("AVERAGEIFS"), ifsAgg(true, false, false));
    r.insert(QStringLiteral("MAXIFS"), ifsAgg(false, false, true));
    r.insert(QStringLiteral("MINIFS"), ifsAgg(false, false, false));
}

} // namespace QindaOffice::Calc
