// SPDX-License-Identifier: GPL-3.0-or-later
#include "functions.h"

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
// ranges (1xx variants ignore hidden rows, which the engine does not
// model: they aggregate the same cells).
r.insert(QStringLiteral("SUBTOTAL"),
         FunctionSpec{2, 255, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Arg {
                          const Value codeValue = std::holds_alternative<Value>(args.at(0))
                              ? std::get<Value>(args.at(0))
                              : Value(Err::Value);
                          const auto code = toNumber(codeValue);
                          if (!code.has_value()) return Err::Value;
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
                          const QVector<Arg> rest = args.mid(1);
                          if (rest.size() < spec->minArgs || rest.size() > spec->maxArgs) {
                              return Err::Value;
                          }
                          return spec->fn(ctx, rest);
                      }});
}

} // namespace QindaOffice::Calc
