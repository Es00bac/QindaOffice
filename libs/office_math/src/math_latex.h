// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// LaTeX in and out (M92, D-100): the subset Word's equation editor accepts
// in its LaTeX mode — \frac \dfrac \tfrac \binom \genfrac \sfrac, ^{} _{},
// \sqrt[n]{}, \sum \prod \int \oint … with _{}^{} and \limits/\nolimits,
// \lim \max \sin \log … and \operatorname{}, \left( … \middle| … \right),
// \begin{matrix pmatrix bmatrix Bmatrix vmatrix Vmatrix cases aligned
// align gathered array}, accents (\hat \bar \vec \dot \ddot \tilde …),
// \overline \underline \overbrace \underbrace \overset \underset \boxed
// \phantom \smash, \text \mathrm \mathbf \mathit \mathbb \mathcal
// \mathfrak \mathsf \mathtt \boldsymbol, spacing (\, \: \; \quad \qquad),
// Greek letters and the symbol commands. Plain ( ) [ ] | | brackets build
// up into growing delimiters, as Word does.
//
// toLatex() writes the forms fromLatex() reads back to the same tree for
// everything LaTeX can express (m:lin, m:box and a groupChr's character
// other than a brace are written approximately).

#include "math_model.h"

#include <QString>

#include <optional>

namespace QindaOffice::Math {

[[nodiscard]] std::optional<Equation> fromLatex(const QString &latex, bool display = false, QString *error = nullptr);
[[nodiscard]] Argument parseLatex(const QString &latex);
[[nodiscard]] QString toLatex(const Equation &equation);
[[nodiscard]] QString toLatex(const Argument &argument);

} // namespace QindaOffice::Math
