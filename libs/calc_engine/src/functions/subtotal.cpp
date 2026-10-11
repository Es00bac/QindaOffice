// SPDX-License-Identifier: GPL-3.0-or-later
#include "functions.h"
#include "aggregate_refs.h"

namespace QindaOffice::Calc {

// The functions the newer Excel spelling reaches (RANK.EQ, FORECAST.LINEAR)
// and SUBTOTAL, which dispatches to the aggregate its code names. Split
// from functions.cpp for the line budget; registered last because the
// aliases copy specs the other files registered.
void registerSubtotalFunctions(QHash<QString, FunctionSpec> &r)
{
// Excel's renamed twins: same semantics, newer names.
r.insert(QStringLiteral("RANK.EQ"), r.value(QStringLiteral("RANK")));
r.insert(QStringLiteral("FORECAST.LINEAR"), r.value(QStringLiteral("FORECAST")));
// SUBTOTAL(code, range…): the aggregate the code names, over the
// ranges, omitting filtered rows and nested totals; 101–111 omit hidden rows.
r.insert(QStringLiteral("SUBTOTAL"),
         FunctionSpec{2, 255, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Arg {
                          const Value codeValue = scalarOrValue(args.at(0));
                          const auto code = toNumber(codeValue);
                          if (!code.has_value() || *code < 1 || *code > 111 || (*code > 11 && *code < 101)) return Err::Value;
                          static const QHash<int, QString> names{
                              {1, QStringLiteral("AVERAGE")}, {2, QStringLiteral("COUNT")},
                              {3, QStringLiteral("COUNTA")},  {4, QStringLiteral("MAX")},
                              {5, QStringLiteral("MIN")},     {6, QStringLiteral("PRODUCT")},
                              {7, QStringLiteral("STDEV")},   {8, QStringLiteral("STDEVP")},
                              {9, QStringLiteral("SUM")},     {10, QStringLiteral("VAR")},
                              {11, QStringLiteral("VARP")}};
                          const int which = int(*code) % 100;
                          const auto name = names.constFind(which);
                          if (name == names.constEnd()) return Err::Value;
                          const auto spec = functionRegistry().constFind(name.value());
                          if (spec == functionRegistry().constEnd()) return Err::Name;
                          QVector<Arg> rest;
                          for (qsizetype i = 1; i < args.size(); ++i) {
                              if (const auto *view = std::get_if<RangeView>(&args.at(i))) {
                                  ArrayValue kept{1, 0, {}};
                                  // Sparse iteration keeps whole-column totals bounded.
                                  for (const auto &[cell, value] : ctx.host->nonBlankCellsIn(view->sheet, view->range)) {
                                      if (!ctx.host->rowExcluded(view->sheet, cell.row, *code >= 101)
                                          && !nestedSubtotal(*ctx.host, view->sheet, cell)) kept.cells.append(value);
                                  }
                                  kept.cols = qint32(kept.cells.size());
                                  rest.append(kept.cells.isEmpty() ? Arg(Value()) : Arg(kept));
                              } else rest.append(args.at(i));
                          }
                          if (rest.size() < spec->minArgs || rest.size() > spec->maxArgs) {
                              return Err::Value;
                          }
                          return spec->fn(ctx, rest);
                      }, true});
}

} // namespace QindaOffice::Calc
