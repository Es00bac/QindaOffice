// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What a math character IS, for the layout's spacing and the linear
// formats' parsing: TeX's atom classes, the n-ary operators, brackets,
// combining accents, function names — and the keyword tables: Word's Math
// AutoCorrect list (\alpha → α, \sqrt → √, \matrix → ■ …, as Microsoft
// documents it for Word's equation editor) and the LaTeX symbol commands.

#include <QString>
#include <QStringView>

namespace QindaOffice::Math {

// TeX's atom classes (The TeXbook, Appendix G); spacing between atoms is
// looked up by class pair.
enum class AtomClass : quint8 { Ord, Op, Bin, Rel, Open, Close, Punct, Inner };

[[nodiscard]] AtomClass atomClassOf(char32_t c);
[[nodiscard]] bool isNaryOperator(char32_t c); // ∑ ∏ ∫ ⋃ …
[[nodiscard]] bool isIntegral(char32_t c);     // ∫ ∬ ∭ ∮ …
[[nodiscard]] bool isOpeningBracket(char32_t c);
[[nodiscard]] bool isClosingBracket(char32_t c);
[[nodiscard]] bool isCombiningMark(char32_t c); // U+0300–036F, U+20D0–20FF
[[nodiscard]] bool isPrime(char32_t c);         // ′ ″ ‴ ⁗ and the apostrophe
// Letters and digits — what UnicodeMath calls an operand character.
[[nodiscard]] bool isAlphanumeric(char32_t c);
// The character a math run DRAWS for a stored one (hyphen-minus → minus
// sign, apostrophe → prime, …).
[[nodiscard]] char32_t displayChar(char32_t c);

// Upright operator names Word recognises (sin, cos, log, lim, …).
[[nodiscard]] bool isFunctionName(QStringView name);
// Of those, the ones whose subscript goes BELOW (lim, max, min, sup, inf, …).
[[nodiscard]] bool isLimitFunction(QStringView name);
// The longest function name starting at `text`, or 0.
[[nodiscard]] qsizetype functionNameAt(QStringView text);

// Word's Math AutoCorrect: "alpha" (no backslash) → "α"; empty when unknown.
// Includes the structure keywords (\sqrt → √, \matrix → ■, \of → ▒ …) and
// the styled alphabets (\doubleR → ℝ, \frakturg, \scriptL).
[[nodiscard]] QString wordAutoCorrect(QStringView keyword);
// A LaTeX symbol command ("alpha", "le", "to", "infty") → its character(s);
// empty when the command is not a plain symbol.
[[nodiscard]] QString latexSymbol(QStringView command);
// The LaTeX command for a character ("\\alpha"), or empty when the
// character is written as itself.
[[nodiscard]] QString latexCommandFor(char32_t c);

// Replaces every \keyword of Word's Math AutoCorrect in `text` (a keyword
// ends at the first non-letter; a following space is consumed, as Word
// does when the keyword is typed).
[[nodiscard]] QString applyAutoCorrect(const QString &text);

} // namespace QindaOffice::Math
