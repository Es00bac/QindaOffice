// SPDX-License-Identifier: GPL-3.0-or-later
#include "functions.h"

#include "../eval_array.h"
#include "../number_format.h"

namespace QindaOffice::Calc {

void registerTextFunctions(QHash<QString, FunctionSpec> &r)
{
    r.insert(QStringLiteral("CONCATENATE"),
             FunctionSpec{0, 255, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              QString out;
                              for (const Arg &arg : args) {
                                  // CONCATENATE takes scalars only — a range
                                  // (or a multi-cell array) is #VALUE!.
                                  const Value v = scalarOrValue(arg);
                                  if (isErr(v)) return v;
                                  out += toText(v);
                              }
                              return out;
                          }});
    r.insert(QStringLiteral("CONCAT"),
             FunctionSpec{0, 255, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                              QString out;
                              for (const Arg &arg : args) {
                                  QVector<Value> values;
                                  const Value err = sparseArgValues(ctx, arg, &values);
                                  if (isErr(err)) return err;
                                  for (const Value &v : values) {
                                      if (!isBlank(v)) out += toText(v);
                                  }
                              }
                              return out;
                          }});
    const auto caseFn = [](std::function<QString(const QString &)> f) {
        return FunctionSpec{1, 1, false,
                            [f](EvalContext &, const QVector<Arg> &args) -> Value {
                                return f(toText(scalarOrValue(args.at(0))));
                            }};
    };
    r.insert(QStringLiteral("LOWER"), caseFn([](const QString &s) { return s.toLower(); }));
    r.insert(QStringLiteral("UPPER"), caseFn([](const QString &s) { return s.toUpper(); }));
    r.insert(QStringLiteral("TRIM"),
             caseFn([](const QString &s) {
                 // Strip leading/trailing space and collapse interior runs.
                 QString out = s.simplified();
                 return out;
             }));
    r.insert(QStringLiteral("CLEAN"),
             caseFn([](const QString &s) {
                 QString out;
                 for (const QChar c : s) {
                     if (c.category() != QChar::Other_Control) out += c;
                 }
                 return out;
             }));
    r.insert(QStringLiteral("PROPER"),
             caseFn([](const QString &s) {
                 QString out = s.toLower();
                 bool boundary = true;
                 for (int i = 0; i < out.size(); ++i) {
                     if (out[i].isLetter()) {
                         if (boundary) out[i] = out[i].toUpper();
                         boundary = false;
                     } else {
                         boundary = true;
                     }
                 }
                 return out;
             }));
    r.insert(QStringLiteral("LEN"),
             FunctionSpec{1, 1, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              return double(toText(scalarOrValue(args.at(0))).size());
                          }});
    const auto sideFn = [](bool left) {
        return FunctionSpec{1, 2, false,
                            [left](EvalContext &, const QVector<Arg> &args) -> Value {
                                const QString s = toText(scalarOrValue(args.at(0)));
                                int n = 1;
                                if (args.size() > 1) {
                                    const auto count = scalarNumber(args.at(1));
                                    if (!count) return Err::Value;
                                    n = int(*count);
                                }
                                if (n < 0) return Err::Value;
                                return left ? s.left(n) : s.right(n);
                            }};
    };
    r.insert(QStringLiteral("LEFT"), sideFn(true));
    r.insert(QStringLiteral("RIGHT"), sideFn(false));
    r.insert(QStringLiteral("MID"),
             FunctionSpec{3, 3, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const QString s = toText(scalarOrValue(args.at(0)));
                              const auto start = scalarNumber(args.at(1));
                              const auto len = scalarNumber(args.at(2));
                              if (!start || !len) return Err::Value;
                              if (*len < 0 || *start < 1) return Err::Value;
                              return s.mid(int(*start) - 1, int(*len));
                          }});
    r.insert(QStringLiteral("REPLACE"),
             FunctionSpec{4, 4, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const QString s = toText(scalarOrValue(args.at(0)));
                              const auto start = scalarNumber(args.at(1));
                              const auto len = scalarNumber(args.at(2));
                              const QString with = toText(scalarOrValue(args.at(3)));
                              if (!start || !len || *start < 1 || *len < 0) return Err::Value;
                              return s.left(int(*start) - 1) + with + s.mid(int(*start) - 1 + int(*len));
                          }});
    r.insert(QStringLiteral("SUBSTITUTE"),
             FunctionSpec{3, 4, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const QString s = toText(scalarOrValue(args.at(0)));
                              const QString from = toText(scalarOrValue(args.at(1)));
                              const QString to = toText(scalarOrValue(args.at(2)));
                              if (from.isEmpty()) return Err::Value;
                              if (args.size() == 3) {
                                  QString out = s;
                                  out.replace(from, to);
                                  return out;
                              }
                              const auto which = scalarNumber(args.at(3));
                              if (!which) return Err::Value;
                              // Replace only the nth occurrence.
                              int pos = -1;
                              for (int i = 0; i < int(*which); ++i) {
                                  pos = s.indexOf(from, pos + 1);
                                  if (pos < 0) break;
                              }
                              if (pos < 0) return s;
                              QString out = s;
                              out.replace(pos, from.size(), to);
                              return out;
                          }});
    r.insert(QStringLiteral("FIND"),
             FunctionSpec{2, 3, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const QString needle = toText(scalarOrValue(args.at(0)));
                              const QString hay = toText(scalarOrValue(args.at(1)));
                              int start = 0;
                              if (args.size() > 2) {
                                  const auto s = scalarNumber(args.at(2));
                                  if (!s || *s < 1) return Err::Value;
                                  start = int(*s) - 1;
                              }
                              const int pos = hay.indexOf(needle, start, Qt::CaseSensitive);
                              return pos < 0 ? Value(Err::Value) : Value(double(pos + 1));
                          }});
    r.insert(QStringLiteral("SEARCH"),
             FunctionSpec{2, 3, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const QString needle = toText(scalarOrValue(args.at(0)));
                              const QString hay = toText(scalarOrValue(args.at(1)));
                              int start = 0;
                              if (args.size() > 2) {
                                  const auto s = scalarNumber(args.at(2));
                                  if (!s || *s < 1) return Err::Value;
                                  start = int(*s) - 1;
                              }
                              const int pos = hay.indexOf(needle, start, Qt::CaseInsensitive);
                              return pos < 0 ? Value(Err::Value) : Value(double(pos + 1));
                          }});
    r.insert(QStringLiteral("REPT"),
             FunctionSpec{2, 2, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const QString s = toText(scalarOrValue(args.at(0)));
                              const auto n = scalarNumber(args.at(1));
                              if (!n || *n < 0) return Err::Value;
                              // Enforce Excel's 32767-char ceiling BEFORE
                              // building: REPT("a", 2e9) previously allocated
                              // multi-gigabyte strings and hung the sheet.
                              if (double(s.size()) * *n > 32767.0) {
                                  return Err::Value;
                              }
                              QString out;
                              for (int i = 0; i < int(*n); ++i) out += s;
                              return out;
                          }});
    r.insert(QStringLiteral("EXACT"),
             FunctionSpec{2, 2, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              return toText(scalarOrValue(args.at(0)))
                                  == toText(scalarOrValue(args.at(1)));
                          }});
    r.insert(QStringLiteral("CHAR"),
             FunctionSpec{1, 1, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto n = scalarNumber(args.at(0));
                              if (!n || *n < 1 || *n > 255) return Err::Value;
                              return QString(QChar(char(*n)));
                          }});
    r.insert(QStringLiteral("CODE"),
             FunctionSpec{1, 1, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const QString s = toText(scalarOrValue(args.at(0)));
                              return s.isEmpty() ? Value(Err::Value) : Value(double(s.at(0).unicode()));
                          }});
    r.insert(QStringLiteral("T"),
             FunctionSpec{1, 1, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const Value v = scalarOrValue(args.at(0));
                              return isText(v) ? v : Value();
                          }});
    r.insert(QStringLiteral("N"),
             FunctionSpec{1, 1, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const Value v = scalarOrValue(args.at(0));
                              if (isNumber(v)) return v;
                              if (isBool(v)) return std::get<bool>(v) ? 1.0 : 0.0;
                              return 0.0;
                          }});
    r.insert(QStringLiteral("VALUE"),
             FunctionSpec{1, 1, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const Value v = scalarOrValue(args.at(0));
                              if (const auto d = toNumber(v); d.has_value()) {
                                  return *d;
                              }
                              return Err::Value;
                          }});
    r.insert(QStringLiteral("NUMBERVALUE"),
             FunctionSpec{1, 3, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              QString s = toText(scalarOrValue(args.at(0)));
                              QString decimal = QStringLiteral(".");
                              QString group = QStringLiteral(",");
                              if (args.size() > 1 && !toText(scalarOrValue(args.at(1))).isEmpty()) {
                                  decimal = toText(scalarOrValue(args.at(1)));
                              }
                              if (args.size() > 2 && !toText(scalarOrValue(args.at(2))).isEmpty()) {
                                  group = toText(scalarOrValue(args.at(2)));
                              }
                              if (group != decimal) {
                                  s.remove(group);
                              }
                              if (decimal != QLatin1String(".")) {
                                  s.replace(decimal, QStringLiteral("."));
                              }
                              if (s.endsWith(QLatin1Char('%'))) {
                                  s.chop(1);
                                  bool ok = false;
                                  const double d = s.toDouble(&ok);
                                  return ok ? Value(d / 100.0) : Value(Err::Value);
                              }
                              bool ok = false;
                              const double d = s.toDouble(&ok);
                              return ok ? Value(d) : Value(Err::Value);
                          }});
    r.insert(QStringLiteral("TEXT"),
             FunctionSpec{2, 2, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const Value v = scalarOrValue(args.at(0));
                              const QString code = toText(scalarOrValue(args.at(1)));
                              return formatWithCode(v, code).text;
                          }});
}


