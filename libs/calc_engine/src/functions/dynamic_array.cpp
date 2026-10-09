// SPDX-License-Identifier: GPL-3.0-or-later
// The dynamic-array functions (M66, D-075): FILTER, SORT, SORTBY, UNIQUE,
// SEQUENCE, RANDARRAY — each returns an array, which a dynamic-array
// formula spills (spill.cpp) and an aggregate takes as an argument — and
// ANCHORARRAY, the function behind the A1# spill reference.
//
// Semantics follow Microsoft's documentation of each function: SEQUENCE
// is row-major; UNIQUE and SORT compare text without regard to case and
// keep the first spelling; SORT and SORTBY are stable, ordering numbers
// before text before logicals before errors, blanks last; FILTER's include
// array must match the rows (or the columns) of the array; an empty
// result is #CALC! unless FILTER has an if_empty; a zero size is #CALC!
// and a negative one #VALUE!.

#include "functions.h"

#include "../eval_array.h"

#include <QRandomGenerator>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace QindaOffice::Calc {

namespace {

// The largest array these build (rows x columns); beyond it, #NUM!.
constexpr qint64 kMaxResultCells = qint64(1) << 20;

int sortRank(const Value &value)
{
    if (isNumber(value)) {
        return 0;
    }
    if (isText(value)) {
        return 1;
    }
    if (isBool(value)) {
        return 2;
    }
    return isErr(value) ? 3 : 4; // blanks last
}

int compareForSort(const Value &a, const Value &b)
{
    const int ra = sortRank(a);
    const int rb = sortRank(b);
    if (ra != rb) {
        return ra < rb ? -1 : 1;
    }
    switch (ra) {
    case 0: {
        const double x = std::get<double>(a);
        const double y = std::get<double>(b);
        return x < y ? -1 : (x > y ? 1 : 0);
    }
    case 1:
        return QString::compare(std::get<QString>(a), std::get<QString>(b), Qt::CaseInsensitive);
    case 2:
        return int(std::get<bool>(a)) - int(std::get<bool>(b));
    case 3:
        return int(std::get<Err>(a)) - int(std::get<Err>(b));
    default:
        return 0;
    }
}

// Equality for UNIQUE: numbers by value, text without regard to case.
QString uniqueKey(const Value &value)
{
    if (const auto *number = std::get_if<double>(&value)) {
        return QStringLiteral("n") + QString::number(*number, 'g', 17);
    }
    if (const auto *text = std::get_if<QString>(&value)) {
        return QStringLiteral("s") + text->toCaseFolded();
    }
    if (const auto *flag = std::get_if<bool>(&value)) {
        return *flag ? QStringLiteral("b1") : QStringLiteral("b0");
    }
    if (const auto *err = std::get_if<Err>(&value)) {
        return QStringLiteral("e") + QString::number(int(*err));
    }
    return QStringLiteral("z");
}

// One row (byCol false) or column (true) of `grid`.
Value at(const ArrayValue &grid, bool byCol, qint32 line, qint32 index)
{
    return byCol ? grid.cells.at(index * grid.cols + line) : grid.cells.at(line * grid.cols + index);
}

// `lines` of `grid` in the given order, as a new array.
ArrayValue pickLines(const ArrayValue &grid, bool byCol, const QVector<qint32> &lines)
{
    ArrayValue out;
    out.rows = byCol ? grid.rows : qint32(lines.size());
    out.cols = byCol ? qint32(lines.size()) : grid.cols;
    out.cells.resize(out.rows * out.cols);
    for (qint32 k = 0; k < qint32(lines.size()); ++k) {
        const qint32 length = byCol ? grid.rows : grid.cols;
        for (qint32 i = 0; i < length; ++i) {
            const Value value = at(grid, byCol, lines.at(k), i);
            if (byCol) {
                out.cells[i * out.cols + k] = value;
            } else {
                out.cells[k * out.cols + i] = value;
            }
        }
    }
    return out;
}

// An optional argument as a number (`fallback` when absent); nullopt when
// it does not read as one. An error argument is reported through `error`.
std::optional<double> numberArg(const QVector<Arg> &args, int index, double fallback, Value *error)
{
    if (index >= args.size()) {
        return fallback;
    }
    const Value value = scalarOrValue(args.at(index));
    if (isErr(value)) {
        *error = value;
        return std::nullopt;
    }
    if (isBlank(value)) {
        return fallback;
    }
    return toNumber(value);
}

std::optional<bool> boolArg(const QVector<Arg> &args, int index, bool fallback, Value *error)
{
    if (index >= args.size()) {
        return fallback;
    }
    const Value value = scalarOrValue(args.at(index));
    if (isErr(value)) {
        *error = value;
        return std::nullopt;
    }
    return isBlank(value) ? std::optional<bool>(fallback) : toBool(value);
}

// rows x columns for SEQUENCE / RANDARRAY: zero is #CALC!, negative or
// too large an error of its own.
Value checkSize(double rows, double cols)
{
    if (rows < 0 || cols < 0) {
        return Err::Value;
    }
    if (rows < 1 || cols < 1) {
        return Err::Calc;
    }
    return rows * cols > double(kMaxResultCells) ? Value(Err::Num) : Value();
}

Arg sequence(EvalContext &, const QVector<Arg> &args)
{
    Value error;
    const auto rows = numberArg(args, 0, 1, &error);
    const auto cols = numberArg(args, 1, 1, &error);
    const auto start = numberArg(args, 2, 1, &error);
    const auto step = numberArg(args, 3, 1, &error);
    if (isErr(error)) {
        return error;
    }
    if (!rows || !cols || !start || !step) {
        return Err::Value;
    }
    const double r = std::trunc(*rows);
    const double c = std::trunc(*cols);
    if (const Value bad = checkSize(r, c); isErr(bad)) {
        return bad;
    }
    ArrayValue out{qint32(r), qint32(c), {}};
    out.cells.reserve(out.rows * out.cols);
    for (qint32 i = 0; i < out.rows * out.cols; ++i) {
        out.cells.append(*start + double(i) * *step);
    }
    return out;
}

Arg randArray(EvalContext &, const QVector<Arg> &args)
{
    Value error;
    const auto rows = numberArg(args, 0, 1, &error);
    const auto cols = numberArg(args, 1, 1, &error);
    const auto low = numberArg(args, 2, 0, &error);
    const auto high = numberArg(args, 3, 1, &error);
    const auto whole = boolArg(args, 4, false, &error);
    if (isErr(error)) {
        return error;
    }
    if (!rows || !cols || !low || !high || !whole || *low > *high) {
        return Err::Value;
    }
    if (*whole && (*low != std::floor(*low) || *high != std::floor(*high))) {
        return Err::Value;
    }
    const double r = std::trunc(*rows);
    const double c = std::trunc(*cols);
    if (const Value bad = checkSize(r, c); isErr(bad)) {
        return bad;
    }
    ArrayValue out{qint32(r), qint32(c), {}};
    QRandomGenerator *random = QRandomGenerator::global();
    for (qint32 i = 0; i < out.rows * out.cols; ++i) {
        out.cells.append(*whole ? *low + std::floor(random->generateDouble() * (*high - *low + 1))
                                : *low + random->generateDouble() * (*high - *low));
    }
    return out;
}

Arg unique(EvalContext &ctx, const QVector<Arg> &args)
{
    ArrayValue grid;
    if (const Value failed = arrayFromArg(ctx, args.at(0), &grid); isErr(failed)) {
        return failed;
    }
    Value error;
    const auto byCol = boolArg(args, 1, false, &error);
    const auto once = boolArg(args, 2, false, &error);
    if (isErr(error)) {
        return error;
    }
    if (!byCol || !once) {
        return Err::Value;
    }
    const qint32 lines = *byCol ? grid.cols : grid.rows;
    const qint32 length = *byCol ? grid.rows : grid.cols;
    QHash<QString, qint32> counts;
    QVector<QPair<QString, qint32>> firsts; // key, first line, in order
    for (qint32 line = 0; line < lines; ++line) {
        QString key;
        for (qint32 i = 0; i < length; ++i) {
            key += uniqueKey(at(grid, *byCol, line, i)) + QChar(0x1f);
        }
        if (counts[key]++ == 0) {
            firsts.append({key, line});
        }
    }
    QVector<qint32> kept;
    for (const auto &[key, line] : std::as_const(firsts)) {
        if (!*once || counts.value(key) == 1) {
            kept.append(line);
        }
    }
    if (kept.isEmpty()) {
        return Err::Calc;
    }
    return pickLines(grid, *byCol, kept);
}

// Stable order of `lines` by a list of (key line, ascending) pairs.
QVector<qint32> sortedLines(qint32 lines, const std::function<int(qint32, qint32)> &compare)
{
    QVector<qint32> order(lines);
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&compare](qint32 a, qint32 b) { return compare(a, b) < 0; });
    return order;
}

