// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dependencies.h"
#include "functions/functions.h"
#include "parser.h"

namespace QindaOffice::Calc {

// Evaluates a formula AST against an EvalHost. Errors propagate from the
// first failing operand. Ranges carry as RangeView through function
// arguments; a range reaching a scalar position is #VALUE! (plan section
// 7.2: no implicit intersection).

[[nodiscard]] Arg evalArg(const NodePtr &node, EvalContext &ctx);
[[nodiscard]] Value evalNode(const NodePtr &node, EvalContext &ctx);

// Dependency targets referenced by an AST (for the recalc index). Sheet
// names resolve through the host; unresolvable names index nothing (the
// evaluator reports #REF! at run time).
[[nodiscard]] QVector<DepTarget> collectTargets(const NodePtr &node, const EvalContext &ctx);

// True when the AST contains a call to a volatile function.
[[nodiscard]] bool containsVolatileCall(const NodePtr &node);

} // namespace QindaOffice::Calc
