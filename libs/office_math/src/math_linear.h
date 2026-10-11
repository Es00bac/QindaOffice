// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Word's "Linear" equation format (M92, D-100): a UnicodeMath subset
// (Unicode Technical Note #28) with Word's Math AutoCorrect keywords, read
// into the model and written from it. toLinear() is also the equation's
// accessible text.
//
// Read: a/b (¦ no bar, ⊘ linear, ∕ skewed), x^2, x_i, x_i^2, _a^b C
// (prescripts), √x, √(x), √(n&x), ∛x, ∜x; n-ary operators with limits
// (∑_(i=1)^n▒a_i, ∫_0^1, ┬ and ┴ for limits below/above); functions
// (sin\u2061x with U+2061, or a known name followed by a space; lim_(n→∞) and
// lim┬(n→∞) put the limit below); brackets ( ) [ ] { } | | ‖ ‖ ⟨ ⟩ ⌊ ⌋ ⌈ ⌉
// matched freely, ├ ┤ for an invisible side, │ or ∣ separating elements,
// 〖 〗 grouping without brackets; ■(a&b@c&d) matrices, █(…@…) equation
// arrays, Ⓒ cases, ⒨ and ⒩ bracketed matrices; combining accents after
// their base (a \u0302 or \hat), ¯ ▁ bars, □ ▭ boxes, ⏞ ⏟ ⏜ ⏝ ⎴ ⎵ group
// characters (a following ^ or _ becomes the label), ⟡ ⬄ ⇳ ⬍ ⬆ ⬇ ⬌
// phantoms; "quoted text" as normal text; \x quotes x. Parentheses that
// only delimit an operand (of /, ^, _, √, …) are removed, as Word does.
//
// AGENT-NOTE: an operand is UnicodeMath's — the run of letters, digits,
// brackets and structures since the last operator or space; a script's
// base is only the LAST atom (ab^2 raises b), the scripts' own operand is
// such a span. toLinear() writes exactly what this parser reads back to
// the same tree for everything it can express.

#include "math_model.h"

#include <QString>

#include <optional>

namespace QindaOffice::Math {

// One line per m:oMath, joined with '\n'.
[[nodiscard]] QString toLinear(const Equation &equation);
[[nodiscard]] QString toLinear(const Argument &argument);

// Parses linear text into an equation (display or inline); nullopt with
// `error` when the text cannot be built up (an unmatched closing bracket
// is plain text and not an error).
[[nodiscard]] std::optional<Equation> fromLinear(const QString &text, bool display = false,
                                                 QString *error = nullptr);
// The same as argument content, for inserting into an equation.
[[nodiscard]] Argument parseLinear(const QString &text);

} // namespace QindaOffice::Math
