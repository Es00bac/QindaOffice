// SPDX-License-Identifier: GPL-3.0-or-later
// M69 (D-078): SINGLE — the '@' implicit-intersection operator — INDEX
// as Excel has it (a reference into a range, an element of an array), and
// the array functions Excel workbooks use with array formulas: TRANSPOSE,
// MMULT. Expected values are worked from Microsoft's documentation of each
// function.

#include "functions.h"

#include "../eval_array.h"
#include "../eval_lift.h"

namespace QindaOffice::Calc {

namespace {

// Every element of an argument as a number grid; false (with the error)
// when an element is not a number.
bool numberGrid(const EvalContext &ctx, const Arg &arg, ArrayValue *grid, Value *error)
{
    if (const Value failed = arrayFromArg(ctx, arg, grid); isErr(failed)) {
        *error = failed;
        return false;
    }
    for (const Value &cell : std::as_const(grid->cells)) {
        if (isErr(cell)) {
            *error = cell;
            return false;
        }
        if (!isNumber(cell)) {
            *error = Err::Value;
            return false;
        }
    }
    return true;
}

// The rows and columns an argument spans (a value is one cell).
QPair<qint32, qint32> shapeOf(const Arg &arg)
{
    if (const auto *view = std::get_if<RangeView>(&arg)) {
        return {view->range.bottomRight.row - view->range.topLeft.row + 1,
                view->range.bottomRight.col - view->range.topLeft.col + 1};
    }
    if (const auto *array = std::get_if<ArrayValue>(&arg)) {
        return {array->rows, array->cols};
    }
    return {1, 1};
}

// INDEX(array, row, [column], [area]): the cell, row or column picked out
// of a range — as a REFERENCE, so INDEX(…):INDEX(…) and SUM(INDEX(A:C,0,2))
// work — or out of an array. Row (column) 0 is the whole column (row); a
// single row takes its one index as the column (Excel's rules). One area
// only: area 1.
Arg indexOf(EvalContext &ctx, const QVector<Arg> &args)
{
    const auto number = [&args](int i, qint32 *out) -> std::optional<Value> {
        if (i >= args.size()) {
            return std::nullopt;
        }
        const Value v = scalarOrValue(args.at(i));
        if (isErr(v)) {
            return v;
        }
        const auto n = toNumber(v);
        if (!n.has_value()) {
            return Value(Err::Value);
        }
        *out = qint32(*n);
        return std::nullopt;
    };
    qint32 row = 0;
    qint32 col = 0;
    qint32 area = 1;
    for (const auto &[i, out] : {std::pair<int, qint32 *>{1, &row}, {2, &col}, {3, &area}}) {
        if (const auto error = number(i, out)) {
            return *error;
        }
    }
    const auto [rows, cols] = shapeOf(args.at(0));
    if (args.size() == 2 && rows == 1) {
        col = row; // INDEX(A1:C1, 2) is B1
        row = 1;
    }
    if (row < 0 || col < 0 || area != 1) {
        return Value(row < 0 || col < 0 ? Err::Value : Err::Ref);
    }
    if (row > rows || col > cols) {
        return Value(Err::Ref);
    }
    const qint32 r0 = row == 0 ? 0 : row - 1;
    const qint32 r1 = row == 0 ? rows - 1 : row - 1;
    const qint32 c0 = col == 0 ? 0 : col - 1;
    const qint32 c1 = col == 0 ? cols - 1 : col - 1;
    if (const auto *view = std::get_if<RangeView>(&args.at(0))) {
        const CellAddr origin = view->range.topLeft;
        return RangeView{view->sheet, {{origin.row + r0, origin.col + c0}, {origin.row + r1, origin.col + c1}}};
    }
    ArrayValue grid;
    if (const Value failed = arrayFromArg(ctx, args.at(0), &grid); isErr(failed)) {
        return failed;
    }
    ArrayValue out;
    out.rows = r1 - r0 + 1;
    out.cols = c1 - c0 + 1;
    for (qint32 r = r0; r <= r1; ++r) {
        for (qint32 c = c0; c <= c1; ++c) {
            out.cells.append(grid.cells.at(r * grid.cols + c));
        }
    }
    return out.isSingleCell() ? Arg(out.cells.first()) : Arg(out);
}

} // namespace

void registerArrayMoreFunctions(QHash<QString, FunctionSpec> &r)
{
    r.insert(QStringLiteral("INDEX"), FunctionSpec{2, 4, false, indexOf, true});
    // @x: the one cell of a range in the formula's own row or column, an
    // array's first element (eval_lift.h). The reference is kept, never
    // lifted: SINGLE reads it whole.
    r.insert(QStringLiteral("SINGLE"),
             FunctionSpec{1, 1, false,
                          [](EvalContext &ctx, const QVector<Arg> &args) -> Arg {
                              return implicitIntersection(ctx, args.at(0));
                          },
                          true});
    r.insert(QStringLiteral("TRANSPOSE"),
             FunctionSpec{1, 1, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Arg {
                              ArrayValue grid;
                              if (const Value failed = arrayFromArg(ctx, args.at(0), &grid); isErr(failed)) {
                                  return failed;
                              }
                              ArrayValue out;
                              out.rows = grid.cols;
                              out.cols = grid.rows;
                              out.cells.reserve(grid.cells.size());
                              for (qint32 row = 0; row < out.rows; ++row) {
                                  for (qint32 col = 0; col < out.cols; ++col) {
                                      out.cells.append(grid.cells.at(col * grid.cols + row));
                                  }
                              }
                              return out;
                          }});
    r.insert(QStringLiteral("MMULT"),
             FunctionSpec{2, 2, false, [](EvalContext &ctx, const QVector<Arg> &args) -> Arg {
                              ArrayValue a;
                              ArrayValue b;
                              Value error;
                              if (!numberGrid(ctx, args.at(0), &a, &error) || !numberGrid(ctx, args.at(1), &b, &error)) {
                                  return error;
                              }
                              if (a.cols != b.rows) {
                                  return Value(Err::Value);
                              }
                              ArrayValue out;
                              out.rows = a.rows;
                              out.cols = b.cols;
                              for (qint32 i = 0; i < a.rows; ++i) {
                                  for (qint32 j = 0; j < b.cols; ++j) {
                                      double sum = 0;
                                      for (qint32 k = 0; k < a.cols; ++k) {
                                          sum += std::get<double>(a.cells.at(i * a.cols + k))
                                              * std::get<double>(b.cells.at(k * b.cols + j));
                                      }
                                      out.cells.append(sum);
                                  }
                              }
                              return out;
                          }});
}

} // namespace QindaOffice::Calc
