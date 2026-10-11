// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "math_position.h"
#include "math_structures.h"

#include <QObject>

namespace QindaOffice::Math {

// An owned equation draft with atom-based selection and one undo step per
// accepted edit. All calls and signals use the owning thread. Input trees
// and inserted structures are cloned; the caller may release or edit them.
// The returned equation is a read-only view until the next edit. Invalid
// positions, cross-slot selections, and partial edits of a run containing
// preserved non-text XML are refused without changing content or history.
class EquationEditor : public QObject {
    Q_OBJECT
public:
    explicit EquationEditor(QObject *parent = nullptr);
    explicit EquationEditor(const Equation &equation, QObject *parent = nullptr);
    [[nodiscard]] const Equation &equation() const { return m_equation; }
    [[nodiscard]] MathPosition position() const { return m_position; }
    [[nodiscard]] MathPosition anchor() const { return m_anchor; }
    [[nodiscard]] bool hasSelection() const { return m_position != m_anchor; }
    [[nodiscard]] Argument selectedArgument() const;
    [[nodiscard]] bool canUndo() const { return !m_undo.isEmpty(); }
    [[nodiscard]] bool canRedo() const { return !m_redo.isEmpty(); }

    // Load a fresh owned draft and clear history.
    void reset(const Equation &equation);
    // Replace the whole draft as one undoable edit (linear-format conversion).
    void replaceEquation(const Equation &equation);
    bool setPosition(const MathPosition &position, bool extend = false);
    bool insertText(const QString &text);
    bool insertStructure(const Structure &structure);
    bool insertArgument(const Argument &argument);
    bool eraseBackward();
    bool eraseForward();
    bool undo();
    bool redo();
    bool moveHorizontal(bool right, bool extend = false);
    bool moveVertical(bool up, bool extend = false);
    bool nextSlot(bool backwards = false);

signals:
    void changed();
    void positionChanged();
    void historyChanged();

private:
    struct State {
        Equation equation;
        MathPosition position;
        MathPosition anchor;
    };
    Equation m_equation;
    MathPosition m_position;
    MathPosition m_anchor;
    QList<State> m_undo;
    QList<State> m_redo;
    [[nodiscard]] State state() const;
    void restore(const State &state);
    void remember();
    void edited();
    bool replace(const Argument &argument, const Structure *structure = nullptr);
    bool erase(bool backwards);
};

} // namespace QindaOffice::Math
