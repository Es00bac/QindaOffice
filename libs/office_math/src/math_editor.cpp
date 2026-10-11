// SPDX-License-Identifier: GPL-3.0-or-later
#include "math_editor.h"

#include "math_props.h"

namespace QindaOffice::Math {
namespace {

bool boundaryEditable(const Argument &slot, int offset)
{
    const AtomRef atom = locateAtom(slot, offset);
    return atom.index == 0 || atom.item == slot.items.size() || isPlainRun(*slot.items.at(atom.item));
}

// Copy an atom range without sharing mutable nodes with undo history.
Argument slice(const Argument &slot, int first, int last)
{
    Argument result;
    int start = 0;
    for (const auto &item : slot.items) {
        const int end = start + atomCount(*item);
        if (start < last && end > first) {
            auto kept = item->clone();
            if (kept->isRun() && (first > start || last < end)) {
                const QString text = kept->text();
                const qsizetype from = codeUnitIndex(text, qMax(0, first - start));
                const qsizetype to = codeUnitIndex(text, qMin(end - start, last - start));
                kept->setText(text.mid(from, to - from));
            }
            result.items.append(kept);
        }
        start = end;
    }
    return result;
}

void joinRuns(Argument &slot)
{
    for (qsizetype i = 1; i < slot.items.size();) {
        Node &before = *slot.items[i - 1];
        const Node &after = *slot.items.at(i);
        if (isPlainRun(before) && isPlainRun(after) && sameRunFormat(before, after)
            && before.attributes.isEmpty() && after.attributes.isEmpty()) {
            before.setText(before.text() + after.text());
            slot.items.removeAt(i);
        } else {
            ++i;
        }
    }
}
}

EquationEditor::EquationEditor(QObject *parent)
    : EquationEditor(makeEquation(false), parent)
{
}

EquationEditor::EquationEditor(const Equation &equation, QObject *parent)
    : QObject(parent), m_equation(equation.clone())
{
    if (m_equation.lines.isEmpty()) {
        m_equation.lines.append(Argument{});
    }
}

void EquationEditor::reset(const Equation &equation)
{
    m_equation = equation.clone();
    if (m_equation.lines.isEmpty()) {
        m_equation.lines.append(Argument{});
    }
    m_position = {};
    m_anchor = {};
    m_undo.clear();
    m_redo.clear();
    edited();
}

void EquationEditor::replaceEquation(const Equation &equation)
{
    remember();
    m_equation = equation.clone();
    if (m_equation.lines.isEmpty()) m_equation.lines.append(Argument{});
    m_position = {};
    m_anchor = {};
    edited();
}

Argument EquationEditor::selectedArgument() const
{
    const Argument *slot = slotArgument(m_equation, m_position);
    return slot && m_position.sameSlot(m_anchor)
        ? slice(*slot, qMin(m_position.offset, m_anchor.offset), qMax(m_position.offset, m_anchor.offset)) : Argument{};
}

bool EquationEditor::setPosition(const MathPosition &position, bool extend)
{
    const Argument *slot = slotArgument(m_equation, position);
    if (slot == nullptr || position.offset < 0 || position.offset > atomCount(*slot)
        || (extend && !position.sameSlot(m_anchor))) {
        return false;
    }
    m_position = position;
    if (!extend) {
        m_anchor = position;
    }
    emit positionChanged();
    return true;
}

EquationEditor::State EquationEditor::state() const
{
    return {m_equation.clone(), m_position, m_anchor};
}

void EquationEditor::remember()
{
    m_undo.append(state());
    m_redo.clear();
}

void EquationEditor::edited()
{
    emit changed();
    emit positionChanged();
    emit historyChanged();
}

void EquationEditor::restore(const State &state)
{
    m_equation = state.equation.clone();
    m_position = state.position;
    m_anchor = state.anchor;
    edited();
}

bool EquationEditor::undo()
{
    if (!canUndo()) {
        return false;
    }
    m_redo.append(state());
    restore(m_undo.takeLast());
    return true;
}

bool EquationEditor::redo()
{
    if (!canRedo()) {
        return false;
    }
    m_undo.append(state());
    restore(m_redo.takeLast());
    return true;
}

bool EquationEditor::replace(const Argument &argument, const Structure *structure)
{
    Argument *slot = slotArgument(m_equation, m_position);
    if (slot == nullptr || !m_position.sameSlot(m_anchor)) {
        return false;
    }
    const int first = qMin(m_position.offset, m_anchor.offset);
    const int last = qMax(m_position.offset, m_anchor.offset);
    const int added = atomCount(argument);
    if (!boundaryEditable(*slot, first) || !boundaryEditable(*slot, last)) {
        return false;
    }
    const AtomRef start = locateAtom(*slot, first);
    if (structure == nullptr && start.item < slot->items.size()) {
        Node &run = *slot->items[start.item];
        const int end = atomOffsetOf(*slot, start.item) + atomCount(run);
        const bool plainInsertion = argument.items.isEmpty()
            || (argument.items.size() == 1 && isPlainRun(*argument.items.first())
                && sameRunFormat(run, *argument.items.first()));
        if (isPlainRun(run) && last <= end && plainInsertion) {
            // Editing within one run keeps its unknown attributes on ONE
            // run, including attributes whose values may be unique IDs.
            const QString previous = run.text();
            const auto through = codeUnitIndex(previous, last - atomOffsetOf(*slot, start.item));
            const QString replacement = previous.left(start.index) + plainText(argument) + previous.mid(through);
            remember();
            if (replacement.isEmpty()) {
                slot->items.removeAt(start.item);
            } else {
                run.setText(replacement);
            }
            m_position.offset = first + added;
            m_anchor = m_position;
            edited();
            return true;
        }
    }
    Argument inserted = argument.clone();
    Argument before = slice(*slot, 0, first);
    Argument after = slice(*slot, last, atomCount(*slot));
    const int itemIndex = int(before.items.size());
    if (structure != nullptr) {
        if (!structure->node || structure->wrapSlot < 0 || structure->wrapSlot >= structure->node->args.size()) {
            return false;
        }
        auto node = structure->node->clone();
        if (last > first) {
            node->args[structure->wrapSlot] = slice(*slot, first, last);
        }
        inserted.items = {node};
    }
    remember();
    // AGENT-GUARD: retain the slot's properties/unknown attributes. Only
    // its edited child sequence changes; the OMML writer preserves the rest.
    slot->items = before.items + inserted.items + after.items;
    m_position.offset = first + atomCount(inserted);
    if (structure != nullptr) {
        m_position.steps.append({itemIndex, structure->wrapSlot});
        m_position.offset = atomCount(inserted.items.first()->args.at(structure->wrapSlot));
    } else {
        joinRuns(*slot);
    }
    m_anchor = m_position;
    edited();
    return true;
}

bool EquationEditor::insertArgument(const Argument &argument)
{
    if (argument.isEmpty() && !hasSelection()) {
        return false;
    }
    return replace(argument);
}

bool EquationEditor::insertText(const QString &text)
{
    if (text.isEmpty()) {
        return false;
    }
    Argument insert;
    NodePtr run = makeRun(text);
    if (const auto *slot = slotArgument(m_equation, m_position); slot && !slot->items.isEmpty()) {
        const auto at = locateAtom(*slot, qMin(m_position.offset, m_anchor.offset));
        const auto &neighbor = slot->items.at(qMin(at.item, int(slot->items.size()) - 1));
        if (isPlainRun(*neighbor)) {
            auto format = neighbor->clone();
            run->props = format->props;
            run->textProps = format->textProps;
        }
    }
    insert.items = {run};
    return replace(insert);
}

bool EquationEditor::insertStructure(const Structure &structure)
{
    return replace({}, &structure);
}

bool EquationEditor::erase(bool backwards)
{
    if (hasSelection()) {
        return replace({});
    }
    const Argument *slot = slotArgument(m_equation, m_position);
    if (slot == nullptr) {
        return false;
    }
    const int offset = m_position.offset + (backwards ? -1 : 1);
    if (offset < 0 || offset > atomCount(*slot)) {
        return false;
    }
    const MathPosition oldAnchor = m_anchor;
    m_anchor.offset = offset;
    const bool accepted = replace({});
    if (!accepted) {
        m_anchor = oldAnchor;
    } else {
        m_undo.last().anchor = oldAnchor;
    }
    return accepted;
}
bool EquationEditor::eraseBackward() { return erase(true); }
bool EquationEditor::eraseForward() { return erase(false); }
} // namespace QindaOffice::Math
