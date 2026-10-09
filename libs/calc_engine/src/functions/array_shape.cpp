// SPDX-License-Identifier: GPL-3.0-or-later
// M69 (D-078): the functions that shape arrays — CHOOSECOLS, CHOOSEROWS,
// TAKE, DROP, VSTACK, HSTACK, TOCOL, TOROW, EXPAND, WRAPROWS, WRAPCOLS —
// the matrix functions MDETERM, MINVERSE, MUNIT, and ROW/COLUMN/ROWS/
// COLUMNS over arrays (ROW(A1:A3) is {1;2;3}, as in Excel 365). Expected
// values are worked from Microsoft's documentation of each function.

#include "functions.h"

#include "../eval_array.h"

#include <cmath>

namespace QindaOffice::Calc {

namespace {

// The grid an argument holds, or the error reading it.
std::optional<Value> gridOf(const EvalContext &ctx, const Arg &arg, ArrayValue *grid)
{
    if (const Value failed = arrayFromArg(ctx, arg, grid); isErr(failed)) {
        return failed;
    }
    return std::nullopt;
}

Value at(const ArrayValue &grid, qint32 row, qint32 col)
{
    return grid.cells.at(row * grid.cols + col);
}

// An integer argument; nullopt (with the error) when it is not a number.
std::optional<qint32> integer(const Arg &arg, Value *error)
{
    const Value v = scalarOrValue(arg);
    if (isErr(v)) {
        *error = v;
        return std::nullopt;
    }
    const auto n = toNumber(v);
    if (!n.has_value()) {
        *error = Err::Value;
        return std::nullopt;
    }
    return qint32(std::trunc(*n));
}

Arg result(ArrayValue grid)
{
    if (grid.cells.isEmpty()) {
        return Value(Err::Calc);
    }
    return grid.isSingleCell() ? Arg(grid.cells.first()) : Arg(std::move(grid));
}

// CHOOSECOLS / CHOOSEROWS: the listed columns (rows); negative counts
// from the end, anything else out of range is #VALUE!.
FunctionSpec choose(bool columns)
{
    return FunctionSpec{2, 255, false, [columns](EvalContext &ctx, const QVector<Arg> &args) -> Arg {
        ArrayValue grid;
        if (const auto err = gridOf(ctx, args.at(0), &grid)) {
            return *err;
        }
        const qint32 extent = columns ? grid.cols : grid.rows;
        QVector<qint32> picks;
        for (qsizetype i = 1; i < args.size(); ++i) {
            Value error;
            const std::optional<qint32> n = integer(args.at(i), &error);
            if (!n) {
                return error;
            }
            if (*n == 0 || std::abs(*n) > extent) {
                return Value(Err::Value);
            }
            picks.append(*n > 0 ? *n - 1 : extent + *n);
        }
        ArrayValue out;
        out.rows = columns ? grid.rows : qint32(picks.size());
        out.cols = columns ? qint32(picks.size()) : grid.cols;
        for (qint32 r = 0; r < out.rows; ++r) {
            for (qint32 c = 0; c < out.cols; ++c) {
                out.cells.append(columns ? at(grid, r, picks.at(c)) : at(grid, picks.at(r), c));
            }
        }
        return result(out);
    }};
}

// TAKE keeps, DROP removes, the first (or, negative, last) rows/columns.
FunctionSpec takeOrDrop(bool take)
{
    return FunctionSpec{2, 3, false, [take](EvalContext &ctx, const QVector<Arg> &args) -> Arg {
        ArrayValue grid;
        if (const auto err = gridOf(ctx, args.at(0), &grid)) {
            return *err;
        }
        qint32 r0 = 0;
        qint32 r1 = grid.rows;
        qint32 c0 = 0;
        qint32 c1 = grid.cols;
        for (int axis = 0; axis < 2 && axis + 1 < args.size(); ++axis) {
            if (std::holds_alternative<Value>(args.at(axis + 1)) && isBlank(std::get<Value>(args.at(axis + 1)))) {
                continue; // an omitted count keeps the whole axis
            }
            Value error;
            const std::optional<qint32> n = integer(args.at(axis + 1), &error);
            if (!n) {
                return error;
            }
            const qint32 extent = axis == 0 ? grid.rows : grid.cols;
            const qint32 count = qMin(std::abs(*n), extent);
            qint32 &lo = axis == 0 ? r0 : c0;
            qint32 &hi = axis == 0 ? r1 : c1;
            if (take) {
                if (*n == 0) {
                    return Value(Err::Calc);
                }
                (*n > 0 ? hi : lo) = *n > 0 ? count : extent - count;
            } else {
                (*n > 0 ? lo : hi) = *n > 0 ? count : extent - count;
            }
        }
        if (r1 <= r0 || c1 <= c0) {
            return Value(Err::Calc);
        }
        ArrayValue out{r1 - r0, c1 - c0, {}};
        for (qint32 r = r0; r < r1; ++r) {
            for (qint32 c = c0; c < c1; ++c) {
                out.cells.append(at(grid, r, c));
            }
        }
        return result(out);
    }};
}

// VSTACK / HSTACK: the arrays one after another, short ones padded #N/A.
FunctionSpec stack(bool vertical)
{
    return FunctionSpec{1, 254, false, [vertical](EvalContext &ctx, const QVector<Arg> &args) -> Arg {
        QVector<ArrayValue> grids;
        qint32 across = 0;
        qint32 along = 0;
        for (const Arg &arg : args) {
            ArrayValue grid;
            if (const auto err = gridOf(ctx, arg, &grid)) {
                return *err;
            }
            across = qMax(across, vertical ? grid.cols : grid.rows);
            along += vertical ? grid.rows : grid.cols;
            grids.append(grid);
        }
        ArrayValue out{vertical ? along : across, vertical ? across : along, {}};
        out.cells.fill(Value(Err::NA), out.rows * out.cols);
        qint32 offset = 0;
        for (const ArrayValue &grid : std::as_const(grids)) {
            for (qint32 r = 0; r < grid.rows; ++r) {
                for (qint32 c = 0; c < grid.cols; ++c) {
                    const qint32 row = vertical ? offset + r : r;
                    const qint32 col = vertical ? c : offset + c;
                    out.cells[row * out.cols + col] = at(grid, r, c);
                }
            }
            offset += vertical ? grid.rows : grid.cols;
        }
        return result(out);
    }};
}

// TOCOL / TOROW: every element in one line; ignore 1 blanks, 2 errors, 3
// both; scanned by row unless asked to scan by column.
FunctionSpec flatten(bool column)
{
    return FunctionSpec{1, 3, false, [column](EvalContext &ctx, const QVector<Arg> &args) -> Arg {
        ArrayValue grid;
        if (const auto err = gridOf(ctx, args.at(0), &grid)) {
            return *err;
        }
        Value error;
        const qint32 ignore = args.size() > 1 ? integer(args.at(1), &error).value_or(-1) : 0;
        const bool byColumn = args.size() > 2 && toBool(scalarOrValue(args.at(2))).value_or(false);
        if (ignore < 0 || ignore > 3) {
            return isErr(error) ? error : Value(Err::Value);
        }
        ArrayValue out;
        const qint32 outer = byColumn ? grid.cols : grid.rows;
        const qint32 inner = byColumn ? grid.rows : grid.cols;
        for (qint32 i = 0; i < outer; ++i) {
            for (qint32 j = 0; j < inner; ++j) {
                const Value v = byColumn ? at(grid, j, i) : at(grid, i, j);
                if (((ignore & 1) && isBlank(v)) || ((ignore & 2) && isErr(v))) {
                    continue;
                }
                out.cells.append(v);
            }
        }
        out.rows = column ? qint32(out.cells.size()) : 1;
        out.cols = column ? 1 : qint32(out.cells.size());
        return result(out);
    }};
}

// WRAPROWS / WRAPCOLS: a vector cut into rows (columns) of `count`.
FunctionSpec wrap(bool rows)
{
    return FunctionSpec{2, 3, false, [rows](EvalContext &ctx, const QVector<Arg> &args) -> Arg {
        ArrayValue grid;
        if (const auto err = gridOf(ctx, args.at(0), &grid)) {
            return *err;
        }
        Value error;
        const std::optional<qint32> count = integer(args.at(1), &error);
        if (!count) {
            return error;
        }
        if ((grid.rows != 1 && grid.cols != 1) || *count < 1) {
            return Value(*count < 1 ? Err::Num : Err::Value);
        }
        const Value pad = args.size() > 2 ? scalarOrValue(args.at(2)) : Value(Err::NA);
        const qint32 n = qint32(grid.cells.size());
        const qint32 lines = (n + *count - 1) / *count;
        ArrayValue out{rows ? lines : *count, rows ? *count : lines, {}};
        out.cells.fill(pad, out.rows * out.cols);
        for (qint32 i = 0; i < n; ++i) {
            const qint32 line = i / *count;
            const qint32 pos = i % *count;
            out.cells[rows ? line * out.cols + pos : pos * out.cols + line] = grid.cells.at(i);
        }
        return result(out);
    }};
}

// A square matrix of numbers, or the error Excel gives.
std::optional<Value> squareMatrix(const EvalContext &ctx, const Arg &arg, QVector<double> *m, qint32 *n)
{
    ArrayValue grid;
    if (const auto err = gridOf(ctx, arg, &grid)) {
        return err;
    }
    if (grid.rows != grid.cols) {
        return Value(Err::Value);
    }
    *n = grid.rows;
    for (const Value &v : std::as_const(grid.cells)) {
        if (!isNumber(v)) {
            return Value(isErr(v) ? v : Value(Err::Value));
        }
        m->append(std::get<double>(v));
    }
    return std::nullopt;
}

// Gauss-Jordan with partial pivoting over [m | inverse]; the determinant.
double eliminate(QVector<double> m, qint32 n, QVector<double> *inverse)
{
    QVector<double> inv(n * n, 0.0);
    for (qint32 i = 0; i < n; ++i) {
        inv[i * n + i] = 1.0;
    }
    double det = 1.0;
    for (qint32 col = 0; col < n; ++col) {
        qint32 pivot = col;
        for (qint32 r = col + 1; r < n; ++r) {
            if (std::abs(m.at(r * n + col)) > std::abs(m.at(pivot * n + col))) {
                pivot = r;
            }
        }
        if (std::abs(m.at(pivot * n + col)) < 1e-300) {
            return 0.0;
        }
        if (pivot != col) {
            for (qint32 c = 0; c < n; ++c) {
                std::swap(m[pivot * n + c], m[col * n + c]);
                std::swap(inv[pivot * n + c], inv[col * n + c]);
            }
            det = -det;
        }
        const double p = m.at(col * n + col);
        det *= p;
        for (qint32 c = 0; c < n; ++c) {
            m[col * n + c] /= p;
            inv[col * n + c] /= p;
        }
        for (qint32 r = 0; r < n; ++r) {
            const double f = m.at(r * n + col);
            if (r == col || f == 0.0) {
                continue;
            }
            for (qint32 c = 0; c < n; ++c) {
                m[r * n + c] -= f * m.at(col * n + c);
                inv[r * n + c] -= f * inv.at(col * n + c);
            }
        }
    }
    *inverse = inv;
    return det;
}

// ROW / COLUMN: of a range of several cells, the array of its rows
// (columns) — Excel 365's answer, whose first element is the old one.
FunctionSpec position(bool row)
{
    FunctionSpec spec{0, 1, false, [row](EvalContext &ctx, const QVector<Arg> &args) -> Arg {
        if (args.isEmpty()) {
            return double((row ? ctx.here.row : ctx.here.col) + 1);
        }
        const auto *view = std::get_if<RangeView>(&args.at(0));
        if (view == nullptr) {
            return Value(Err::Value);
        }
        const qint32 first = row ? view->range.topLeft.row : view->range.topLeft.col;
        const qint32 last = row ? view->range.bottomRight.row : view->range.bottomRight.col;
        ArrayValue out{row ? last - first + 1 : 1, row ? 1 : last - first + 1, {}};
        for (qint32 i = first; i <= last; ++i) {
            out.cells.append(double(i + 1));
        }
        return result(out);
    }};
    spec.preserveRefArgs = true;
    return spec;
}

FunctionSpec extent(bool rows)
{
    FunctionSpec spec{1, 1, false, [rows](EvalContext &, const QVector<Arg> &args) -> Arg {
        if (const auto *view = std::get_if<RangeView>(&args.at(0))) {
            return double(rows ? view->range.bottomRight.row - view->range.topLeft.row + 1
                               : view->range.bottomRight.col - view->range.topLeft.col + 1);
        }
        if (const auto *array = std::get_if<ArrayValue>(&args.at(0))) {
            return double(rows ? array->rows : array->cols); // ROWS({1;2;3}) — M69
        }
        return isErr(std::get<Value>(args.at(0))) ? std::get<Value>(args.at(0)) : Value(1.0);
    }};
    spec.preserveRefArgs = true;
    return spec;
}

} // namespace

void registerArrayShapeFunctions(QHash<QString, FunctionSpec> &r)
{
    r.insert(QStringLiteral("CHOOSECOLS"), choose(true));
    r.insert(QStringLiteral("CHOOSEROWS"), choose(false));
    r.insert(QStringLiteral("TAKE"), takeOrDrop(true));
    r.insert(QStringLiteral("DROP"), takeOrDrop(false));
    r.insert(QStringLiteral("VSTACK"), stack(true));
    r.insert(QStringLiteral("HSTACK"), stack(false));
    r.insert(QStringLiteral("TOCOL"), flatten(true));
    r.insert(QStringLiteral("TOROW"), flatten(false));
    r.insert(QStringLiteral("WRAPROWS"), wrap(true));
    r.insert(QStringLiteral("WRAPCOLS"), wrap(false));
    r.insert(QStringLiteral("ROW"), position(true));
    r.insert(QStringLiteral("COLUMN"), position(false));
    r.insert(QStringLiteral("ROWS"), extent(true));
    r.insert(QStringLiteral("COLUMNS"), extent(false));
    r.insert(QStringLiteral("EXPAND"),
             FunctionSpec{2, 4, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Arg {
                              ArrayValue grid;
                              if (const auto err = gridOf(ctx, args.at(0), &grid)) {
                                  return *err;
                              }
                              Value error;
                              const qint32 rows = integer(args.at(1), &error).value_or(-1);
                              const bool colsGiven = args.size() > 2 && !(std::holds_alternative<Value>(args.at(2))
                                                                          && isBlank(std::get<Value>(args.at(2))));
                              const qint32 cols = colsGiven ? integer(args.at(2), &error).value_or(-1) : grid.cols;
                              if (isErr(error)) {
                                  return error;
                              }
                              if (rows < grid.rows || cols < grid.cols) {
                                  return Value(Err::Value);
                              }
                              const Value pad = args.size() > 3 ? scalarOrValue(args.at(3)) : Value(Err::NA);
                              ArrayValue out{rows, cols, {}};
                              for (qint32 rr = 0; rr < rows; ++rr) {
                                  for (qint32 cc = 0; cc < cols; ++cc) {
                                      out.cells.append(rr < grid.rows && cc < grid.cols ? at(grid, rr, cc) : pad);
                                  }
                              }
                              return result(out);
                          }});
    r.insert(QStringLiteral("MUNIT"),
             FunctionSpec{1, 1, false, [](EvalContext &, const QVector<Arg> &args) -> Arg {
                              Value error;
                              const qint32 n = integer(args.at(0), &error).value_or(0);
                              if (isErr(error) || n < 1) {
                                  return isErr(error) ? error : Value(Err::Value);
                              }
                              ArrayValue out{n, n, {}};
                              for (qint32 i = 0; i < n * n; ++i) {
                                  out.cells.append(i % (n + 1) == 0 ? 1.0 : 0.0);
                              }
                              return result(out);
                          }});
    r.insert(QStringLiteral("MDETERM"),
             FunctionSpec{1, 1, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Arg {
                              QVector<double> m;
                              qint32 n = 0;
                              if (const auto err = squareMatrix(ctx, args.at(0), &m, &n)) {
                                  return *err;
                              }
                              QVector<double> inverse;
                              return eliminate(m, n, &inverse);
                          }});
    r.insert(QStringLiteral("MINVERSE"),
             FunctionSpec{1, 1, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Arg {
                              QVector<double> m;
                              qint32 n = 0;
                              if (const auto err = squareMatrix(ctx, args.at(0), &m, &n)) {
                                  return *err;
                              }
                              QVector<double> inverse;
                              if (eliminate(m, n, &inverse) == 0.0) {
                                  return Value(Err::Num); // singular
                              }
                              ArrayValue out{n, n, {}};
                              for (const double v : std::as_const(inverse)) {
                                  out.cells.append(v);
                              }
                              return result(out);
                          }});
}

} // namespace QindaOffice::Calc
