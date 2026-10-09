// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// How the evaluator finds what a formula refers to: sheets by name, defined
// names (a formula body, or a range whose relative axes follow the cell
// using the name — M69, D-078), tables by name and structured references.
// Split from eval.cpp (M69) so the evaluator and the dependency walk
// (collectTargets) resolve references through one set of rules.
// Internal to calc_engine: eval.h is the public boundary. Every function
// reads the host through `ctx` and owns nothing.

#include "eval.h"
#include "structured_ref.h"

#include <optional>

namespace QindaOffice::Calc::EvalRefs {

// Defined names bound to a formula, a constant or one sheet (D-069) expand
// to their body; a name that reaches itself again stops at this depth as
// #NAME? (the dependency walk stops there too).
constexpr int kMaxNameDepth = 32;

// The sheet a reference names (quoted names unquoted; none = the formula's
// own sheet); *ok false when no sheet has that name.
[[nodiscard]] qint32 resolveSheet(const RangeRef &ref, const EvalContext &ctx, bool *ok);

// A reference as a range of a resolved sheet (invalid when the sheet is gone).
[[nodiscard]] RangeView viewFromRef(const RangeRef &ref, const EvalContext &ctx);

// A structured reference node resolved for the formula's cell.
[[nodiscard]] StructTarget structTarget(const NodePtr &node, const EvalContext &ctx);

// A Ref or StructRef node's range; false for every other node kind.
bool refView(const NodePtr &node, const EvalContext &ctx, RangeView *view);

// The body a formula/constant name stands for at the evaluating cell, and
// in *inner the context to evaluate it in (no LET bindings of the caller);
// null when the name has no formula body.
[[nodiscard]] NodePtr nameBody(const NodePtr &node, const EvalContext &ctx, EvalContext *inner);

// A range name's target for the cell using it, or a table's data rows for
// the table's bare name; nullopt for neither.
[[nodiscard]] std::optional<RangeRef> namedTarget(const NodePtr &node, const EvalContext &ctx);

} // namespace QindaOffice::Calc::EvalRefs