void registerTextJoinFunctions(QHash<QString, FunctionSpec> &r)
{
    r.insert(QStringLiteral("TEXTJOIN"),
             FunctionSpec{3, 255, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                              const QString delimiter = toText(scalarOrValue(args.at(0)));
                              const auto ignoreEmpty = toBool(scalarOrValue(args.at(1)));
                              if (!ignoreEmpty) return Err::Value;
                              QStringList pieces;
                              for (int i = 2; i < args.size(); ++i) {
                                  QVector<Value> values;
                                  const Value err = sparseArgValues(ctx, args.at(i), &values);
                                  if (isErr(err)) return err;
                                  for (const Value &v : values) {
                                      const QString t = toText(v);
                                      if (*ignoreEmpty && t.isEmpty()) continue;
                                      pieces.append(t);
                                  }
                              }
                              return pieces.join(delimiter);
                          }});
    r.insert(QStringLiteral("UNICHAR"),
             FunctionSpec{1, 1, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto code = scalarNumber(args.at(0));
                              if (!code) return Err::Value;
                              if (*code < 1 || *code > 0x10FFFF) return Err::Value;
                              QString out;
                              out.append(QChar::fromUcs4(uint(*code)));
                              return out;
                          }});
    r.insert(QStringLiteral("UNICODE"),
             FunctionSpec{1, 1, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const QString text = toText(scalarOrValue(args.at(0)));
                              if (text.isEmpty()) return Err::Value;
                              return double(text.at(0).unicode());
                          }});
    r.insert(QStringLiteral("FIXED"),
             FunctionSpec{1, 3, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto number = scalarNumber(args.at(0));
                              if (!number) return Err::Value;
                              int decimals = 2;
                              if (args.size() > 1) {
                                  const auto d = scalarNumber(args.at(1));
                                  if (!d) return Err::Value;
                                  decimals = int(*d);
                              }
                              bool noCommas = false;
                              if (args.size() > 2) {
                                  const auto n = toBool(scalarOrValue(args.at(2)));
                                  if (!n) return Err::Value;
                                  noCommas = *n;
                              }
                              const double factor = std::pow(10.0, decimals);
                              const double rounded = std::copysign(
                                  std::floor(std::abs(*number) * factor + 0.5) / factor,
                                  *number);
                              QString out = QString::number(rounded, 'f', qMax(0, decimals));
                              if (!noCommas) {
                                  const int dot = out.indexOf(QLatin1Char('.'));
                                  const int start = dot < 0 ? out.size() - 1 : dot - 1;
                                  for (int i = start, group = 0; i > 0; --i) {
                                      if (out.at(i).isDigit()) {
                                          if (++group == 3) {
                                              out.insert(i, QLatin1Char(','));
                                              group = 0;
                                          }
                                      } else {
                                          break;
                                      }
                                  }
                              }
                              return out;
                          }});
}

} // namespace QindaOffice::Calc
