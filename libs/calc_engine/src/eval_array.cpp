// SPDX-License-Identifier: GPL-3.0-or-later
#include "eval_array.h"

#include <cmath>

namespace QindaOffice::Calc {

namespace {

// The same ceiling flatRangeValues uses: a guard against a whole-column
// reference accidentally materializing four million cells.
constexpr qint64 kMaxArrayCells = 1 << 22;

// Stretches an index along a degenerate axis: a one-row or one-column
// operand repeats for every row/column of the result.
inline qint32 stretch(qint32 index, qint32 extent)
{
    return extent == 1 ? 0 : index;
}

} // namespace

bool isArrayLike(const Arg &arg)
{
    if (std::holds_alternative<RangeView>(arg)) {
        return true;
    }
    return std::holds_alternative<ArrayValue>(arg);
}

Value arrayFromArg(const EvalContext &ctx, const Arg &arg, ArrayValue *out)
{
    if (const auto *array = std::get_if<ArrayValue>(&arg)) {
        *out = *array;
        return Value();
    }
    if (const auto *view = std::get_if<RangeView>(&arg)) {
        if (ctx.host == nullptr || !view->isValid()) {
            return Err::Ref;
        }
        const qint64 count = view->range.cellCount();
        if (count > kMaxArrayCells) {
            return Err::Value;
        }
        out->rows = view->range.bottomRight.row - view->range.topLeft.row + 1;
        out->cols = view->range.bottomRight.col - view->range.topLeft.col + 1;
        out->cells.clear();
        out->cells.reserve(int(count));
        for (qint32 r = view->range.topLeft.row; r <= view->range.bottomRight.row; ++r) {
            for (qint32 c = view->range.topLeft.col; c <= view->range.bottomRight.col; ++c) {
                // Error cells are kept in place, not propagated here: the
                // aggregate that consumes the array decides what an error
                // element means to it.
                out->cells.append(ctx.host->cellValue(view->sheet, r, c));
            }
        }
        return Value();
    }
    out->rows = 1;
    out->cols = 1;
    out->cells = {std::get<Value>(arg)};
    return Value();
}

namespace {

// Excel's comparison operators (M69, D-078): a blank compares as the other
// side's empty value — "" against text, 0 against a number, FALSE against a
// boolean — and text compares without regard to case. Before M69 =A1=""
// was FALSE for an empty A1 and ="a"="A" was FALSE.
int operatorCompare(const Value &a, const Value &b)
{
    const auto emptyLike = [](const Value &other) -> Value {
        if (isText(other)) {
            return QString();
        }
        return isBool(other) ? Value(false) : Value(0.0);
    };
    const Value x = isBlank(a) ? emptyLike(b) : a;
    const Value y = isBlank(b) ? emptyLike(x) : b;
    if (isText(x) && isText(y)) {
        return QString::compare(std::get<QString>(x), std::get<QString>(y), Qt::CaseInsensitive);
    }
    return compareValues(x, y);
}

} // namespace

Value applyBinaryOp(Op op, const Value &lhs, const Value &rhs)
{
    if (isErr(lhs)) {
        return lhs;
    }
    if (isErr(rhs)) {
        return rhs;
    }
    switch (op) {
    case Op::Add:
    case Op::Sub:
    case Op::Mul:
    case Op::Div:
    case Op::Pow: {
        const auto a = toNumber(lhs);
        const auto b = toNumber(rhs);
        if (!a.has_value() || !b.has_value()) {
            return Err::Value;
        }
        switch (op) {
        case Op::Add: return *a + *b;
        case Op::Sub: return *a - *b;
        case Op::Mul: return *a * *b;
        case Op::Div:
            if (*b == 0) {
                return Err::Div0;
            }
            return *a / *b;
        case Op::Pow: return std::pow(*a, *b);
        default: break;
        }
        return Err::Value;
    }
    case Op::Concat: return toText(lhs) + toText(rhs);
    case Op::Eq: return operatorCompare(lhs, rhs) == 0;
    case Op::Ne: return operatorCompare(lhs, rhs) != 0;
    case Op::Lt: return operatorCompare(lhs, rhs) < 0;
    case Op::Le: return operatorCompare(lhs, rhs) <= 0;
    case Op::Gt: return operatorCompare(lhs, rhs) > 0;
    case Op::Ge: return operatorCompare(lhs, rhs) >= 0;
    default: break;
    }
    return Err::Value; // Range/Union only survive in argument position
}

Value applyUnaryOp(Op op, const Value &operand)
{
    if (isErr(operand)) {
        return operand;
    }
    const auto d = toNumber(operand);
    if (!d.has_value()) {
        return Err::Value;
    }
    switch (op) {
    case Op::Percent: return *d / 100.0;
    case Op::Neg: return -*d;
    default: break;
    }
    return *d; // Op::Pos
}

Arg broadcastBinary(const EvalContext &ctx, Op op, const Arg &lhs, const Arg &rhs)
{
    ArrayValue left;
    ArrayValue right;
    if (const Value err = arrayFromArg(ctx, lhs, &left); isErr(err)) {
        return err;
    }
    if (const Value err = arrayFromArg(ctx, rhs, &right); isErr(err)) {
        return err;
    }
    const auto axis = [](qint32 a, qint32 b, qint32 *out) {
        if (a == b || b == 1) {
            *out = a;
        } else if (a == 1) {
            *out = b;
        } else {
            return false;
        }
        return true;
    };
    ArrayValue result;
    if (!axis(left.rows, right.rows, &result.rows) || !axis(left.cols, right.cols, &result.cols)) {
        return Value(Err::Value);
    }
    if (qint64(result.rows) * qint64(result.cols) > kMaxArrayCells) {
        return Value(Err::Value);
    }
    result.cells.reserve(result.rows * result.cols);
    for (qint32 r = 0; r < result.rows; ++r) {
        for (qint32 c = 0; c < result.cols; ++c) {
            const Value &a = left.cells.at(stretch(r, left.rows) * left.cols + stretch(c, left.cols));
            const Value &b =
                right.cells.at(stretch(r, right.rows) * right.cols + stretch(c, right.cols));
            result.cells.append(applyBinaryOp(op, a, b));
        }
    }
    return result;
}

Arg broadcastUnary(const EvalContext &ctx, Op op, const Arg &operand)
{
    ArrayValue array;
    if (const Value err = arrayFromArg(ctx, operand, &array); isErr(err)) {
        return err;
    }
    for (Value &cell : array.cells) {
        cell = applyUnaryOp(op, cell);
    }
    return array;
}

namespace {

// The shared tail of both arg readers: an array's cells, with the first
// error cell reported the way a range read reports one.
Value appendArrayCells(const ArrayValue &array, QVector<Value> *out, bool skipBlanks)
{
    for (const Value &cell : array.cells) {
        if (isErr(cell)) {
            out->clear();
            return cell;
        }
        if (skipBlanks && isBlank(cell)) {
            continue;
        }
        out->append(cell);
    }
    return Value();
}

} // namespace

Value flatArgValues(const EvalContext &ctx, const Arg &arg, QVector<Value> *out)
{
    if (const auto *view = std::get_if<RangeView>(&arg)) {
        return flatRangeValues(ctx, *view, out);
    }
    out->clear();
    if (const auto *array = std::get_if<ArrayValue>(&arg)) {
        return appendArrayCells(*array, out, false);
    }
    const Value &scalar = std::get<Value>(arg);
    if (isErr(scalar)) {
        return scalar;
    }
    out->append(scalar);
    return Value();
}

Value sparseArgValues(const EvalContext &ctx, const Arg &arg, QVector<Value> *out)
{
    if (const auto *view = std::get_if<RangeView>(&arg)) {
        return sparseRangeValues(ctx, *view, out);
    }
    out->clear();
    if (const auto *array = std::get_if<ArrayValue>(&arg)) {
        return appendArrayCells(*array, out, true);
    }
    const Value &scalar = std::get<Value>(arg);
    if (isErr(scalar)) {
        return scalar;
    }
    out->append(scalar);
    return Value();
}

} // namespace QindaOffice::Calc
