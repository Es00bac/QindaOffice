// SPDX-License-Identifier: GPL-3.0-or-later
// Reference resolution for the evaluator and the dependency walk — see
// eval_refs.h. collectTargets/containsVolatileCall are declared in eval.h.
#include "eval_refs.h"

#include <functional>

namespace QindaOffice::Calc {

namespace EvalRefs {

// A structured reference resolved for the formula's cell (M69, D-078).
StructTarget structTarget(const NodePtr &node, const EvalContext &ctx)
{
    if (!node->structRef || ctx.host == nullptr) {
        return {};
    }
    return resolveStructRef(*node->structRef, *ctx.host, ctx.sheet, ctx.here);
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
    if (node->kind == Node::Kind::StructRef) {
        const StructTarget target = structTarget(node, ctx);
        *view = {target.sheet, target.range};
        return target.ok();
    }
    if (node->kind != Node::Kind::Ref) {
        return false;
    }
    *view = viewFromRef(node->ref, ctx);
    return view->isValid();
}

// The body `node` (a Name) stands for on the evaluating sheet, with the
// context it must be evaluated in: the caller's position, none of the
// caller's LET bindings (a name is workbook state, not formula state).
NodePtr nameBody(const NodePtr &node, const EvalContext &ctx, EvalContext *inner)
{
    NodePtr body = ctx.host->nameFormula(node->text, ctx.sheet);
    if (!body) {
        return body;
    }
    *inner = ctx;
    inner->locals.clear();
    if (++inner->nameDepth > kMaxNameDepth) {
        return Node::makeError(Err::Name);
    }
    // Relative references in a name are relative to the cell using it:
    // Excel stores them as if used in A1 (M69, D-078; D-069 had kept them
    // anchored at A1).
    return shiftNodeWrapped(body, ctx.here.row, ctx.here.col);
}

// A workbook range name's target for the cell using it (relative axes
// follow that cell, as in a formula name). A table's name alone means its
// data rows, as in Excel's SUM(Table1) (M69).
std::optional<RangeRef> namedTarget(const NodePtr &node, const EvalContext &ctx)
{
    const auto target = ctx.host->namedRange(node->text.toUpper());
    if (!target.has_value()) {
        qint32 sheet = -1;
        const TableSpec *table = ctx.host->tableNamed(node->text, &sheet);
        const CellRange data = table != nullptr ? tableDataRows(*table) : CellRange{};
        if (!data.isValid()) {
            return std::nullopt;
        }
        RangeRef ref;
        ref.sheet = ctx.host->sheetName(sheet);
        ref.range = data;
        ref.absRow0 = ref.absCol0 = ref.absRow1 = ref.absCol1 = true;
        return ref;
    }
    return shiftRefWrapped(*target, ctx.here.row, ctx.here.col);
}

} // namespace EvalRefs

using namespace EvalRefs;

QVector<DepTarget> collectTargets(const NodePtr &node, const EvalContext &ctx)
{
    QVector<DepTarget> out;
    if (!node) {
        return out;
    }
    int nameDepth = 0;
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
            // A name with a formula or constant body depends on whatever
            // its body references (D-069).
            if (const NodePtr body = ctx.host->nameFormula(n->text, ctx.sheet)) {
                if (nameDepth < kMaxNameDepth) {
                    ++nameDepth;
                    walk(shiftNodeWrapped(body, ctx.here.row, ctx.here.col));
                    --nameDepth;
                }
                break;
            }
            // Named ranges index their resolved target so edits inside the
            // range propagate to formulas using the name.
            const auto target = namedTarget(n, ctx);
            if (target.has_value()) {
                bool ok = false;
                const qint32 sheet = resolveSheet(*target, ctx, &ok);
                if (ok) {
                    out.append({sheet, target->range});
                }
            }
            break;
        }
        case Node::Kind::StructRef: {
            // The cells the reference reaches now; a table that grows or
            // loses a column is followed by a reindex (table_edit.h).
            if (n->structRef && ctx.host != nullptr) {
                const StructTarget target = resolveStructRef(*n->structRef, *ctx.host, ctx.sheet, ctx.here);
                if (target.ok()) {
                    out.append({target.sheet, target.range});
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
