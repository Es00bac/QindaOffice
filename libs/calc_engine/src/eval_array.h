// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Array arithmetic (M38, D-038). An operator whose operand is a range or an
// array evaluates element-wise and yields an ArrayValue, so the argument of
// SUMPRODUCT($C$8:$C$19,(1+rate)^$B$8:$B$19) is a twelve-cell grid instead
// of #VALUE!. Arrays live only inside argument evaluation: Value has no
// array case, so cell storage, undo snapshots and recovery are untouched,
// and an array reaching a scalar position is #VALUE! unless it is 1x1 (no
// spilling — that is a separate model, out of scope here).

#include "functions/functions.h"
#include "parser.h"

namespace QindaOffice::Calc {

// True when the argument carries a grid rather than a single value: a range
// still to be read, or an array already materialized.
[[nodiscard]] bool isArrayLike(const Arg &arg);

// Materializes any argument as a rows x cols grid — a scalar becomes 1x1, a
// range is read in grid order. Error CELLS stay in place (Excel keeps them
// per element); the returned Value is an error only when the whole read
// fails: #REF! for an invalid range, #VALUE! for an oversized one.
[[nodiscard]] Value arrayFromArg(const EvalContext &ctx, const Arg &arg, ArrayValue *out);

// Element-wise operators. Shapes broadcast the way Excel does: 1x1 stretches
// to anything and a single row or column stretches along its degenerate
// axis; any other mismatch is #VALUE! (Excel pads with #N/A instead — our
// callers are aggregations, where either way the result is an error).
[[nodiscard]] Arg broadcastBinary(const EvalContext &ctx, Op op, const Arg &lhs, const Arg &rhs);
[[nodiscard]] Arg broadcastUnary(const EvalContext &ctx, Op op, const Arg &operand);

// The scalar operators themselves, shared by scalar and element-wise
// evaluation so the two can never drift apart.
[[nodiscard]] Value applyBinaryOp(Op op, const Value &lhs, const Value &rhs);
[[nodiscard]] Value applyUnaryOp(Op op, const Value &operand);

// Every value an argument carries, whatever shape it arrived in: a range
// reads through the host, an array yields its cells, a scalar yields itself.
// These mirror flatRangeValues/sparseRangeValues — ordered including blanks
// versus unordered non-blank — and share their error contract: the first
// error found is returned instead of the values, whatever shape carried it.
// That is what makes a shape mismatch visible, since broadcasting reports
// one as an ordinary #VALUE! argument.
[[nodiscard]] Value flatArgValues(const EvalContext &ctx, const Arg &arg, QVector<Value> *out);
[[nodiscard]] Value sparseArgValues(const EvalContext &ctx, const Arg &arg, QVector<Value> *out);

} // namespace QindaOffice::Calc
