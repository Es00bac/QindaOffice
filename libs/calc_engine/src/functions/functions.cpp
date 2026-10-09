// SPDX-License-Identifier: GPL-3.0-or-later
#include "functions.h"

#include "../eval_array.h"
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
        registerMathExtraFunctions(r);
        registerStatisticalFunctions(r);
        registerRegressionFunctions(r);
        registerConditionalAggFunctions(r);
        registerTextFunctions(r);
        registerTextJoinFunctions(r);
        registerLogicalFunctions(r);
        registerLookupFunctions(r);
        registerDateFunctions(r);
        registerDateExtraFunctions(r);
        registerInfoFunctions(r);
        registerSubtotalFunctions(r);
        registerFinancialFunctions(r);
        registerFinancialExtraFunctions(r);
        registerFinancialMoreFunctions(r);
        registerStatisticalMoreFunctions(r);
        registerMathMoreFunctions(r);
        registerLookupMoreFunctions(r);
        registerTextMoreFunctions(r);
        registerDateMoreFunctions(r);
        registerDynamicArrayFunctions(r);
        return r;
    }();
    return registry;
}

Value scalarOrValue(const Arg &arg)
{
    if (std::holds_alternative<RangeView>(arg)) {
        return Err::Value;
    }
    if (const auto *array = std::get_if<ArrayValue>(&arg)) {
        // A computed grid in a scalar slot: one cell derefs, more is #VALUE!
        // (the same rule the evaluator applies at the top level).
        return array->isSingleCell() ? array->cells.first() : Value(Err::Value);
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

namespace {

// Builds a case-insensitive anchored regex from Excel's * ? wildcards,
// escaping every other regex metacharacter and honouring the ~ escape
// (~* and ~? are literal; ~~ is a literal tilde).
QRegularExpression wildcardRegex(const QString &criteria)
{
    QString pattern;
    for (int i = 0; i < criteria.size(); ++i) {
        const QChar c = criteria.at(i);
        if (c == QLatin1Char('~') && i + 1 < criteria.size()) {
            const QChar next = criteria.at(++i);
            pattern += QRegularExpression::escape(QString(next));
            continue;
        }
        if (c == QLatin1Char('*')) {
            pattern += QStringLiteral(".*");
        } else if (c == QLatin1Char('?')) {
            pattern += QLatin1Char('.');
        } else {
            pattern += QRegularExpression::escape(QString(c));
        }
    }
    return QRegularExpression(QStringLiteral("^") + pattern + QStringLiteral("$"),
                              QRegularExpression::CaseInsensitiveOption);
}

bool textMatchesWithWildcards(const QString &text, const QString &criteria)
{
    if (criteria.contains(QLatin1Char('*')) || criteria.contains(QLatin1Char('?'))
        || criteria.contains(QLatin1Char('~'))) {
        return wildcardRegex(criteria).match(text).hasMatch();
    }
    return QString::compare(text, criteria, Qt::CaseInsensitive) == 0;
}

} // namespace

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
                // A blank is not a number: it fails every numeric comparison
                // except "not equal" (Excel's COUNTIF(range,"<1") skips
                // blanks; the old coercion counted them as 0 — M61).
                if (isBlank(cell)) {
                    return op.prefix == QLatin1String("<>");
                }
                const std::optional<double> cellNum = toNumber(cell);
                if (!cellNum.has_value()) {
                    // Excel: "<>5" counts TEXT cells as not-equal; only
                    // blanks fall outside every numeric comparison.
                    return op.prefix == QLatin1String("<>") && !isBlank(cell);
                }
                const double diff = *cellNum - num;
                return op.check(diff == 0 ? 0 : (diff < 0 ? -1 : 1));
            }
            // Wildcards apply after operators too ("=a*", "<>b?").
            if (rest.contains(QLatin1Char('*')) || rest.contains(QLatin1Char('?'))
                || rest.contains(QLatin1Char('~'))) {
                const bool matched = textMatchesWithWildcards(toText(cell), rest);
                return op.check(matched ? 0 : 1);
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
    if (criteria.contains(QLatin1Char('*')) || criteria.contains(QLatin1Char('?'))
        || criteria.contains(QLatin1Char('~'))) {
        return textMatchesWithWildcards(toText(cell), criteria);
    }
    // An error cell matches its own spelling ("#N/A"), as in Excel (M61).
    if (const auto *err = std::get_if<Err>(&cell)) {
        return QString::compare(errText(*err), criteria, Qt::CaseInsensitive) == 0;
    }
    // Numeric criterion coerces numeric text ("5" matches 5).
    if (isText(cell) && isText(criterionValue)) {
        const QString cellText = std::get<QString>(cell);
        bool cellNumOk = false;
        const double cellNum = cellText.toDouble(&cellNumOk);
        if (cellNumOk) {
            return qFuzzyCompare(cellNum, std::get<double>(criterionValue));
        }
        return QString::compare(cellText, std::get<QString>(criterionValue),
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
                              if (*lo > *hi) return Err::Num; // Qt bounded() requires a positive span
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
                              const auto x = scalarNumber(args.at(0));
                              const auto y = scalarNumber(args.at(1));
                              if (!x || !y) return Err::Value;
                              if (*x == 0.0 && *y == 0.0) return Value(Err::Div0);
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
                                  // Ranges, computed arrays and scalars all
                                  // flatten here: the second argument of
                                  // SUMPRODUCT(C8:C19,(1+rate)^B8:B19) is an
                                  // array, and it must line up with the first.
                                  QVector<Value> values;
                                  const Value err = flatArgValues(ctx, arg, &values);
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

} // namespace QindaOffice::Calc
