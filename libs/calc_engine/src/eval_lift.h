// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Lifting and implicit intersection (M69, D-078): what a function does when
// an argument it wants as ONE value holds several — LEN(A1:A3),
// IF(A1:A3>1,"y","n"), VLOOKUP(A2:A9,…).
//
// AGENT-NOTE: Excel decides by the formula, not the function. A
// dynamic-array formula (and a Ctrl+Shift+Enter array formula) applies the
// function to each element and returns the array of results — "lifting";
// LEN(A1:A3) spills three lengths. A formula from a file Excel stored
// without the dynamic-array mark keeps pre-2019 semantics: the range is
// cut down to the cell in the formula's own row or column — "implicit
// intersection", shown as '@' by Excel 365 (=LEN(@A1:A3)).
//
// Which arguments a function wants as one value is not declared anywhere:
// the engine watches. Every call runs inside a CallFrame; scalarOrValue
// (functions.cpp), asked for one value from an argument of that call that
// holds several, records it and answers the first cell. When the call
// returns, callFunction lifts or intersects exactly the recorded arguments
// and calls again. An argument the first call never reached (an IF branch
// it did not take) is caught by the next call down — lifting recurses.
//
// AGENT-GUARD: a function reading an argument as one value must go through
// scalarOrValue/scalarNumber with the reference into its `args` vector; a
// copy of the Arg is not in the frame and is never lifted (it then keeps
// the pre-M69 #VALUE!).

#include "functions/functions.h"
#include "parser.h"

namespace QindaOffice::Calc {

// The function call being made: the argument vector the function was
// handed, the arguments it asked for as one value while they held several
// (bit i = argument i, for the first 64), and the context asking.
struct CallFrame {
    const Arg *begin = nullptr;
    const Arg *end = nullptr;
    quint64 scalarMask = 0;
    const EvalContext *ctx = nullptr;
};

// The innermost call being made on this thread, or nullptr.
[[nodiscard]] CallFrame *currentCallFrame();

// Calls `spec` with `args` the way Excel would for ctx.mode (see above).
// `node` is the call itself: Legacy mode re-evaluates its argument trees
// with intersection. `lifted` (optional) is set when the result is an
// array built by lifting.
[[nodiscard]] Arg callFunction(const FunctionSpec &spec, const QVector<Arg> &args, EvalContext &ctx,
                               const NodePtr &node, bool *lifted = nullptr);

// Excel's implicit intersection with the formula's cell: a one-column range
// gives the cell in the formula's row, a one-row range the cell in its
// column (on the range's own sheet), anything else #VALUE! unless the
// formula's cell lies inside it; an array gives its first element, a value
// itself. Also the '@' operator (SINGLE).
[[nodiscard]] Value implicitIntersection(const EvalContext &ctx, const Arg &arg);

// True when the argument holds more than one cell (a range or an array).
[[nodiscard]] bool holdsSeveralCells(const Arg &arg);

} // namespace QindaOffice::Calc
