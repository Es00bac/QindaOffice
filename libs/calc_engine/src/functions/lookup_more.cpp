// SPDX-License-Identifier: GPL-3.0-or-later
// Lookup and information functions added for Excel interchange (M61,
// D-069): XMATCH, HYPERLINK (its value is the friendly name — the link
// itself is the formula's business), ISFORMULA, FORMULATEXT, ISREF, SHEET
// and SHEETS. The cell-introspecting ones read through
// EvalHost::cellFormula and take their reference argument as a reference
// (preserveRefArgs).

#include "functions.h"

#include "../eval_array.h"

#include <QRegularExpression>

namespace QindaOffice::Calc {

namespace {

// Excel's wildcards: * any run, ? one character, ~ escapes either.
bool wildcardMatch(const QString &text, const QString &pattern)
{
    QString regex;
    for (qsizetype i = 0; i < pattern.size(); ++i) {
        const QChar c = pattern.at(i);
        if (c == QLatin1Char('~') && i + 1 < pattern.size()) {
            regex += QRegularExpression::escape(QString(pattern.at(++i)));
        } else if (c == QLatin1Char('*')) {
            regex += QStringLiteral(".*");
        } else if (c == QLatin1Char('?')) {
            regex += QLatin1Char('.');
        } else {
            regex += QRegularExpression::escape(QString(c));
        }
    }
    return QRegularExpression(QStringLiteral("\\A") + regex + QStringLiteral("\\z"),
                              QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption)
        .match(text)
        .hasMatch();
}

// How a lookup value compares with a candidate of the same kind: numbers
// numerically, text without regard to case, booleans FALSE < TRUE. A
// candidate of another kind never matches (nullopt).
std::optional<int> lookupCompare(const Value &candidate, const Value &needle)
{
    if (isNumber(candidate) && isNumber(needle)) {
        const double a = std::get<double>(candidate);
        const double b = std::get<double>(needle);
        return a < b ? -1 : (a > b ? 1 : 0);
    }
    if (isText(candidate) && isText(needle)) {
        return QString::compare(std::get<QString>(candidate), std::get<QString>(needle), Qt::CaseInsensitive);
    }
    if (isBool(candidate) && isBool(needle)) {
        return int(std::get<bool>(candidate)) - int(std::get<bool>(needle));
    }
    return std::nullopt;
}

// The lookup vector of XMATCH: one row or one column, in order.
Value lookupVector(const EvalContext &ctx, const Arg &arg, QVector<Value> *out)
{
    if (const auto *view = std::get_if<RangeView>(&arg)) {
        if (view->range.topLeft.row != view->range.bottomRight.row
            && view->range.topLeft.col != view->range.bottomRight.col) {
            return Err::Value;
        }
    } else if (const auto *array = std::get_if<ArrayValue>(&arg)) {
        if (array->rows != 1 && array->cols != 1) {
            return Err::Value;
        }
    }
    // Not flatArgValues: it stops at the first error cell, and a lookup
    // must step over those.
    ArrayValue grid;
    if (const Value err = arrayFromArg(ctx, arg, &grid); isErr(err)) {
        return err;
    }
    *out = grid.cells;
    return {};
}

int optionalInt(const QVector<Arg> &args, int index, int fallback, bool *ok)
{
    *ok = true;
    if (index >= args.size()) {
        return fallback;
    }
    const auto n = scalarNumber(args.at(index));
    *ok = n.has_value();
    return n.has_value() ? int(*n) : fallback;
}

} // namespace

void registerLookupMoreFunctions(QHash<QString, FunctionSpec> &r)
{
    // XMATCH(value, array, [match_mode], [search_mode]). match_mode 0 exact,
    // -1 exact or next smaller, 1 exact or next larger, 2 wildcard;
    // search_mode 1 first-to-last, -1 last-to-first (the binary modes 2 and
    // -2 search linearly: same answer on sorted data, no wrong one on
    // unsorted data).
    r.insert(QStringLiteral("XMATCH"),
             FunctionSpec{2, 4, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                 const Value needle = scalarOrValue(args.at(0));
                 if (isErr(needle)) return needle;
                 QVector<Value> candidates;
                 if (const Value err = lookupVector(ctx, args.at(1), &candidates); isErr(err)) return err;
                 bool ok = true;
                 const int matchMode = optionalInt(args, 2, 0, &ok);
                 if (!ok || matchMode < -1 || matchMode > 2) return Err::Value;
                 const int searchMode = optionalInt(args, 3, 1, &ok);
                 if (!ok || (searchMode != 1 && searchMode != -1 && searchMode != 2 && searchMode != -2)) {
                     return Err::Value;
                 }
                 const bool backward = searchMode == -1;
                 qsizetype best = -1;
                 for (qsizetype step = 0; step < candidates.size(); ++step) {
                     const qsizetype i = backward ? candidates.size() - 1 - step : step;
                     const Value &candidate = candidates.at(i);
                     if (matchMode == 2) {
                         if (isText(needle) && isText(candidate)
                             && wildcardMatch(std::get<QString>(candidate), std::get<QString>(needle))) {
                             return double(i + 1);
                         }
                         if (!isText(needle) && lookupCompare(candidate, needle) == 0) {
                             return double(i + 1);
                         }
                         continue;
                     }
                     const std::optional<int> cmp = lookupCompare(candidate, needle);
                     if (!cmp.has_value()) continue;
                     if (*cmp == 0) return double(i + 1);
                     if ((matchMode == -1 && *cmp < 0) || (matchMode == 1 && *cmp > 0)) {
                         // Keep the closest; the first one met wins a tie.
                         if (best < 0) {
                             best = i;
                         } else {
                             const int closer = *lookupCompare(candidate, candidates.at(best));
                             if ((matchMode == -1 && closer > 0) || (matchMode == 1 && closer < 0)) {
                                 best = i;
                             }
                         }
                     }
                 }
                 return best < 0 ? Value(Err::NA) : Value(double(best + 1));
             }});
    // HYPERLINK(link, [friendly_name]): the cell shows the friendly name.
    r.insert(QStringLiteral("HYPERLINK"),
             FunctionSpec{1, 2, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                 return scalarOrValue(args.at(args.size() > 1 ? 1 : 0));
             }});
    r.insert(QStringLiteral("ISFORMULA"),
             FunctionSpec{1, 1, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                 const auto *view = std::get_if<RangeView>(&args.at(0));
                 if (view == nullptr || !view->isValid()) return Err::Value;
                 return !ctx.host->cellFormula(view->sheet, view->range.topLeft.row,
                                               view->range.topLeft.col).isEmpty();
             }, true});
    r.insert(QStringLiteral("FORMULATEXT"),
             FunctionSpec{1, 1, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                 const auto *view = std::get_if<RangeView>(&args.at(0));
                 if (view == nullptr || !view->isValid()) return Err::Value;
                 const QString formula = ctx.host->cellFormula(view->sheet, view->range.topLeft.row,
                                                               view->range.topLeft.col);
                 return formula.isEmpty() ? Value(Err::NA) : Value(QStringLiteral("=") + formula);
             }, true});
    r.insert(QStringLiteral("ISREF"),
             FunctionSpec{1, 1, false, [](EvalContext &, const QVector<Arg> &args) -> Value {
                 return std::holds_alternative<RangeView>(args.at(0));
             }, true});
    // SHEET([value]): the 1-based number of the sheet a reference (or a
    // sheet name) is on; with no argument, the formula's own sheet.
    r.insert(QStringLiteral("SHEET"),
             FunctionSpec{0, 1, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                 if (args.isEmpty()) return double(ctx.sheet + 1);
                 if (const auto *view = std::get_if<RangeView>(&args.at(0))) {
                     return view->isValid() ? Value(double(view->sheet + 1)) : Value(Err::Ref);
                 }
                 const Value v = scalarOrValue(args.at(0));
                 if (isErr(v)) return v;
                 if (!isText(v)) return Err::NA;
                 const qint32 index = ctx.host->sheetIndex(std::get<QString>(v));
                 return index < 0 ? Value(Err::NA) : Value(double(index + 1));
             }, true});
    r.insert(QStringLiteral("SHEETS"),
             FunctionSpec{0, 1, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Value {
                 if (args.isEmpty()) return double(ctx.host->sheetCount());
                 return std::holds_alternative<RangeView>(args.at(0)) ? Value(1.0) : Value(Err::Value);
             }, true});
}

} // namespace QindaOffice::Calc
