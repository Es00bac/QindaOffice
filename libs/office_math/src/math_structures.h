// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The structures Word's Equation tab inserts (M92, D-100): Fraction,
// Script, Radical, Integral, Large Operator, Bracket, Function, Accent,
// Limit and Log, Operator, Matrix — each a template with empty slots, as
// OMML Word itself writes for it, ready for EquationEditor::insertStructure.
//
// `wrapSlot` is the argument that receives the selection when a structure
// is inserted over one (selecting x+1 and choosing a fraction makes it the
// numerator; a radical, its radicand), as in Word.

#include "math_model.h"
#include "math_props.h"

#include <QString>

namespace QindaOffice::Math {

struct Structure {
    NodePtr node;
    int wrapSlot = 0;

    [[nodiscard]] static Structure fraction(FractionType type = FractionType::Bar);
    [[nodiscard]] static Structure superscript();
    [[nodiscard]] static Structure subscript();
    [[nodiscard]] static Structure subSuperscript();
    [[nodiscard]] static Structure leftSubSuperscript();
    // √ (degree hidden) or ⁿ√ with a degree slot.
    [[nodiscard]] static Structure radical(bool withDegree = false);
    // ∫ ∬ ∭ ∮ … with or without limits; limits beside (Word's default for
    // integrals) or under and over.
    [[nodiscard]] static Structure integral(char32_t op = U'∫', bool limits = true, bool underOver = false);
    // ∑ ∏ ∐ ⋃ ⋂ ⋁ ⋀ with the limits Word's gallery gives them.
    [[nodiscard]] static Structure largeOperator(char32_t op = U'∑', bool lower = true, bool upper = true,
                                                 bool underOver = true);
    // ( ) [ ] { } ⟨ ⟩ | | ‖ ‖ ⌊ ⌋ ⌈ ⌉, one-sided with an empty string, with
    // `elements` slots split by `separator` (empty: the default │).
    [[nodiscard]] static Structure bracket(const QString &open = QStringLiteral("("),
                                           const QString &close = QStringLiteral(")"), int elements = 1,
                                           const QString &separator = QString());
    // sin, cos, tan, … (upright name) with an argument slot; an empty name
    // gives a name slot too.
    [[nodiscard]] static Structure function(const QString &name = QStringLiteral("sin"));
    [[nodiscard]] static Structure accent(char32_t chr = U'̂');
    [[nodiscard]] static Structure bar(bool top = true);
    [[nodiscard]] static Structure groupCharacter(char32_t chr = U'⏟', bool top = false);
    // lim_(⬚) ⬚, min, max (name with the limit below), or the name with
    // the limit above when `below` is false.
    [[nodiscard]] static Structure limit(const QString &name = QStringLiteral("lim"), bool below = true);
    // log_⬚ ⬚ (with a base slot) or log ⬚, ln ⬚.
    [[nodiscard]] static Structure logarithm(bool withBase = true, const QString &name = QStringLiteral("log"));
    // Operator gallery: a stretched arrow (→ ← ↔ ⇒ …) with text above or
    // below, or an operator with a label over it (≝ = with "def").
    [[nodiscard]] static Structure operatorWithText(char32_t op = U'→', bool textAbove = true);
    [[nodiscard]] static Structure matrix(int rows = 2, int columns = 2);
    [[nodiscard]] static Structure equationArray(int rows = 2);
    [[nodiscard]] static Structure boxed();
    [[nodiscard]] static Structure box();
    [[nodiscard]] static Structure phantom();
};

// Word's navigation order of a node's slots (hidden limits and degrees
// skipped); empty for runs and kept content.
[[nodiscard]] QList<int> navigableSlots(const Node &node);
// The slot above (`up`) or below the slot `arg` of `node`; -1 when none.
[[nodiscard]] int verticalNeighbor(const Node &node, int arg, bool up);

} // namespace QindaOffice::Math
