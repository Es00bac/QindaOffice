// SPDX-License-Identifier: GPL-3.0-or-later
#include "functions.h"

#include <QDateTime>
#include <QRegularExpression>

#include <QRandomGenerator>

#include <numeric>
#include <random>

namespace QindaOffice::Calc {

const QHash<QString, FunctionSpec> &functionRegistry()
{
    static const QHash<QString, FunctionSpec> registry = [] {
        QHash<QString, FunctionSpec> r;
        registerMathFunctions(r);
        registerStatisticalFunctions(r);
        registerTextFunctions(r);
        registerLogicalFunctions(r);
        registerLookupFunctions(r);
        registerDateFunctions(r);
        registerInfoFunctions(r);
        registerFinancialFunctions(r);
        return r;
    }();
    return registry;
}

Value scalarOrValue(const Arg &arg)
{
    if (std::holds_alternative<RangeView>(arg)) {
        return Err::Value;
    }
    return std::get<Value>(arg);
}

std::optional<double> scalarNumber(const Arg &arg)
{
    return toNumber(scalarOrValue(arg));
}

Value flatRangeValues(const EvalContext &ctx, const RangeView &view, QVector<Value> *out)
{
    out->clear();
    if (!ctx.host || !view.isValid()) {
        return Err::Ref;
    }
    const qint64 count = view.range.cellCount();
    if (count > (1 << 22)) {
        return Err::Value; // bounded guard against accidental whole-sheet scans
    }
    out->reserve(int(count));
    for (qint32 r = view.range.topLeft.row; r <= view.range.bottomRight.row; ++r) {
        for (qint32 c = view.range.topLeft.col; c <= view.range.bottomRight.col; ++c) {
            const Value v = ctx.host->cellValue(view.sheet, r, c);
            if (isErr(v)) {
                return v; // errors propagate out of range aggregation
            }
            out->append(v);
        }
    }
    return Value();
}

Value sparseRangeValues(const EvalContext &ctx, const RangeView &view, QVector<Value> *out)
{
    out->clear();
    if (!ctx.host || !view.isValid()) {
        return Err::Ref;
    }
    const auto cells = ctx.host->nonBlankCellsIn(view.sheet, view.range);
    out->reserve(cells.size());
    for (const auto &[addr, value] : cells) {
        Q_UNUSED(addr);
        if (isErr(value)) {
            return value;
        }
        out->append(value);
    }
    return Value();
}

bool criteriaMatches(const Value &cell, const QString &rawCriteria)
{
    const QString criteria = rawCriteria.trimmed();
    if (criteria.isEmpty()) {
        return isBlank(cell);
    }
    struct Op {
        QString prefix;
        std::function<bool(int cmp)> check;
    };
    static const Op kOps[] = {
        {QStringLiteral(">="), [](int c) { return c >= 0; }},
        {QStringLiteral("<="), [](int c) { return c <= 0; }},
        {QStringLiteral("<>"), [](int c) { return c != 0; }},
        {QStringLiteral(">"), [](int c) { return c > 0; }},
        {QStringLiteral("<"), [](int c) { return c < 0; }},
        {QStringLiteral("="), [](int c) { return c == 0; }},
    };
    for (const Op &op : kOps) {
        if (criteria.startsWith(op.prefix)) {
            const QString rest = criteria.mid(op.prefix.size());
            bool numOk = false;
            const double num = rest.toDouble(&numOk);
            if (numOk) {
                const std::optional<double> cellNum = toNumber(cell);
                if (!cellNum.has_value()) {
                    return false;
                }
                const double diff = *cellNum - num;
                return op.check(diff == 0 ? 0 : (diff < 0 ? -1 : 1));
            }
            const int cmp = QString::compare(toText(cell), rest, Qt::CaseInsensitive);
            return op.check(cmp == 0 ? 0 : (cmp < 0 ? -1 : 1));
        }
    }
    // Wildcard / equality match: COUNTIF/SUMIF equality is
    // case-insensitive (matches Excel and the LibreOffice oracle).
    const Value criterionValue = [&criteria]() -> Value {
        bool numOk = false;
        const double num = criteria.toDouble(&numOk);
        if (numOk) {
            return num;
        }
        return criteria;
    }();
    if (criteria.contains(QLatin1Char('*')) || criteria.contains(QLatin1Char('?'))) {
        QString pattern = criteria;
        pattern.replace(QLatin1Char('*'), QStringLiteral(".*"));
        pattern.replace(QLatin1Char('?'), QStringLiteral("."));
        const QRegularExpression re(QStringLiteral("^") + pattern + QStringLiteral("$"),
                                    QRegularExpression::CaseInsensitiveOption);
        return re.match(toText(cell)).hasMatch();
    }
    if (isText(cell) && isText(criterionValue)) {
        return QString::compare(std::get<QString>(cell), std::get<QString>(criterionValue),
                                Qt::CaseInsensitive) == 0;
    }
    return compareValues(cell, criterionValue) == 0;
}

double serialFromDate(const QDate &date)
{
    static const QDate kEpoch(1899, 12, 30);
    return double(kEpoch.daysTo(date));
}

QDate dateFromSerial(double serial)
{
    static const QDate kEpoch(1899, 12, 30);
    return kEpoch.addDays(int(std::floor(serial)));
}

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
                if (const auto d = toNumber(v); d.has_value()
                    && !(skipTextAndBlank && isText(v))) {
                    numbers.append(*d);
                }
            }
        } else {
            const Value v = std::get<Value>(arg);
            if (const auto d = toNumber(v); d.has_value() && !(skipTextAndBlank && isText(v))) {
                numbers.append(*d);
            }
        }
    }
    return finish(numbers);
}

} // namespace

