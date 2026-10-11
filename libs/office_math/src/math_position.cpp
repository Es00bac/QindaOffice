// SPDX-License-Identifier: GPL-3.0-or-later
#include "math_position.h"

namespace QindaOffice::Math {

namespace {

template<typename Eq, typename Arg, typename NodeT>
Arg *walk(Eq &equation, const MathPosition &position, NodeT **owner)
{
    if (owner != nullptr) {
        *owner = nullptr;
    }
    if (position.line < 0 || position.line >= equation.lines.size()) {
        return nullptr;
    }
    Arg *arg = &equation.lines[position.line];
    for (const MathPosition::Step &step : position.steps) {
        if (step.item < 0 || step.item >= arg->items.size()) {
            return nullptr;
        }
        NodeT *node = arg->items[step.item].get();
        if (step.arg < 0 || step.arg >= node->args.size()) {
            return nullptr;
        }
        if (owner != nullptr) {
            *owner = node;
        }
        arg = &node->args[step.arg];
    }
    return arg;
}

} // namespace

const Argument *slotArgument(const Equation &equation, const MathPosition &position)
{
    return walk<const Equation, const Argument, const Node>(equation, position, nullptr);
}

Argument *slotArgument(Equation &equation, const MathPosition &position)
{
    return walk<Equation, Argument, Node>(equation, position, nullptr);
}

const Node *slotOwner(const Equation &equation, const MathPosition &position)
{
    const Node *owner = nullptr;
    walk<const Equation, const Argument, const Node>(equation, position, &owner);
    return owner;
}

Node *slotOwner(Equation &equation, const MathPosition &position)
{
    Node *owner = nullptr;
    walk<Equation, Argument, Node>(equation, position, &owner);
    return owner;
}

int codePointCount(const QString &text)
{
    int n = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (!(text.at(i).isLowSurrogate() && i > 0 && text.at(i - 1).isHighSurrogate())) {
            ++n;
        }
    }
    return n;
}

qsizetype codeUnitIndex(const QString &text, int atom)
{
    int n = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text.at(i).isLowSurrogate() && i > 0 && text.at(i - 1).isHighSurrogate()) {
            continue;
        }
        if (n == atom) {
            return i;
        }
        ++n;
    }
    return text.size();
}

int atomCount(const Node &node)
{
    return node.kind == NodeKind::Run ? codePointCount(node.text()) : 1;
}

int atomCount(const Argument &argument)
{
    int n = 0;
    for (const NodePtr &item : argument.items) {
        n += atomCount(*item);
    }
    return n;
}

AtomRef locateAtom(const Argument &argument, int offset)
{
    int start = 0;
    for (int i = 0; i < argument.items.size(); ++i) {
        const Node &node = *argument.items.at(i);
        const int n = atomCount(node);
        if (offset < start + n) {
            return {i, node.kind == NodeKind::Run ? codeUnitIndex(node.text(), offset - start) : 0};
        }
        start += n;
    }
    return {int(argument.items.size()), 0};
}

int atomOffsetOf(const Argument &argument, int item)
{
    int start = 0;
    for (int i = 0; i < item && i < argument.items.size(); ++i) {
        start += atomCount(*argument.items.at(i));
    }
    return start;
}

} // namespace QindaOffice::Math
