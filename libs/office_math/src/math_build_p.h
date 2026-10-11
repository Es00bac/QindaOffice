// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to office_math's text parsers (math_linear_parse.cpp,
// math_latex_parse.cpp): an argument under construction as a list of
// ATOMS — characters with their run style, finished structures, and
// invisible groups — with UnicodeMath's notion of the current OPERAND SPAN
// (the atoms since the last operator or space), which is what a build-up
// operator such as / or \u2061 takes as its left operand.

#include "math_model.h"
#include "math_props.h"

#include <QList>

#include <optional>

namespace QindaOffice::Math {

// The m:rPr a parsed character gets.
struct RunStyle {
    MathStyle style = MathStyle::Default;
    MathScript script = MathScript::Roman;
    bool normal = false; // m:nor (quoted text, \text{…})
    friend bool operator==(const RunStyle &, const RunStyle &) = default;
};

struct Atom {
    char32_t ch = 0;      // a character when node and group are empty
    RunStyle style;
    NodePtr node;         // a finished structure (or a whole run)
    bool group = false;   // an invisible group whose content is `inner`
    bool parens = false;  // node is a lone "( … )" a build-up operator may strip
    Argument inner;

    [[nodiscard]] bool isChar() const { return !node && !group; }
};

class AtomList {
public:
    void addChar(char32_t c, const RunStyle &style, bool operand);
    void addNode(NodePtr node, bool parens = false);
    void addGroup(Argument inner);
    void breakSpan() { m_spanStart = m_atoms.size(); }

    [[nodiscard]] bool isEmpty() const { return m_atoms.isEmpty(); }
    [[nodiscard]] bool spanIsEmpty() const { return m_spanStart >= m_atoms.size(); }
    [[nodiscard]] const QList<Atom> &atoms() const { return m_atoms; }
    // True when the last atom is a letter (a function name must start a word).
    [[nodiscard]] bool endsWithLetter() const;
    // The operand span, removed.
    [[nodiscard]] QList<Atom> takeSpan();
    // The last atom (a script's or accent's base), removed.
    [[nodiscard]] std::optional<Atom> takeLast();
    [[nodiscard]] Argument finish() const;

private:
    QList<Atom> m_atoms;
    qsizetype m_spanStart = 0;
};

// Atoms as argument content: groups spliced in, consecutive characters of
// one style merged into runs; with `stripParens`, a lone "( … )" becomes
// its content (UnicodeMath removes the parentheses an operand needed).
[[nodiscard]] Argument toArgument(const QList<Atom> &atoms, bool stripParens);
[[nodiscard]] Argument atomArgument(const Atom &atom, bool stripParens);
[[nodiscard]] NodePtr styledRun(const QString &text, const RunStyle &style);
// A character as typed: a styled code point (𝐱, ℝ) becomes its letter and
// the style that draws it.
[[nodiscard]] RunStyle styleFor(char32_t c, char32_t *plain);
// A node with its arguments filled.
[[nodiscard]] NodePtr structure(NodeKind kind, QList<Argument> args);

[[nodiscard]] inline QString charText(char32_t c)
{
    return QString::fromUcs4(&c, 1);
}

[[nodiscard]] inline Argument singleItem(NodePtr node)
{
    Argument a;
    a.items.append(std::move(node));
    return a;
}

} // namespace QindaOffice::Math
