// SPDX-License-Identifier: GPL-3.0-or-later
#include "functions.h"

#include <cmath>

namespace QindaOffice::Calc {

namespace {

Value unaryReal(std::function<double(double)> fn, const QVector<Arg> &args,
                std::function<bool(double)> domain = {})
{
    const auto x = scalarNumber(args.at(0));
    if (!x) return Err::Value;
    if (domain && !domain(*x)) return Err::Num;
    const double result = fn(*x);
    if (!std::isfinite(result)) return Err::Num;
    return result;
}

} // namespace

// Trigonometric and numeric-conversion extras (plan M10): hyperbolic
// functions, reciprocals, and the BASE/DECIMAL/ARABIC converters. Kept in
// their own file so functions.cpp stays under the line budget.
void registerMathExtraFunctions(QHash<QString, FunctionSpec> &r)
{
    r.insert(QStringLiteral("SINH"),
             FunctionSpec{1, 1, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              return unaryReal([](double x) { return std::sinh(x); }, args);
                          }});
    r.insert(QStringLiteral("COSH"),
             FunctionSpec{1, 1, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              return unaryReal([](double x) { return std::cosh(x); }, args);
                          }});
    r.insert(QStringLiteral("TANH"),
             FunctionSpec{1, 1, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              return unaryReal([](double x) { return std::tanh(x); }, args);
                          }});
    r.insert(QStringLiteral("ASINH"),
             FunctionSpec{1, 1, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              return unaryReal([](double x) { return std::asinh(x); }, args);
                          }});
    r.insert(QStringLiteral("ACOSH"),
             FunctionSpec{1, 1, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              return unaryReal([](double x) { return std::acosh(x); }, args,
                                               [](double x) { return x >= 1.0; });
                          }});
    r.insert(QStringLiteral("ATANH"),
             FunctionSpec{1, 1, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              return unaryReal([](double x) { return std::atanh(x); }, args,
                                               [](double x) { return x > -1.0 && x < 1.0; });
                          }});
    const auto reciprocal = [](std::function<double(double)> fn) {
        return FunctionSpec{1, 1, false,
                            [fn](EvalContext &, const QVector<Arg> &args) -> Value {
                                const auto x = scalarNumber(args.at(0));
                                if (!x) return Err::Value;
                                const double denom = fn(*x);
                                if (denom == 0.0) return Err::Div0;
                                return 1.0 / denom;
                            }};
    };
    r.insert(QStringLiteral("COT"), reciprocal([](double x) { return std::tan(x); }));
    r.insert(QStringLiteral("CSC"), reciprocal([](double x) { return std::sin(x); }));
    r.insert(QStringLiteral("SEC"), reciprocal([](double x) { return std::cos(x); }));
    r.insert(QStringLiteral("COTH"), reciprocal([](double x) { return std::tanh(x); }));
    r.insert(QStringLiteral("CSCH"), reciprocal([](double x) { return std::sinh(x); }));
    r.insert(QStringLiteral("SECH"), reciprocal([](double x) { return std::cosh(x); }));
    r.insert(QStringLiteral("SQRTPI"),
             FunctionSpec{1, 1, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              return unaryReal([](double x) { return std::sqrt(x * M_PI); }, args,
                                               [](double x) { return x >= 0.0; });
                          }});
    r.insert(QStringLiteral("BASE"),
             FunctionSpec{2, 3, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto number = scalarNumber(args.at(0));
                              const auto radix = scalarNumber(args.at(1));
                              if (!number || !radix) return Err::Value;
                              const qint64 value = qint64(std::floor(*number));
                              const int base = int(*radix);
                              if (value < 0 || base < 2 || base > 36) return Err::Num;
                              QString digits = QString::number(value, base).toUpper();
                              if (args.size() > 2) {
                                  const auto minLength = scalarNumber(args.at(2));
                                  if (!minLength) return Err::Value;
                                  while (digits.size() < int(*minLength)) {
                                      digits.prepend(QLatin1Char('0'));
                                  }
                              }
                              return digits;
                          }});
    r.insert(QStringLiteral("DECIMAL"),
             FunctionSpec{2, 2, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const QString text = toText(scalarOrValue(args.at(0))).trimmed();
                              const auto radix = scalarNumber(args.at(1));
                              if (!radix) return Err::Value;
                              const int base = int(*radix);
                              if (base < 2 || base > 36) return Err::Num;
                              bool ok = false;
                              const qlonglong value = text.toLongLong(&ok, base);
                              if (!ok) return Err::Num;
                              return double(value);
                          }});
    r.insert(QStringLiteral("ARABIC"),
             FunctionSpec{1, 1, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const QString roman =
                                  toText(scalarOrValue(args.at(0))).trimmed().toUpper();
                              if (roman.isEmpty()) return 0.0;
                              const QHash<QChar, int> values = {
                                  {QLatin1Char('I'), 1},   {QLatin1Char('V'), 5},
                                  {QLatin1Char('X'), 10},  {QLatin1Char('L'), 50},
                                  {QLatin1Char('C'), 100}, {QLatin1Char('D'), 500},
                                  {QLatin1Char('M'), 1000}};
                              int total = 0;
                              int prev = 0;
                              for (auto it = roman.crbegin(); it != roman.crend(); ++it) {
                                  if (!values.contains(*it)) return Err::Value;
                                  const int v = values.value(*it);
                                  total += v < prev ? -v : v;
                                  prev = qMax(prev, v);
                              }
                              return double(total);
                          }});
}

} // namespace QindaOffice::Calc
