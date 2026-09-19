// SPDX-License-Identifier: GPL-3.0-or-later
#include "eval.h"

#include "eval_array.h"

#include <functional>

namespace QindaOffice::Calc {

namespace {

Value scalarFromArg(const EvalContext &ctx, const Arg &arg)
{
    const auto *view = std::get_if<RangeView>(&arg);
    if (view != nullptr) {
        // A range-valued function result (OFFSET/INDIRECT) in scalar
        // position dereferences to the top-left cell (legacy Excel implicit
        // intersection). The earlier release mapped every such result to
        // #VALUE!, making both functions dead on arrival.
        if (!view->isValid()) {
            return Err::Ref;
        }
        return ctx.host->cellValue(view->sheet, view->range.topLeft.row,
                                   view->range.topLeft.col);
    }
    if (const auto *array = std::get_if<ArrayValue>(&arg)) {
        // A computed grid collapses only when it holds exactly one cell;
        // anything larger would need spilling, which this engine does not do.
        return array->isSingleCell() ? array->cells.first() : Value(Err::Value);
    }
    return std::get<Value>(arg);
}

qint32 resolveSheet(const RangeRef &ref, const EvalContext &ctx, bool *ok)
{
    if (ref.sheet.isEmpty()) {
        *ok = true;
        return ctx.sheet;
    }
    QString name = ref.sheet;
    if (name.startsWith(QLatin1Char('\'')) && name.endsWith(QLatin1Char('\''))) {
        name = name.mid(1, name.size() - 2).replace(QStringLiteral("''"), QStringLiteral("'"));
    }
    const qint32 index = ctx.host->sheetIndex(name);
    *ok = index >= 0;
    return index;
}

RangeView viewFromRef(const RangeRef &ref, const EvalContext &ctx)
{
    bool ok = false;
    const qint32 sheet = resolveSheet(ref, ctx, &ok);
    if (!ok) {
        return {ctx.sheet, {{-1, -1}, {-1, -1}}};
    }
    return {sheet, ref.range};
}

// Resolves a reference node to its view; false for non-reference nodes.
bool refView(const NodePtr &node, const EvalContext &ctx, RangeView *view)
{
    if (node->kind != Node::Kind::Ref) {
        return false;
    }
    *view = viewFromRef(node->ref, ctx);
    return view->isValid();
}

} // namespace

// Builds the argument vector for a call, preserving reference-position args.
QVector<Arg> buildCallArgs(const NodePtr &node, const FunctionSpec &spec, EvalContext &ctx)
{
    QVector<Arg> args;
    args.reserve(node->args.size());
    for (const NodePtr &argNode : node->args) {
        RangeView view;
        if (spec.preserveRefArgs && refView(argNode, ctx, &view)) {
            args.append(view);
        } else {
            args.append(evalArg(argNode, ctx));
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
    return spec.fn(ctx, args);
}

Arg evalArg(const NodePtr &node, EvalContext &ctx)
{
    if (!node) {
        return Err::Value;
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
        const auto target = ctx.host->namedRange(node->text.toUpper());
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
        const bool lIsView = refView(node->left, ctx, &lView);
        const bool rIsView = refView(node->right, ctx, &rView);
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
        if (!lIsView && !rIsView) {
            return evalNode(node, ctx);
        }
        return Err::Value;
    }
    if (node->kind == Node::Kind::Unary) {
        // Array arithmetic (M38): -A1:A10, and the --(A1:A10>5) idiom that
        // coerces a comparison grid to ones and zeroes.
        const Arg operand = evalArg(node->left, ctx);
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
        const Arg lhs = evalArg(node->left, ctx);
        const Arg rhs = evalArg(node->right, ctx);
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
        // Named ranges resolve through the host (Workbook::namedRange).
        const auto target = ctx.host->namedRange(node->text.toUpper());
        if (!target.has_value()) {
            return Err::Name;
        }
        if (target->isSingleCell()) {
            const RangeView view = viewFromRef(*target, ctx);
            if (!view.isValid()) {
                return Err::Ref;
            }
            return ctx.host->cellValue(view.sheet, view.range.topLeft.row,
                                       view.range.topLeft.col);
        }
        return Err::Value; // range in scalar position
    }
    case Node::Kind::Ref: {
        if (!node->ref.isSingleCell()) {
            return Err::Value; // range in scalar position
        }
        const RangeView view = viewFromRef(node->ref, ctx);
        if (!view.isValid()) {
            return Err::Ref;
        }
        return ctx.host->cellValue(view.sheet, view.range.topLeft.row, view.range.topLeft.col);
    }
    case Node::Kind::Unary:
        return applyUnaryOp(node->op, evalNode(node->left, ctx));
    case Node::Kind::Binary: {
        if (node->op == Op::Range || node->op == Op::Union) {
            return Err::Value; // ranges only survive in argument position
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
        return scalarFromArg(ctx, spec.fn(ctx, args));
    }
    }
    return Err::Value;
}

QVector<DepTarget> collectTargets(const NodePtr &node, const EvalContext &ctx)
{
    QVector<DepTarget> out;
    if (!node) {
        return out;
    }
    std::function<void(const NodePtr &)> walk = [&](const NodePtr &n) {
        if (!n) {
            return;
        }
        switch (n->kind) {
        case Node::Kind::Ref: {
            bool ok = false;
            const qint32 sheet = resolveSheet(n->ref, ctx, &ok);
            if (ok) {
                out.append({sheet, n->ref.range});
            }
            break;
        }
        case Node::Kind::Name: {
            // Named ranges index their resolved target so edits inside the
            // range propagate to formulas using the name.
            const auto target = ctx.host->namedRange(n->text.toUpper());
            if (target.has_value()) {
                bool ok = false;
                const qint32 sheet = resolveSheet(*target, ctx, &ok);
                if (ok) {
                    out.append({sheet, target->range});
                }
            }
            break;
        }
        case Node::Kind::Unary:
            walk(n->left);
            break;
        case Node::Kind::Binary:
            walk(n->left);
            walk(n->right);
            break;
        case Node::Kind::Call:
            for (const NodePtr &arg : n->args) {
                walk(arg);
            }
            break;
        default:
            break;
        }
    };
    walk(node);
    return out;
}

bool containsVolatileCall(const NodePtr &node)
{
    if (!node) {
        return false;
    }
    if (node->kind == Node::Kind::Call) {
        const auto it = functionRegistry().constFind(node->text.toUpper());
        if (it != functionRegistry().constEnd() && it->isVolatile) {
            return true;
        }
    }
    if (node->kind == Node::Kind::Unary) {
        return containsVolatileCall(node->left);
    }
    if (node->kind == Node::Kind::Binary) {
        return containsVolatileCall(node->left) || containsVolatileCall(node->right);
    }
    if (node->kind == Node::Kind::Call) {
        for (const NodePtr &arg : node->args) {
            if (containsVolatileCall(arg)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace QindaOffice::Calc