Arg sort(EvalContext &ctx, const QVector<Arg> &args)
{
    ArrayValue grid;
    if (const Value failed = arrayFromArg(ctx, args.at(0), &grid); isErr(failed)) {
        return failed;
    }
    Value error;
    const auto byCol = boolArg(args, 3, false, &error);
    if (isErr(error)) {
        return error;
    }
    if (!byCol) {
        return Err::Value;
    }
    ArrayValue indices{1, 1, {1.0}};
    ArrayValue orders{1, 1, {1.0}};
    if (args.size() > 1 && isErr(arrayFromArg(ctx, args.at(1), &indices))) {
        return Err::Value;
    }
    if (args.size() > 2 && isErr(arrayFromArg(ctx, args.at(2), &orders))) {
        return Err::Value;
    }
    const qint32 length = *byCol ? grid.rows : grid.cols;
    QVector<QPair<qint32, int>> keys; // index into the line, direction
    for (qsizetype k = 0; k < indices.cells.size(); ++k) {
        const Value orderValue = orders.cells.size() == 1 ? orders.cells.first() : orders.cells.value(k);
        const auto index = toNumber(isBlank(indices.cells.at(k)) ? Value(1.0) : indices.cells.at(k));
        const auto order = toNumber(isBlank(orderValue) ? Value(1.0) : orderValue);
        if (!index || !order || std::trunc(*index) < 1 || std::trunc(*index) > length
            || (*order != 1 && *order != -1)) {
            return Err::Value;
        }
        keys.append({qint32(std::trunc(*index)) - 1, int(*order)});
    }
    const QVector<qint32> order = sortedLines(*byCol ? grid.cols : grid.rows, [&](qint32 a, qint32 b) {
        for (const auto &[index, direction] : keys) {
            if (const int cmp = compareForSort(at(grid, *byCol, a, index), at(grid, *byCol, b, index)); cmp != 0) {
                // Blanks stay last whichever way the key runs.
                const bool blankA = isBlank(at(grid, *byCol, a, index));
                const bool blankB = isBlank(at(grid, *byCol, b, index));
                return blankA != blankB ? cmp : cmp * direction;
            }
        }
        return 0;
    });
    return pickLines(grid, *byCol, order);
}

