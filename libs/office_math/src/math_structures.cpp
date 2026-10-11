// SPDX-License-Identifier: GPL-3.0-or-later
#include "math_structures.h"

namespace QindaOffice::Math {
namespace {
QString character(char32_t c) { return QString::fromUcs4(&c, 1); }
Structure simple(NodeKind kind, int slot = 0) { return {makeNode(kind), slot}; }
NodePtr nameRun(const QString &name)
{
    auto run = makeRun(name);
    setRunStyle(*run, MathStyle::Plain);
    return run;
}
}

Structure Structure::fraction(FractionType type)
{
    auto result = simple(NodeKind::Fraction);
    setFractionType(*result.node, type);
    return result;
}
Structure Structure::superscript() { return simple(NodeKind::Superscript); }
Structure Structure::subscript() { return simple(NodeKind::Subscript); }
Structure Structure::subSuperscript() { return simple(NodeKind::SubSup); }
Structure Structure::leftSubSuperscript() { return simple(NodeKind::PreScript, 2); }
Structure Structure::radical(bool withDegree)
{
    auto result = simple(NodeKind::Radical, 1);
    setPropOn(*result.node, u"degHide", !withDegree);
    return result;
}
Structure Structure::integral(char32_t op, bool limits, bool underOver)
{
    return largeOperator(op, limits, limits, underOver);
}
Structure Structure::largeOperator(char32_t op, bool lower, bool upper, bool underOver)
{
    auto result = simple(NodeKind::Nary, 2);
    setPropValue(*result.node, u"chr", character(op));
    setPropValue(*result.node, u"limLoc", underOver ? QStringLiteral("undOvr") : QStringLiteral("subSup"));
    setPropOn(*result.node, u"subHide", !lower);
    setPropOn(*result.node, u"supHide", !upper);
    return result;
}
Structure Structure::bracket(const QString &open, const QString &close, int elements, const QString &separator)
{
    Structure result{makeNode(NodeKind::Delimiter, qBound(1, elements, 64)), 0};
    setPropValue(*result.node, u"begChr", open);
    setPropValue(*result.node, u"endChr", close);
    if (!separator.isNull()) {
        setPropValue(*result.node, u"sepChr", separator);
    }
    return result;
}
Structure Structure::function(const QString &name)
{
    auto result = simple(NodeKind::Function, 1);
    if (!name.isEmpty()) {
        result.node->args[0].items.append(nameRun(name));
    }
    return result;
}
Structure Structure::accent(char32_t chr)
{
    auto result = simple(NodeKind::Accent);
    setPropValue(*result.node, u"chr", character(chr));
    return result;
}
Structure Structure::bar(bool top)
{
    auto result = simple(NodeKind::Bar);
    setPropValue(*result.node, u"pos", top ? QStringLiteral("top") : QStringLiteral("bot"));
    return result;
}
Structure Structure::groupCharacter(char32_t chr, bool top)
{
    auto result = simple(NodeKind::GroupChar);
    setPropValue(*result.node, u"chr", character(chr));
    setPropValue(*result.node, u"pos", top ? QStringLiteral("top") : QStringLiteral("bot"));
    setPropValue(*result.node, u"vertJc", top ? QStringLiteral("bot") : QStringLiteral("top"));
    return result;
}
Structure Structure::limit(const QString &name, bool below)
{
    auto result = function(QString());
    auto limits = makeNode(below ? NodeKind::LimLow : NodeKind::LimUpp);
    limits->args[0].items.append(nameRun(name));
    result.node->args[0].items.append(limits);
    return result;
}
Structure Structure::logarithm(bool withBase, const QString &name)
{
    auto result = function(name);
    if (withBase) {
        auto sub = makeNode(NodeKind::Subscript);
        sub->args[0] = result.node->args[0];
        result.node->args[0].items = {sub};
    }
    return result;
}
Structure Structure::operatorWithText(char32_t op, bool textAbove)
{
    auto result = groupCharacter(op, !textAbove);
    return result;
}
Structure Structure::matrix(int rows, int columns)
{
    return {makeNode(NodeKind::Matrix, qBound(1, rows, 64), qBound(1, columns, 64)), 0};
}
Structure Structure::equationArray(int rows) { return {makeNode(NodeKind::EqArray, qBound(1, rows, 64)), 0}; }
Structure Structure::boxed() { return simple(NodeKind::BorderBox); }
Structure Structure::box() { return simple(NodeKind::Box); }
Structure Structure::phantom()
{
    auto result = simple(NodeKind::Phantom);
    setPropOn(*result.node, u"show", false);
    return result;
}

QList<int> navigableSlots(const Node &node)
{
    QList<int> result;
    for (int i = 0; i < node.args.size(); ++i) {
        if (node.kind == NodeKind::Radical && i == 0 && degreeHidden(node)) {
            continue;
        }
        if (node.kind == NodeKind::Nary && ((i == 0 && subHidden(node)) || (i == 1 && supHidden(node)))) {
            continue;
        }
        result.append(i);
    }
    return result;
}

int verticalNeighbor(const Node &node, int arg, bool up)
{
    if (arg < 0 || arg >= node.args.size()) {
        return -1;
    }
    if (node.kind == NodeKind::Matrix) {
        int row = 0;
        int column = arg;
        while (row < node.rowSizes.size() && column >= node.rowSizes.at(row)) {
            column -= node.rowSizes.at(row++);
        }
        const int next = row + (up ? -1 : 1);
        if (next < 0 || next >= node.rowSizes.size() || column >= node.rowSizes.at(next)) {
            return -1;
        }
        int index = column;
        for (int i = 0; i < next; ++i) {
            index += node.rowSizes.at(i);
        }
        return index;
    }
    if (node.kind == NodeKind::EqArray) {
        const int next = arg + (up ? -1 : 1);
        return next >= 0 && next < node.args.size() ? next : -1;
    }
    QList<int> order;
    switch (node.kind) {
    case NodeKind::Fraction: order = {0, 1}; break;
    case NodeKind::Superscript: order = {1, 0}; break;
    case NodeKind::Subscript: order = {0, 1}; break;
    case NodeKind::SubSup: order = {2, 0, 1}; break;
    case NodeKind::PreScript: order = {1, 2, 0}; break;
    case NodeKind::Radical: order = {0, 1}; break;
    case NodeKind::Nary: order = {1, 2, 0}; break;
    case NodeKind::LimLow: order = {0, 1}; break;
    case NodeKind::LimUpp: order = {1, 0}; break;
    default: return -1;
    }
    const auto allowed = navigableSlots(node);
    for (qsizetype i = order.size(); i > 0; --i) {
        if (!allowed.contains(order.at(i - 1))) {
            order.removeAt(i - 1);
        }
    }
    const qsizetype next = order.indexOf(arg) + (up ? -1 : 1);
    return next >= 0 && next < order.size() ? order.at(next) : -1;
}
} // namespace QindaOffice::Math
