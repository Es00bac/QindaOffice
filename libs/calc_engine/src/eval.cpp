// SPDX-License-Identifier: GPL-3.0-or-later
#include "eval.h"

#include "eval_array.h"
#include "eval_lift.h"
#include "eval_refs.h"

#include <functional>

namespace QindaOffice::Calc {

using namespace EvalRefs;

namespace {

Value scalarFromArg(const EvalContext &ctx, const Arg &arg)
{
    const auto *view = std::get_if<RangeView>(&arg);
    if (view != nullptr) {
        // A range-valued function result (OFFSET/INDIRECT) in scalar
        // position. A Legacy formula intersects it with its own cell, as
        // Excel does (M69, D-078 — it used to take the top-left cell, which
        // is what a Dynamic formula's one-value slot still does).
        if (!view->isValid()) {
            return Err::Ref;
        }
        if (ctx.mode == ArrayMode::Legacy) {
            return implicitIntersection(ctx, arg);
        }
        return ctx.host->cellValue(view->sheet, view->range.topLeft.row,
                                   view->range.topLeft.col);
    }
    if (const auto *array = std::get_if<ArrayValue>(&arg)) {
        // A computed grid in one cell: a Legacy formula shows its first
        // element (Excel before dynamic arrays); otherwise only a one-cell
        // grid collapses (a Dynamic formula spills, spill.cpp).
        if (array->isSingleCell() || (ctx.mode == ArrayMode::Legacy && !array->cells.isEmpty())) {
            return array->cells.first();
        }
        return Err::Value;
    }
    return std::get<Value>(arg);
}

// An array constant as the evaluator carries arrays.
ArrayValue arrayOf(const ArrayConstant &constant)
{
    ArrayValue array;
    array.rows = constant.rows;
    array.cols = constant.cols;
    array.cells = constant.cells;
    return array;
}

// A function result used as one end of the range operator: INDEX(…):INDEX(…)
// and OFFSET(…):B9 are references (M69).
bool callView(const NodePtr &node, EvalContext &ctx, RangeView *view)
{
    if (node->kind != Node::Kind::Call) {
        return false;
    }
    const Arg result = evalArg(node, ctx);
    const auto *range = std::get_if<RangeView>(&result);
    if (range == nullptr || !range->isValid()) {
        return false;
    }
    *view = *range;
    return true;
}

// A one-cell reference is its value in arithmetic (INDEX(…)*2 is no array).
Arg oneCellAsValue(const Arg &arg, const EvalContext &ctx)
{
    const auto *view = std::get_if<RangeView>(&arg);
    if (view != nullptr && view->isValid() && view->range.cellCount() == 1) {
        return ctx.host->cellValue(view->sheet, view->range.topLeft.row, view->range.topLeft.col);
    }
    return arg;
}

} // namespace

// Builds the argument vector for a call, preserving reference-position args.
QVector<Arg> buildCallArgs(const NodePtr &node, const FunctionSpec &spec, EvalContext &ctx)
{
    QVector<Arg> args;
    args.reserve(node->args.size());
    // An array-typed parameter is worked out as an array even in a Legacy
    // formula, as Excel does (FunctionSpec::arrayParams, M69).
    EvalContext arrays;
    if (ctx.mode == ArrayMode::Legacy && spec.arrayParams != 0) {
        arrays = ctx;
        arrays.mode = ArrayMode::Dynamic;
    }
    for (int i = 0; i < node->args.size(); ++i) {
        const NodePtr &argNode = node->args.at(i);
        EvalContext &use = ctx.mode == ArrayMode::Legacy && spec.takesArrayAt(i) ? arrays : ctx;
        RangeView view;
        if (spec.preserveRefArgs && refView(argNode, use, &view)) {
            args.append(view);
        } else {
            args.append(evalArg(argNode, use));
        }
    }
    return args;
}

// LET(name1, value1, [name2, value2, …], calculation): each value is bound
// under its name for the rest of the call (later values may use earlier
// names); the calculation is evaluated in that scope and its result — a
// range stays a range — is the call's. Bindings never leak out of the call.
Arg evalLet(const NodePtr &node, EvalContext &ctx)
{
    const qsizetype count = node->args.size();
    if (count < 3 || count % 2 == 0) {
        return Err::Value;
    }
    EvalContext scope = ctx;
    for (qsizetype i = 0; i + 1 < count; i += 2) {
        const NodePtr &nameNode = node->args.at(int(i));
        if (!nameNode || nameNode->kind != Node::Kind::Name) {
            return Err::Value;
        }
        scope.locals.insert(nameNode->text.toUpper(), evalArg(node->args.at(int(i) + 1), scope));
    }
    return evalArg(node->args.at(int(count) - 1), scope);
}

bool isLetCall(const NodePtr &node)
{
    return node->kind == Node::Kind::Call && node->text.compare(QLatin1String("LET"), Qt::CaseInsensitive) == 0;
}

// Evaluates a call node, keeping a range result AS a range (argument
// position): SUM(OFFSET(A1,0,0,5,1)) must receive the range, not a scalar.
Arg evalCallArg(const NodePtr &node, EvalContext &ctx)
{
    if (isLetCall(node)) {
        return evalLet(node, ctx);
    }
    const QHash<QString, FunctionSpec> &registry = functionRegistry();
    const auto it = registry.constFind(node->text.toUpper());
    if (it == registry.constEnd()) {
        return Err::Name;
    }
    const FunctionSpec &spec = it.value();
    if (node->args.size() < spec.minArgs || node->args.size() > spec.maxArgs) {
        return Err::Value;
    }
    const QVector<Arg> args = buildCallArgs(node, spec, ctx);
    return callFunction(spec, args, ctx, node); // lifting / intersection (M69)
}

Arg evalArg(const NodePtr &node, EvalContext &ctx)
{
    if (!node) {
        return Err::Value;
    }
    if (node->kind == Node::Kind::StructRef) {
        // Like a reference: one cell reads its value, more stay a range.
        const StructTarget target = structTarget(node, ctx);
        if (!target.ok()) {
            return target.error;
        }
        if (target.range.cellCount() == 1) {
            return ctx.host->cellValue(target.sheet, target.range.topLeft.row, target.range.topLeft.col);
        }
        return RangeView{target.sheet, target.range};
    }
    if (node->kind == Node::Kind::Array) {
        return node->array ? Arg(arrayOf(*node->array)) : Arg(Value(Err::Value));
    }
    if (node->kind == Node::Kind::Ref) {
        const bool single = node->ref.isSingleCell();
        const RangeView view = viewFromRef(node->ref, ctx);
        if (!view.isValid()) {
            return Err::Ref;
        }
        if (single) {
            return ctx.host->cellValue(view.sheet, view.range.topLeft.row, view.range.topLeft.col);
        }
        return view;
    }
    if (node->kind == Node::Kind::Name) {
        // A LET binding first; then a named range, which in argument
        // position carries as a RangeView.
        if (const auto local = ctx.locals.constFind(node->text.toUpper()); local != ctx.locals.constEnd()) {
            return local.value();
        }
        EvalContext inner;
        if (const NodePtr body = nameBody(node, ctx, &inner)) {
            return evalArg(body, inner);
        }
        const auto target = namedTarget(node, ctx);
        if (!target.has_value()) {
            return Err::Name;
        }
        const RangeView view = viewFromRef(*target, ctx);
        if (!view.isValid()) {
            return Err::Ref;
        }
        if (target->isSingleCell()) {
            return ctx.host->cellValue(view.sheet, view.range.topLeft.row,
                                       view.range.topLeft.col);
        }
        return view;
    }
    if (node->kind == Node::Kind::Binary
        && (node->op == Op::Range || node->op == Op::Union)) {
        // ':' and ',' in argument position build/extend ranges. Operands
        // that are references keep their addresses (a single cell becomes a
        // one-cell view); anything else must evaluate cleanly to a value or
        // the range construction fails with #VALUE!.
        RangeView lView;
        RangeView rView;
        const bool lIsView = refView(node->left, ctx, &lView) || callView(node->left, ctx, &lView);
        const bool rIsView = refView(node->right, ctx, &rView) || callView(node->right, ctx, &rView);
        if (lIsView && rIsView) {
            if (lView.sheet != rView.sheet) {
                return Err::Ref; // 3-D refs are #NAME?-class in v1
            }
            lView.range.topLeft.row = qMin(lView.range.topLeft.row, rView.range.topLeft.row);
            lView.range.topLeft.col = qMin(lView.range.topLeft.col, rView.range.topLeft.col);
            lView.range.bottomRight.row =
                qMax(lView.range.bottomRight.row, rView.range.bottomRight.row);
            lView.range.bottomRight.col =
                qMax(lView.range.bottomRight.col, rView.range.bottomRight.col);
            return lView;
        }
        // AGENT-GUARD: never hand this back to evalNode — its range branch
        // evaluates through here (M69), and the two would recurse forever.
        return Err::Value;
    }
    if (node->kind == Node::Kind::Unary) {
        // Array arithmetic (M38): -A1:A10, and the --(A1:A10>5) idiom that
        // coerces a comparison grid to ones and zeroes.
        const Arg operand = oneCellAsValue(evalArg(node->left, ctx), ctx);
        if (isArrayLike(operand)) {
            return broadcastUnary(ctx, node->op, operand);
        }
        return applyUnaryOp(node->op, std::get<Value>(operand));
    }
    if (node->kind == Node::Kind::Binary) {
        // Range and Union were handled above; every other operator evaluates
        // element-wise when either side carries a grid. That is what makes
        // SUMPRODUCT($C$8:$C$19,(1+rate)^$B$8:$B$19) a twelve-cell product
        // instead of #VALUE! — a range in an operand used to be fatal.
        const Arg lhs = oneCellAsValue(evalArg(node->left, ctx), ctx);
        const Arg rhs = oneCellAsValue(evalArg(node->right, ctx), ctx);
        if (isArrayLike(lhs) || isArrayLike(rhs)) {
            return broadcastBinary(ctx, node->op, lhs, rhs);
        }
        return applyBinaryOp(node->op, std::get<Value>(lhs), std::get<Value>(rhs));
    }
    if (node->kind == Node::Kind::Call) {
        return evalCallArg(node, ctx);
    }
    return evalNode(node, ctx);
}

Value evalNode(const NodePtr &node, EvalContext &ctx)
{
    if (!node) {
        return Err::Value;
    }
    switch (node->kind) {
    case Node::Kind::Number:
        return node->number;
    case Node::Kind::Text:
        return node->text;
    case Node::Kind::Bool:
        return node->boolean;
    case Node::Kind::Error:
        return node->error;
    case Node::Kind::Name: {
        if (const auto local = ctx.locals.constFind(node->text.toUpper()); local != ctx.locals.constEnd()) {
            return scalarFromArg(ctx, local.value());
        }
        EvalContext inner;
        if (const NodePtr body = nameBody(node, ctx, &inner)) {
            return evalNode(body, inner);
        }
        // Named ranges resolve through the host (Workbook::namedRange).
        const auto target = namedTarget(node, ctx);
        if (!target.has_value()) {
            return Err::Name;
        }
        const RangeView view = viewFromRef(*target, ctx);
        if (!view.isValid()) {
            return Err::Ref;
        }
        if (target->isSingleCell()) {
            return ctx.host->cellValue(view.sheet, view.range.topLeft.row,
                                       view.range.topLeft.col);
        }
        // A range in a one-value position: Excel's implicit intersection in
        // a Legacy formula (M69, D-078), #VALUE! otherwise.
        return ctx.mode == ArrayMode::Legacy ? implicitIntersection(ctx, view) : Value(Err::Value);
    }
    case Node::Kind::Ref: {
        const RangeView view = viewFromRef(node->ref, ctx);
        if (!view.isValid()) {
            return Err::Ref;
        }
        if (!node->ref.isSingleCell()) {
            return ctx.mode == ArrayMode::Legacy ? implicitIntersection(ctx, view) : Value(Err::Value);
        }
        return ctx.host->cellValue(view.sheet, view.range.topLeft.row, view.range.topLeft.col);
    }
    case Node::Kind::StructRef: {
        const StructTarget target = structTarget(node, ctx);
        if (!target.ok()) {
            return target.error;
        }
        const RangeView view{target.sheet, target.range};
        if (target.range.cellCount() == 1) {
            return ctx.host->cellValue(view.sheet, view.range.topLeft.row, view.range.topLeft.col);
        }
        return ctx.mode == ArrayMode::Legacy ? implicitIntersection(ctx, view) : Value(Err::Value);
    }
    case Node::Kind::Array:
        return node->array ? scalarFromArg(ctx, arrayOf(*node->array)) : Value(Err::Value);
    case Node::Kind::Missing:
        return Value(); // an omitted argument: blank, read as 0 or ""
    case Node::Kind::Unary:
        return applyUnaryOp(node->op, evalNode(node->left, ctx));
    case Node::Kind::Binary: {
        if (node->op == Op::Range || node->op == Op::Union) {
            // A range built in one-value position: intersected in a Legacy
            // formula (M69), #VALUE! otherwise.
            return scalarFromArg(ctx, evalArg(node, ctx));
        }
        const Value lhs = evalNode(node->left, ctx);
        if (isErr(lhs)) return lhs;
        return applyBinaryOp(node->op, lhs, evalNode(node->right, ctx));
    }
    case Node::Kind::Call: {
        if (isLetCall(node)) {
            return scalarFromArg(ctx, evalLet(node, ctx));
        }
        const QHash<QString, FunctionSpec> &registry = functionRegistry();
        const auto it = registry.constFind(node->text.toUpper());
        if (it == registry.constEnd()) {
            return Err::Name;
        }
        const FunctionSpec &spec = it.value();
        if (node->args.size() < spec.minArgs || node->args.size() > spec.maxArgs) {
            return Err::Value;
        }
        const QVector<Arg> args = buildCallArgs(node, spec, ctx);
        return scalarFromArg(ctx, callFunction(spec, args, ctx, node));
    }
    }
    return Err::Value;
}

} // namespace QindaOffice::Calc
