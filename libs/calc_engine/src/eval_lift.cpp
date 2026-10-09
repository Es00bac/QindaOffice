// SPDX-License-Identifier: GPL-3.0-or-later
// Lifting and implicit intersection (eval_lift.h, M69, D-078).

#include "eval_lift.h"

#include "eval.h"
#include "eval_array.h"

namespace QindaOffice::Calc {

namespace {

thread_local CallFrame *t_frame = nullptr;

// Installs a frame for the duration of one function call; frames nest
// (a function may evaluate others) and restore on the way out.
class FrameScope {
public:
    explicit FrameScope(CallFrame *frame)
        : m_previous(t_frame)
    {
        t_frame = frame;
    }
    ~FrameScope() { t_frame = m_previous; }
    FrameScope(const FrameScope &) = delete;
    FrameScope &operator=(const FrameScope &) = delete;

private:
    CallFrame *m_previous;
};

// Element (r, c) of a grid the way a lifted argument broadcasts: one row or
// column stretches; beyond a smaller grid Excel gives #N/A.
Value elementOf(const ArrayValue &grid, qint32 r, qint32 c)
{
    const qint32 row = grid.rows == 1 ? 0 : r;
    const qint32 col = grid.cols == 1 ? 0 : c;
    if (row >= grid.rows || col >= grid.cols) {
        return Err::NA;
    }
    return grid.cells.at(row * grid.cols + col);
}

// One value out of a call's result for element (r, c) of a lift: an array
// a lift produced answers that element, an array the function returned on
// its own (SEQUENCE inside a lift) its first value, as Excel does.
Value resultElement(const EvalContext &ctx, const Arg &result, bool lifted, qint32 r, qint32 c)
{
    if (const auto *value = std::get_if<Value>(&result)) {
        return *value;
    }
    ArrayValue grid;
    if (const Value err = arrayFromArg(ctx, result, &grid); isErr(err)) {
        return err;
    }
    if (grid.cells.isEmpty()) {
        return Err::Calc;
    }
    return lifted ? elementOf(grid, r, c) : grid.cells.first();
}

// The cap on one lift: the same ceiling a materialized range has.
constexpr qint64 kMaxLiftCells = qint64(1) << 22;

Arg liftCall(const FunctionSpec &spec, const QVector<Arg> &args, quint64 mask, EvalContext &ctx,
             const NodePtr &node)
{
    QVector<int> indices;
    QVector<ArrayValue> grids;
    qint32 rows = 1;
    qint32 cols = 1;
    for (int i = 0; i < args.size() && i < 64; ++i) {
        if ((mask & (quint64(1) << i)) == 0) {
            continue;
        }
        ArrayValue grid;
        if (const Value err = arrayFromArg(ctx, args.at(i), &grid); isErr(err)) {
            return err;
        }
        rows = qMax(rows, grid.rows);
        cols = qMax(cols, grid.cols);
        indices.append(i);
        grids.append(std::move(grid));
    }
    if (qint64(rows) * qint64(cols) > kMaxLiftCells) {
        return Value(Err::Value);
    }
    ArrayValue out;
    out.rows = rows;
    out.cols = cols;
    out.cells.reserve(rows * cols);
    QVector<Arg> element = args;
    for (qint32 r = 0; r < rows; ++r) {
        for (qint32 c = 0; c < cols; ++c) {
            for (qsizetype k = 0; k < indices.size(); ++k) {
                element[indices.at(k)] = elementOf(grids.at(k), r, c);
            }
            bool inner = false;
            const Arg result = callFunction(spec, element, ctx, node, &inner);
            out.cells.append(resultElement(ctx, result, inner, r, c));
        }
    }
    return out;
}

} // namespace

CallFrame *currentCallFrame()
{
    return t_frame;
}

bool holdsSeveralCells(const Arg &arg)
{
    if (const auto *view = std::get_if<RangeView>(&arg)) {
        return view->isValid() && view->range.cellCount() > 1;
    }
    if (const auto *array = std::get_if<ArrayValue>(&arg)) {
        return array->cells.size() > 1;
    }
    return false;
}

Value implicitIntersection(const EvalContext &ctx, const Arg &arg)
{
    if (const auto *value = std::get_if<Value>(&arg)) {
        return *value;
    }
    if (const auto *array = std::get_if<ArrayValue>(&arg)) {
        return array->cells.isEmpty() ? Value(Err::Value) : array->cells.first();
    }
    const RangeView &view = std::get<RangeView>(arg);
    if (!view.isValid() || ctx.host == nullptr) {
        return Err::Ref;
    }
    const CellRange &range = view.range;
    const auto read = [&ctx, &view](qint32 row, qint32 col) { return ctx.host->cellValue(view.sheet, row, col); };
    const bool inRows = ctx.here.row >= range.topLeft.row && ctx.here.row <= range.bottomRight.row;
    const bool inCols = ctx.here.col >= range.topLeft.col && ctx.here.col <= range.bottomRight.col;
    if (range.topLeft == range.bottomRight) {
        return read(range.topLeft.row, range.topLeft.col);
    }
    if (range.topLeft.col == range.bottomRight.col) {
        return inRows ? read(ctx.here.row, range.topLeft.col) : Value(Err::Value);
    }
    if (range.topLeft.row == range.bottomRight.row) {
        return inCols ? read(range.topLeft.row, ctx.here.col) : Value(Err::Value);
    }
    return inRows && inCols ? read(ctx.here.row, ctx.here.col) : Value(Err::Value);
}

Arg callFunction(const FunctionSpec &spec, const QVector<Arg> &args, EvalContext &ctx, const NodePtr &node,
                 bool *lifted)
{
    if (lifted != nullptr) {
        *lifted = false;
    }
    CallFrame frame{args.constData(), args.constData() + args.size(), 0, &ctx};
    Arg result;
    {
        const FrameScope scope(&frame);
        result = spec.fn(ctx, args);
    }
    if (frame.scalarMask == 0) {
        return result;
    }
    if (ctx.mode == ArrayMode::Dynamic) {
        if (lifted != nullptr) {
            *lifted = true;
        }
        return liftCall(spec, args, frame.scalarMask, ctx, node);
    }
    // Legacy: each argument the function wanted as one value is worked out
    // again the way pre-dynamic-array Excel did — with implicit
    // intersection all the way down — and the function is called again.
    QVector<Arg> intersected = args;
    for (int i = 0; i < args.size() && i < 64; ++i) {
        if ((frame.scalarMask & (quint64(1) << i)) == 0) {
            continue;
        }
        const bool tree = node && i < node->args.size() && node->args.at(i);
        intersected[i] = tree ? evalNode(node->args.at(i), ctx) : implicitIntersection(ctx, args.at(i));
    }
    return callFunction(spec, intersected, ctx, node, nullptr);
}

} // namespace QindaOffice::Calc