Arg sortBy(EvalContext &ctx, const QVector<Arg> &args)
{
    ArrayValue grid;
    if (const Value failed = arrayFromArg(ctx, args.at(0), &grid); isErr(failed)) {
        return failed;
    }
    struct Key {
        ArrayValue values;
        int direction = 1;
    };
    QVector<Key> keys;
    std::optional<bool> byCol;
    for (qsizetype i = 1; i < args.size(); i += 2) {
        Key key;
        if (isErr(arrayFromArg(ctx, args.at(i), &key.values))) {
            return Err::Value;
        }
        if (i + 1 < args.size()) {
            const Value order = scalarOrValue(args.at(i + 1));
            const auto direction = toNumber(isBlank(order) ? Value(1.0) : order);
            if (isErr(order) || !direction || (*direction != 1 && *direction != -1)) {
                return Err::Value;
            }
            key.direction = int(*direction);
        }
        // A by-array is one column as tall as the array (sort rows) or
        // one row as wide (sort columns); every one must agree.
        const bool column = key.values.cols == 1 && key.values.rows == grid.rows;
        const bool row = key.values.rows == 1 && key.values.cols == grid.cols;
        if (!column && !row) {
            return Err::Value;
        }
        const bool thisByCol = !column;
        if (byCol && *byCol != thisByCol) {
            return Err::Value;
        }
        byCol = thisByCol;
        keys.append(key);
    }
    if (keys.isEmpty()) {
        return Err::Value;
    }
    const QVector<qint32> order = sortedLines(*byCol ? grid.cols : grid.rows, [&](qint32 a, qint32 b) {
        for (const Key &key : keys) {
            const Value &va = key.values.cells.at(a);
            const Value &vb = key.values.cells.at(b);
            if (const int cmp = compareForSort(va, vb); cmp != 0) {
                return isBlank(va) != isBlank(vb) ? cmp : cmp * key.direction;
            }
        }
        return 0;
    });
    return pickLines(grid, *byCol, order);
}

