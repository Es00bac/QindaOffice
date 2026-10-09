// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dependencies.h"
#include "functions/functions.h"
#include "parser.h"

namespace QindaOffice::Calc {

// Evaluates a formula AST against an EvalHost. Errors propagate from the
// first failing operand. Ranges carry as RangeView through function
// arguments. A range or array reaching a one-value position follows the
// formula's ArrayMode (M69, D-078): a Legacy formula — one Excel wrote
// before dynamic arrays — intersects a range with its own row or column
// and takes an array's first element; a Dynamic formula lifts the call over
// the array (eval_lift.h). Reference resolution lives in eval_refs.cpp.

[[nodiscard]] Arg evalArg(const NodePtr &node, EvalContext &ctx);
[[nodiscard]] Value evalNode(const NodePtr &node, EvalContext &ctx);

// Dependency targets referenced by an AST (for the recalc index). Sheet
// names resolve through the host; unresolvable names index nothing (the
// evaluator reports #REF! at run time).
[[nodiscard]] QVector<DepTarget> collectTargets(const NodePtr &node, const EvalContext &ctx);

// True when the AST contains a call to a volatile function.
[[nodiscard]] bool containsVolatileCall(const NodePtr &node);

} // namespace QindaOffice::Calc
