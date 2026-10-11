// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Word's math styles as Unicode's Mathematical Alphanumeric Symbols
// (U+1D400–U+1D7FF and the Letterlike Symbols holes such as ℎ, ℬ, ℭ, ℝ):
// OMML stores plain letters with m:sty/m:scr, the layout draws the styled
// code point from the math font, and the linear formats read them back.
//
// AGENT-NOTE: the default (no m:sty) follows Unicode TR #25 as Word does:
// Latin letters and lowercase Greek are italic, digits, capital Greek and
// everything else upright.

#include "math_props.h"

namespace QindaOffice::Math {

// The styled code point, or `c` itself when the alphabet has no such letter.
[[nodiscard]] char32_t mathAlphanumeric(char32_t c, MathScript script, bool bold, bool italic);

// Whether `c` is italic when the run has no m:sty (TR #25).
[[nodiscard]] bool italicByDefault(char32_t c);

// A styled code point taken apart again (linear input of 𝐱, ℝ, …):
// false when `c` is not a mathematical alphanumeric.
struct PlainLetter {
    char32_t base = 0;
    MathScript script = MathScript::Roman;
    bool bold = false;
    bool italic = false;
};
[[nodiscard]] bool plainLetter(char32_t c, PlainLetter *out);

} // namespace QindaOffice::Math
