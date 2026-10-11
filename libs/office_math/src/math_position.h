// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Positions inside an equation: a SLOT (one argument — a line, a numerator,
// a matrix cell, …) named by its path from the line, and an OFFSET in that
// slot counted in ATOMS: a run contributes one atom per code point, every
// other node exactly one. The caret, the selection, the layout's caret
// geometry and hit testing all speak this.
//
// AGENT-CONTRACT (math_editor ⇄ math_layout): a position is valid for the
// equation it was made for; after an edit the editor hands out new ones.
// EquationLayout::caretRect()/positionAt() resolve them against the tree
// the layout was built from.

#include "math_model.h"

#include <QList>

namespace QindaOffice::Math {

struct MathPosition {
    struct Step {
        int item = 0; // the node's index in its argument
        int arg = 0;  // the argument's index in that node
        friend bool operator==(const Step &, const Step &) = default;
    };

    int line = 0;
    QList<Step> steps;
    int offset = 0;

    [[nodiscard]] bool sameSlot(const MathPosition &other) const
    {
        return line == other.line && steps == other.steps;
    }
    friend bool operator==(const MathPosition &, const MathPosition &) = default;
};

// The argument a position's slot names; nullptr for an invalid path.
[[nodiscard]] const Argument *slotArgument(const Equation &equation, const MathPosition &position);
[[nodiscard]] Argument *slotArgument(Equation &equation, const MathPosition &position);
// The node owning the slot (nullptr for a line).
[[nodiscard]] const Node *slotOwner(const Equation &equation, const MathPosition &position);
[[nodiscard]] Node *slotOwner(Equation &equation, const MathPosition &position);

[[nodiscard]] int atomCount(const Node &node);
[[nodiscard]] int atomCount(const Argument &argument);

// Where atom `offset` lives: the item, and for a run the UTF-16 index of
// the atom's code point. Past the end: item == items.size().
struct AtomRef {
    int item = 0;
    qsizetype index = 0;
};
[[nodiscard]] AtomRef locateAtom(const Argument &argument, int offset);
// The atom offset at which item `item` begins.
[[nodiscard]] int atomOffsetOf(const Argument &argument, int item);
// UTF-16 index of the `atom`-th code point of `text` (text.size() past the end).
[[nodiscard]] qsizetype codeUnitIndex(const QString &text, int atom);
[[nodiscard]] int codePointCount(const QString &text);

} // namespace QindaOffice::Math
