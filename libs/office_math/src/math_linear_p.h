// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to the UnicodeMath reader (math_linear_parse.cpp for the
// expression loop, scripts, fractions and brackets; math_linear_structs.cpp
// for n-ary operators, radicals, arrays, prefix structures and functions).

#include "math_build_p.h"

#include <QList>
#include <QString>

#include <string_view>

namespace QindaOffice::Math::detail {

inline constexpr char32_t kApply = 0x2061;   // FUNCTION APPLICATION
inline constexpr char32_t kNaryand = 0x2592; // ▒ (\of, \naryand)
inline constexpr std::u32string_view kArrays = U"■█Ⓒ⒨⒩";
inline constexpr std::u32string_view kPrefixes = U"¯▁□▭⏞⏟⏜⏝⎴⎵⏠⏡⟡⬄⇳⬍⬆⬇⬌";
inline constexpr std::u32string_view kGroupChars = U"⏞⏟⏜⏝⎴⎵⏠⏡";

[[nodiscard]] inline bool isRadicalSign(char32_t c)
{
    return c == U'√' || c == U'∛' || c == U'∜';
}

// The runs of a function's name are upright (m:sty p), as Word writes them.
void markFunctionName(Argument &name);

class LinearParser {
public:
    explicit LinearParser(const QString &source);
    [[nodiscard]] Argument parse();

private:
    QList<char32_t> m_s;
    qsizetype m_i = 0;
    int m_depth = 0; // open brackets and groups
    int m_bars = 0;  // open |…| and ‖…‖

    [[nodiscard]] char32_t peek(qsizetype k = 0) const { return m_i + k < m_s.size() ? m_s.at(m_i + k) : 0; }
    [[nodiscard]] bool atEnd() const { return m_i >= m_s.size(); }
    void skipSpaces();

    AtomList expression(std::u32string_view stops, bool span);
    void step(AtomList &atoms);
    void character(AtomList &atoms, char32_t c);
    void literal(AtomList &atoms);
    Argument operand(bool strip);
    Argument scriptOperand();
    void fraction(AtomList &atoms);
    void readScripts(Argument &sub, Argument &sup, bool &hasSub, bool &hasSup);
    void scripts(AtomList &atoms);
    void limit(AtomList &atoms);
    void apply(AtomList &atoms);
    void accent(AtomList &atoms);
    void primes(AtomList &atoms);
    void group(AtomList &atoms);
    void bracket(AtomList &atoms);
    // math_linear_structs.cpp
    NodePtr nary();
    NodePtr radical();
    NodePtr array();
    NodePtr prefixed();
    bool function(AtomList &atoms);
};

} // namespace QindaOffice::Math::detail
