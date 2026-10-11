// SPDX-License-Identifier: GPL-3.0-or-later
#include "math_editor.h"

namespace QindaOffice::Math {
namespace {
// Reading-order caret stops include each structure's visible argument slots.
void collect(const Argument &argument, MathPosition at, QList<MathPosition> &stops, bool slotsOnly)
{
    at.offset = 0;
    stops.append(at);
    for (int i = 0; i < argument.items.size(); ++i) {
        const auto &node = *argument.items.at(i);
        for (const int arg : navigableSlots(node)) {
            MathPosition child = at;
            child.steps.append({i, arg});
            collect(node.args.at(arg), child, stops, slotsOnly);
        }
        const int count = atomCount(node);
        for (int n = 0; n < count; ++n) {
            ++at.offset;
            if (!slotsOnly) {
                stops.append(at);
            }
        }
    }
}
QList<MathPosition> positions(const Equation &equation, bool slotsOnly)
{
    QList<MathPosition> stops;
    for (int i = 0; i < equation.lines.size(); ++i) {
        collect(equation.lines.at(i), {i, {}, 0}, stops, slotsOnly);
    }
    return stops;
}
}

bool EquationEditor::moveHorizontal(bool right, bool extend)
{
    if (extend) {
        auto next = m_position;
        next.offset += right ? 1 : -1;
        return setPosition(next, true);
    }
    if (hasSelection()) {
        auto next = m_position;
        next.offset = right ? qMax(m_position.offset, m_anchor.offset) : qMin(m_position.offset, m_anchor.offset);
        return setPosition(next);
    }
    const auto stops = positions(m_equation, false);
    const qsizetype current = stops.indexOf(m_position);
    const qsizetype next = current + (right ? 1 : -1);
    return current >= 0 && next >= 0 && next < stops.size() && setPosition(stops.at(next));
}

bool EquationEditor::nextSlot(bool backwards)
{
    const auto stops = positions(m_equation, true);
    auto current = m_position;
    current.offset = 0;
    const qsizetype index = stops.indexOf(current);
    const qsizetype next = index + (backwards ? -1 : 1);
    return index >= 0 && next >= 0 && next < stops.size() && setPosition(stops.at(next));
}

bool EquationEditor::moveVertical(bool up, bool extend)
{
    auto next = m_position;
    while (!next.steps.isEmpty()) {
        const Node *owner = slotOwner(m_equation, next);
        if (owner == nullptr) {
            return false;
        }
        const int neighbor = verticalNeighbor(*owner, next.steps.last().arg, up);
        if (neighbor >= 0) {
            next.steps.last().arg = neighbor;
            next.offset = qMin(next.offset, atomCount(owner->args.at(neighbor)));
            return setPosition(next, extend);
        }
        next.steps.removeLast();
    }
    next.line += up ? -1 : 1;
    if (next.line < 0 || next.line >= m_equation.lines.size()) {
        return false;
    }
    next.offset = qMin(next.offset, atomCount(m_equation.lines.at(next.line)));
    return setPosition(next, extend);
}
} // namespace QindaOffice::Math