void registerMathFunctions(QHash<QString, FunctionSpec> &r)
{
    const auto simple1 = [](std::function<Value(double)> f) {
        return FunctionSpec{1, 1, false,
                            [f](EvalContext &, const QVector<Arg> &args) -> Value {
                                const auto x = scalarNumber(args.at(0));
                                if (!x.has_value()) return isErr(scalarOrValue(args.at(0)))
                                                          ? std::get<Err>(scalarOrValue(args.at(0)))
                                                          : Err::Value;
                                return f(*x);
                            }};
    };
    r.insert(QStringLiteral("ABS"), simple1([](double x) { return std::abs(x); }));
    r.insert(QStringLiteral("SQRT"), simple1([](double x) {
                  return x < 0 ? Value(Err::Num) : Value(std::sqrt(x));
              }));
    r.insert(QStringLiteral("EXP"), simple1([](double x) { return std::exp(x); }));
    r.insert(QStringLiteral("LN"), simple1([](double x) {
                  return x <= 0 ? Value(Err::Num) : Value(std::log(x));
              }));
    r.insert(QStringLiteral("LOG10"), simple1([](double x) {
                  return x <= 0 ? Value(Err::Num) : Value(std::log10(x));
              }));
    r.insert(QStringLiteral("SIGN"), simple1([](double x) {
                  return double(x > 0 ? 1 : (x < 0 ? -1 : 0));
              }));
    r.insert(QStringLiteral("INT"), simple1([](double x) { return std::floor(x); }));
    r.insert(QStringLiteral("SIN"), simple1([](double x) { return std::sin(x); }));
    r.insert(QStringLiteral("COS"), simple1([](double x) { return std::cos(x); }));
    r.insert(QStringLiteral("TAN"), simple1([](double x) { return std::tan(x); }));
    r.insert(QStringLiteral("ASIN"), simple1([](double x) { return std::asin(x); }));
    r.insert(QStringLiteral("ACOS"), simple1([](double x) { return std::acos(x); }));
    r.insert(QStringLiteral("ATAN"), simple1([](double x) { return std::atan(x); }));
    r.insert(QStringLiteral("DEGREES"), simple1([](double x) { return x * 180.0 / M_PI; }));
    r.insert(QStringLiteral("RADIANS"), simple1([](double x) { return x * M_PI / 180.0; }));
    r.insert(QStringLiteral("EVEN"), simple1([](double x) {
                  const double up = std::ceil(std::abs(x));
                  return std::copysign(up + int(qint64(up)) % 2, x);
              }));
    r.insert(QStringLiteral("ODD"), simple1([](double x) {
                  const double up = std::ceil(std::abs(x));
                  return std::copysign((qint64(up) % 2 == 0) ? up + 1 : up, x);
              }));

    r.insert(QStringLiteral("PI"), FunctionSpec{0, 0, false, [](EvalContext &, const QVector<Arg> &) {
                  return M_PI;
              }});
    r.insert(QStringLiteral("RAND"), FunctionSpec{0, 0, true, [](EvalContext &, const QVector<Arg> &) {
                  return double(QRandomGenerator::global()->generateDouble());
              }});
    r.insert(QStringLiteral("RANDBETWEEN"),
             FunctionSpec{2, 2, true, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto lo = scalarNumber(args.at(0));
                              const auto hi = scalarNumber(args.at(1));
                              if (!lo || !hi) return Err::Value;
                              return double(QRandomGenerator::global()->bounded(
                                  qint64(std::ceil(*hi)) - qint64(std::floor(*lo)) + 1)
                                            + qint64(std::floor(*lo)));
                          }});
    r.insert(QStringLiteral("POWER"),
             FunctionSpec{2, 2, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto b = scalarNumber(args.at(0));
                              const auto e = scalarNumber(args.at(1));
                              if (!b || !e) return Err::Value;
                              return std::pow(*b, *e);
                          }});
    r.insert(QStringLiteral("LOG"),
             FunctionSpec{1, 2, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto x = scalarNumber(args.at(0));
                              if (!x || *x <= 0) return Err::Num;
                              if (args.size() == 1) return std::log10(*x);
                              const auto base = scalarNumber(args.at(1));
                              if (!base || *base <= 0) return Err::Num;
                              return std::log(*x) / std::log(*base);
                          }});
    r.insert(QStringLiteral("MOD"),
             FunctionSpec{2, 2, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto a = scalarNumber(args.at(0));
                              const auto b = scalarNumber(args.at(1));
                              if (!a || !b || *b == 0) return Err::Div0;
                              return *a - *b * std::floor(*a / *b);
                          }});
    r.insert(QStringLiteral("QUOTIENT"),
             FunctionSpec{2, 2, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto a = scalarNumber(args.at(0));
                              const auto b = scalarNumber(args.at(1));
                              if (!a || !b || *b == 0) return Err::Div0;
                              return double(qint64(*a / *b));
                          }});
    r.insert(QStringLiteral("ROUND"),
             FunctionSpec{1, 2, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto x = scalarNumber(args.at(0));
                              if (!x) return Err::Value;
                              int digits = 0;
                              if (args.size() > 1) {
                                  const auto d = scalarNumber(args.at(1));
                                  if (!d) return Err::Value;
                                  digits = int(*d);
                              }
                              return roundHalfAway(*x, digits);
                          }});
    const auto rounding = [](std::function<double(double, int)> mode) {
        return FunctionSpec{1, 2, false,
                            [mode](EvalContext &, const QVector<Arg> &args) -> Value {
                                const auto x = scalarNumber(args.at(0));
                                if (!x) return Err::Value;
                                int digits = 0;
                                if (args.size() > 1) {
                                    const auto d = scalarNumber(args.at(1));
                                    if (!d) return Err::Value;
                                    digits = int(*d);
                                }
                                return mode(*x, digits);
                            }};
    };
    r.insert(QStringLiteral("ROUNDUP"),
             rounding([](double x, int d) {
                 const double f = std::pow(10.0, d);
                 return std::copysign(std::ceil(std::abs(x) * f) / f, x);
             }));
    r.insert(QStringLiteral("ROUNDDOWN"),
             rounding([](double x, int d) {
                 const double f = std::pow(10.0, d);
                 return std::copysign(std::floor(std::abs(x) * f) / f, x);
             }));
    r.insert(QStringLiteral("TRUNC"), rounding([](double x, int d) {
                  const double f = std::pow(10.0, d);
                  return std::trunc(x * f) / f;
              }));
    r.insert(QStringLiteral("CEILING"),
             FunctionSpec{1, 2, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto x = scalarNumber(args.at(0));
                              const double sig = args.size() > 1
                                  ? scalarNumber(args.at(1)).value_or(1.0)
                                  : 1.0;
                              if (!x || sig == 0) return Err::Num;
                              return std::ceil(*x / sig) * sig;
                          }});
    r.insert(QStringLiteral("FLOOR"),
             FunctionSpec{1, 2, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto x = scalarNumber(args.at(0));
                              const double sig = args.size() > 1
                                  ? scalarNumber(args.at(1)).value_or(1.0)
                                  : 1.0;
                              if (!x || sig == 0) return Err::Num;
                              return std::floor(*x / sig) * sig;
                          }});
    r.insert(QStringLiteral("MROUND"),
             FunctionSpec{2, 2, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto x = scalarNumber(args.at(0));
                              const auto m = scalarNumber(args.at(1));
                              if (!x || !m || *m == 0) return Err::Num;
                              return std::round(*x / *m) * *m;
                          }});
    r.insert(QStringLiteral("FACT"),
             FunctionSpec{1, 1, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto x = scalarNumber(args.at(0));
                              if (!x || *x < 0) return Err::Num;
                              double out = 1;
                              for (qint64 i = 2; i <= qint64(std::floor(*x)); ++i) out *= double(i);
                              return out;
                          }});
    r.insert(QStringLiteral("COMBIN"),
             FunctionSpec{2, 2, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto n = scalarNumber(args.at(0));
                              const auto k = scalarNumber(args.at(1));
                              if (!n || !k) return Err::Value;
                              const qint64 ni = qint64(std::floor(*n));
                              const qint64 ki = qint64(std::floor(*k));
                              if (ki < 0 || ni < ki) return Err::Num;
                              double out = 1;
                              for (qint64 i = 1; i <= ki; ++i) {
                                  out = out * double(ni - ki + i) / double(i);
                              }
                              return std::round(out);
                          }});
    r.insert(QStringLiteral("GCD"),
             FunctionSpec{1, 255, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              qint64 g = 0;
                              for (const Arg &arg : args) {
                                  const auto x = scalarNumber(arg);
                                  if (!x) return Err::Value;
                                  g = std::gcd(g, qint64(std::abs(std::floor(*x))));
                              }
                              return double(g);
                          }});
    r.insert(QStringLiteral("LCM"),
             FunctionSpec{1, 255, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              qint64 l = 1;
                              for (const Arg &arg : args) {
                                  const auto x = scalarNumber(arg);
                                  if (!x) return Err::Value;
                                  l = std::lcm(l, qint64(std::abs(std::floor(*x))));
                              }
                              return double(l);
                          }});
    r.insert(QStringLiteral("ATAN2"),
             FunctionSpec{2, 2, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                              const auto y = scalarNumber(args.at(0));
                              const auto x = scalarNumber(args.at(1));
                              if (!y || !x) return Err::Value;
                              return std::atan2(*y, *x);
                          }});

    r.insert(QStringLiteral("SUM"),
             FunctionSpec{1, 255, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                              return numberAgg(ctx, args, [](const QVector<double> &ns) -> Value {
                                  return std::accumulate(ns.begin(), ns.end(), 0.0);
                              });
                          }});
    r.insert(QStringLiteral("PRODUCT"),
             FunctionSpec{1, 255, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                              return numberAgg(ctx, args, [](const QVector<double> &ns) -> Value {
                                  return std::accumulate(ns.begin(), ns.end(), 1.0,
                                                         std::multiplies<double>());
                              });
                          }});
    r.insert(QStringLiteral("SUMPRODUCT"),
             FunctionSpec{1, 255, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                              QVector<QVector<Value>> grids;
                              for (const Arg &arg : args) {
                                  const auto *view = std::get_if<RangeView>(&arg);
                                  QVector<Value> values;
                                  Value err;
                                  if (view) {
                                      err = flatRangeValues(ctx, *view, &values);
                                  } else {
                                      values.append(std::get<Value>(arg));
                                  }
                                  if (isErr(err)) return err;
                                  grids.append(values);
                              }
                              if (grids.isEmpty()) return 0.0;
                              const qsizetype n = grids.first().size();
                              for (const QVector<Value> &grid : grids) {
                                  if (grid.size() != n) return Err::Value;
                              }
                              double sum = 0;
                              for (qsizetype i = 0; i < n; ++i) {
                                  double product = 1;
                                  for (const QVector<Value> &grid : grids) {
                                      const auto d = toNumber(grid.at(i));
                                      if (!d.has_value()) return Err::Value;
                                      product *= *d;
                                  }
                                  sum += product;
                              }
                              return sum;
                          }});
}

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
