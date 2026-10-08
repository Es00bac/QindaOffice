// SPDX-License-Identifier: GPL-3.0-or-later
#include "functions.h"

#include "../eval_array.h"

namespace QindaOffice::Calc {

void registerConditionalAggFunctions(QHash<QString, FunctionSpec> &r)
{
    // One value range plus (criteria range, criterion) pairs.
    enum class Mode { Sum, Average, Min, Max };
    const auto ifsAgg = [](Mode mode) {
        const bool average = mode == Mode::Average;
        const bool sum = mode == Mode::Sum;
        const bool maxima = mode == Mode::Max;
        constexpr bool countOnly = false;
        return FunctionSpec{3, 255, false,
                            [average, sum, maxima, countOnly](EvalContext &ctx,
                                                              const QVector<Arg> &args) -> Value {
                                // Any shape, error cells kept (M61): an
                                // error only matters in a value cell whose
                                // row matches every criterion.
                                ArrayValue grid;
                                if (const Value err = arrayFromArg(ctx, args.at(0), &grid); isErr(err)) return err;
                                const QVector<Value> values = grid.cells;
                                struct Pair {
                                    QVector<Value> cells;
                                    QString criteria;
                                };
                                QVector<Pair> pairs;
                                for (int i = 1; i + 1 < args.size(); i += 2) {
                                    ArrayValue crit;
                                    if (const Value err = arrayFromArg(ctx, args.at(i), &crit); isErr(err)) return err;
                                    pairs.append({crit.cells, toText(scalarOrValue(args.at(i + 1)))});
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
                                    if (isErr(values.at(i))) return values.at(i);
                                    // Blank (and text) cells in the value
                                    // range contribute nothing — counting a
                                    // blank as 0 skewed MINIFS/AVERAGEIFS.
                                    if (isBlank(values.at(i))) continue;
                                    const auto d = toNumber(values.at(i));
                                    if (!d.has_value()) continue;
                                    if (average || sum) {
                                        result += *d;
                                    } else if (count == 0 || (maxima && *d > result)
                                               || (!maxima && *d < result)) {
                                        result = *d;
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
                                  ArrayValue crit; // any shape, error cells kept (M61)
                                  if (const Value err = arrayFromArg(ctx, args.at(i), &crit); isErr(err)) return err;
                                  pairs.append({crit.cells, toText(scalarOrValue(args.at(i + 1)))});
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
    r.insert(QStringLiteral("SUMIFS"), ifsAgg(Mode::Sum));
    r.insert(QStringLiteral("AVERAGEIFS"), ifsAgg(Mode::Average));
    r.insert(QStringLiteral("MAXIFS"), ifsAgg(Mode::Max));
    r.insert(QStringLiteral("MINIFS"), ifsAgg(Mode::Min));
}

} // namespace QindaOffice::Calc
