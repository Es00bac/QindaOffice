// SPDX-License-Identifier: GPL-3.0-or-later
#include "functions.h"

namespace QindaOffice::Calc {

void registerLogicalFunctions(QHash<QString, FunctionSpec> &r)
{
    r.insert(QStringLiteral("TRUE"), FunctionSpec{0, 0, false, [](EvalContext &, const QVector<Arg> &) {
                  return true;
              }});
    r.insert(QStringLiteral("FALSE"), FunctionSpec{0, 0, false, [](EvalContext &, const QVector<Arg> &) {
                  return false;
              }});
    r.insert(QStringLiteral("IF"),
             FunctionSpec{2, 3, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                              const Value cond = scalarOrValue(args.at(0));
                              if (isErr(cond)) return cond;
                              const auto truth = toBool(cond);
                              if (!truth.has_value()) return Err::Value;
                              if (*truth) {
                                  return scalarOrValue(args.at(1));
                              }
                              if (args.size() > 2) {
                                  return scalarOrValue(args.at(2));
                              }
                                  Q_UNUSED(ctx);
                                  return false;
                          }});
    r.insert(QStringLiteral("IFS"),
             FunctionSpec{2, 254, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              for (int i = 0; i + 1 < args.size(); i += 2) {
                                  const auto truth = toBool(scalarOrValue(args.at(i)));
                                  if (truth.has_value() && *truth) {
                                      return scalarOrValue(args.at(i + 1));
                                  }
                              }
                              return Err::NA;
                          }});
    r.insert(QStringLiteral("IFERROR"),
             FunctionSpec{2, 2, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const Value v = scalarOrValue(args.at(0));
                              return isErr(v) ? scalarOrValue(args.at(1)) : v;
                          }});
    r.insert(QStringLiteral("IFNA"),
             FunctionSpec{2, 2, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const Value v = scalarOrValue(args.at(0));
                              return std::holds_alternative<Err>(v) && std::get<Err>(v) == Err::NA
                                  ? scalarOrValue(args.at(1))
                                  : v;
                          }});
    const auto boolFold = [](std::function<bool(bool, bool)> fold, bool emptyResult) {
        return FunctionSpec{1, 255, false,
                            [fold, emptyResult](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                                bool acc = emptyResult;
                                bool any = false;
                                for (const Arg &arg : args) {
                                    if (std::holds_alternative<RangeView>(arg)) {
                                        QVector<Value> values;
                                        const Value err =
                                            sparseRangeValues(ctx, std::get<RangeView>(arg), &values);
                                        if (isErr(err)) return err;
                                        for (const Value &v : values) {
                                            if (isBlank(v)) continue;
                                            const auto b = toBool(v);
                                            if (!b.has_value()) return Err::Value;
                                            acc = fold(acc, *b);
                                            any = true;
                                        }
                                    } else {
                                        const Value v = std::get<Value>(arg);
                                        if (isBlank(v)) continue;
                                        const auto b = toBool(v);
                                        if (!b.has_value()) return Err::Value;
                                        acc = fold(acc, *b);
                                        any = true;
                                    }
                                }
                                Q_UNUSED(any);
                                return acc;
                            }};
    };
    r.insert(QStringLiteral("AND"), boolFold([](bool a, bool b) { return a && b; }, true));
    r.insert(QStringLiteral("OR"), boolFold([](bool a, bool b) { return a || b; }, false));
    r.insert(QStringLiteral("XOR"), boolFold([](bool a, bool b) { return a != b; }, false));
    r.insert(QStringLiteral("NOT"),
             FunctionSpec{1, 1, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto b = toBool(scalarOrValue(args.at(0)));
                              if (!b.has_value()) return Err::Value;
                              return !*b;
                          }});
    r.insert(QStringLiteral("SWITCH"),
             FunctionSpec{3, 254, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const Value target = scalarOrValue(args.at(0));
                              if (isErr(target)) return target;
                              for (int i = 1; i + 1 < args.size(); i += 2) {
                                  if (compareValues(target, scalarOrValue(args.at(i))) == 0) {
                                      return scalarOrValue(args.at(i + 1));
                                  }
                              }
                              // Optional final default when an even arg count remains.
                              if ((args.size() - 1) % 2 == 1) {
                                  return scalarOrValue(args.last());
                              }
                              return Err::NA;
                          }});
}

void registerInfoFunctions(QHash<QString, FunctionSpec> &r)
{
    const auto isFn = [](std::function<bool(const Value &)> test) {
        return FunctionSpec{1, 1, false,
                            [test](EvalContext &, const QVector<Arg> &args) -> Value {
                                return test(scalarOrValue(args.at(0)));
                            }};
    };
    r.insert(QStringLiteral("ISBLANK"), isFn([](const Value &v) { return isBlank(v); }));
    r.insert(QStringLiteral("ISNUMBER"), isFn([](const Value &v) { return isNumber(v); }));
    r.insert(QStringLiteral("ISTEXT"), isFn([](const Value &v) { return isText(v); }));
    r.insert(QStringLiteral("ISNONTEXT"), isFn([](const Value &v) { return !isText(v); }));
    r.insert(QStringLiteral("ISLOGICAL"), isFn([](const Value &v) { return isBool(v); }));
    r.insert(QStringLiteral("ISERROR"),
             isFn([](const Value &v) { return isErr(v) && std::get<Err>(v) != Err::NA; }));
    r.insert(QStringLiteral("ISERR"),
             isFn([](const Value &v) { return isErr(v) && std::get<Err>(v) != Err::NA; }));
    r.insert(QStringLiteral("ISNA"),
             isFn([](const Value &v) { return std::holds_alternative<Err>(v) && std::get<Err>(v) == Err::NA; }));
    r.insert(QStringLiteral("ISEVEN"),
             isFn([](const Value &v) {
                 const auto d = toNumber(v);
                 return d.has_value() && qint64(std::abs(*d)) % 2 == 0;
             }));
    r.insert(QStringLiteral("ISODD"),
             isFn([](const Value &v) {
                 const auto d = toNumber(v);
                 return d.has_value() && qint64(std::abs(*d)) % 2 == 1;
             }));
    r.insert(QStringLiteral("NA"), FunctionSpec{0, 0, false, [](EvalContext &, const QVector<Arg> &) {
                  return Err::NA;
              }});
    r.insert(QStringLiteral("TYPE"),
             FunctionSpec{1, 1, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const Value v = scalarOrValue(args.at(0));
                              if (isNumber(v)) return 1.0;
                              if (isText(v)) return 2.0;
                              if (isBool(v)) return 4.0;
                              if (std::holds_alternative<Err>(v) && std::get<Err>(v) == Err::NA) {
                                  return 16.0;
                              }
                              return 64.0;
                          }});
    r.insert(QStringLiteral("ERROR.TYPE"),
             FunctionSpec{1, 1, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const Value v = scalarOrValue(args.at(0));
                              if (!isErr(v)) return Err::NA;
                              switch (std::get<Err>(v)) {
                              case Err::Null: return 1.0;
                              case Err::Div0: return 2.0;
                              case Err::Value: return 3.0;
                              case Err::Ref: return 4.0;
                              case Err::Name: return 5.0;
                              case Err::Num: return 6.0;
                              case Err::NA: return 7.0;
                              case Err::Circular: return 4.0;
                              }
                              return Err::NA;
                          }});
}

} // namespace QindaOffice::Calc