Arg filter(EvalContext &ctx, const QVector<Arg> &args)
{
    ArrayValue grid;
    if (const Value failed = arrayFromArg(ctx, args.at(0), &grid); isErr(failed)) {
        return failed;
    }
    ArrayValue include;
    if (const Value failed = arrayFromArg(ctx, args.at(1), &include); isErr(failed)) {
        return failed;
    }
    const bool byRows = include.cols == 1 && include.rows == grid.rows;
    const bool byCols = !byRows && include.rows == 1 && include.cols == grid.cols;
    if (!byRows && !byCols) {
        return Err::Value;
    }
    QVector<qint32> kept;
    for (qint32 i = 0; i < qint32(include.cells.size()); ++i) {
        const Value &flag = include.cells.at(i);
        if (isErr(flag)) {
            return flag;
        }
        const auto keep = toBool(flag);
        if (!keep) {
            return Err::Value;
        }
        if (*keep) {
            kept.append(i);
        }
    }
    if (kept.isEmpty()) {
        return args.size() > 2 ? args.at(2) : Arg(Value(Err::Calc));
    }
    return pickLines(grid, byCols, kept);
}

// A1#: the spill range of the anchor A1 names (preserveRefArgs keeps the
// argument a reference).
Arg anchorArray(EvalContext &ctx, const QVector<Arg> &args)
{
    const auto *view = std::get_if<RangeView>(&args.at(0));
    if (view == nullptr || !view->isValid() || !(view->range.topLeft == view->range.bottomRight)) {
        return Err::Ref;
    }
    const std::optional<CellRange> extent = ctx.host->spillExtent(view->sheet, view->range.topLeft);
    if (!extent) {
        return Err::Ref;
    }
    return RangeView{view->sheet, *extent};
}

} // namespace

void registerDynamicArrayFunctions(QHash<QString, FunctionSpec> &r)
{
    r.insert(QStringLiteral("SEQUENCE"), FunctionSpec{1, 4, false, sequence});
    r.insert(QStringLiteral("RANDARRAY"), FunctionSpec{0, 5, true, randArray});
    r.insert(QStringLiteral("UNIQUE"), FunctionSpec{1, 3, false, unique});
    r.insert(QStringLiteral("SORT"), FunctionSpec{1, 4, false, sort});
    r.insert(QStringLiteral("SORTBY"), FunctionSpec{2, 255, false, sortBy});
    r.insert(QStringLiteral("FILTER"), FunctionSpec{2, 3, false, filter});
    r.insert(QStringLiteral("ANCHORARRAY"), FunctionSpec{1, 1, false, anchorArray, true});
}

} // namespace QindaOffice::Calc
