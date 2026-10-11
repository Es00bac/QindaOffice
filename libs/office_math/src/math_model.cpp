// SPDX-License-Identifier: GPL-3.0-or-later
#include "math_model.h"

#include "ooxml_namespaces.h"

#include <array>

namespace QindaOffice::Math {

namespace {

struct KindRow {
    NodeKind kind;
    QStringView element;
    QStringView props;
    std::array<QStringView, 3> args;
    int arity; // fixed argument count; 0 = variable
};

// shared-math.xsd, EG_OMathMathElements.
constexpr std::array kKinds{
    KindRow{NodeKind::Run, u"r", u"rPr", {}, 0},
    KindRow{NodeKind::Fraction, u"f", u"fPr", {u"num", u"den"}, 2},
    KindRow{NodeKind::Superscript, u"sSup", u"sSupPr", {u"e", u"sup"}, 2},
    KindRow{NodeKind::Subscript, u"sSub", u"sSubPr", {u"e", u"sub"}, 2},
    KindRow{NodeKind::SubSup, u"sSubSup", u"sSubSupPr", {u"e", u"sub", u"sup"}, 3},
    KindRow{NodeKind::PreScript, u"sPre", u"sPrePr", {u"sub", u"sup", u"e"}, 3},
    KindRow{NodeKind::Radical, u"rad", u"radPr", {u"deg", u"e"}, 2},
    KindRow{NodeKind::Nary, u"nary", u"naryPr", {u"sub", u"sup", u"e"}, 3},
    KindRow{NodeKind::Delimiter, u"d", u"dPr", {u"e"}, 0},
    KindRow{NodeKind::Function, u"func", u"funcPr", {u"fName", u"e"}, 2},
    KindRow{NodeKind::Accent, u"acc", u"accPr", {u"e"}, 1},
    KindRow{NodeKind::Bar, u"bar", u"barPr", {u"e"}, 1},
    KindRow{NodeKind::Box, u"box", u"boxPr", {u"e"}, 1},
    KindRow{NodeKind::BorderBox, u"borderBox", u"borderBoxPr", {u"e"}, 1},
    KindRow{NodeKind::EqArray, u"eqArr", u"eqArrPr", {u"e"}, 0},
    KindRow{NodeKind::Matrix, u"m", u"mPr", {u"e"}, 0},
    KindRow{NodeKind::GroupChar, u"groupChr", u"groupChrPr", {u"e"}, 1},
    KindRow{NodeKind::LimLow, u"limLow", u"limLowPr", {u"e", u"lim"}, 2},
    KindRow{NodeKind::LimUpp, u"limUpp", u"limUppPr", {u"e", u"lim"}, 2},
    KindRow{NodeKind::Phantom, u"phant", u"phantPr", {u"e"}, 1},
};

const KindRow *rowFor(NodeKind kind)
{
    for (const KindRow &row : kKinds) {
        if (row.kind == kind) {
            return &row;
        }
    }
    return nullptr;
}

Ooxml::XmlElementPtr cloned(const Ooxml::XmlElementPtr &e)
{
    return e ? e->clone() : nullptr;
}

QList<KeptChild> clonedExtras(const QList<KeptChild> &extras)
{
    QList<KeptChild> out;
    out.reserve(extras.size());
    for (const KeptChild &k : extras) {
        out.append({k.after, cloned(k.element)});
    }
    return out;
}

void appendPlain(const Node &node, QString &out)
{
    switch (node.kind) {
    case NodeKind::Run:
        out += node.text();
        return;
    case NodeKind::Kept:
        if (node.kept) {
            out += node.kept->text();
        }
        return;
    default:
        break;
    }
    for (const Argument &a : node.args) {
        out += plainText(a);
    }
}

} // namespace

Argument Argument::clone() const
{
    Argument out;
    out.items.reserve(items.size());
    for (const NodePtr &n : items) {
        out.items.append(n->clone());
    }
    out.argPr = cloned(argPr);
    out.ctrlPr = cloned(ctrlPr);
    out.attributes = attributes;
    return out;
}

QString Node::text() const
{
    QString out;
    for (const RunPiece &p : pieces) {
        if (!p.element) {
            out += p.text;
        }
    }
    return out;
}

void Node::setText(const QString &text)
{
    QList<RunPiece> others;
    RunPiece first;
    bool haveFirst = false;
    for (const RunPiece &p : std::as_const(pieces)) {
        if (p.element) {
            others.append(p);
        } else if (!haveFirst) {
            first = p;
            haveFirst = true;
        }
    }
    first.text = text;
    first.element = nullptr;
    pieces.clear();
    pieces.append(first);
    pieces.append(others);
}

NodePtr Node::clone() const
{
    auto out = std::make_shared<Node>();
    out->kind = kind;
    out->props = cloned(props);
    out->attributes = attributes;
    out->args.reserve(args.size());
    for (const Argument &a : args) {
        out->args.append(a.clone());
    }
    out->rowSizes = rowSizes;
    out->extras = clonedExtras(extras);
    out->textProps = cloned(textProps);
    out->pieces.reserve(pieces.size());
    for (const RunPiece &p : pieces) {
        out->pieces.append({p.text, p.attributes, cloned(p.element)});
    }
    out->kept = cloned(kept);
    return out;
}

Equation Equation::clone() const
{
    Equation out;
    out.display = display;
    out.paraProps = cloned(paraProps);
    out.attributes = attributes;
    out.lines.reserve(lines.size());
    for (const Argument &a : lines) {
        out.lines.append(a.clone());
    }
    out.extras = clonedExtras(extras);
    return out;
}

bool Equation::isEmpty() const
{
    for (const Argument &line : lines) {
        if (!line.isEmpty()) {
            return false;
        }
    }
    return true;
}

NodePtr makeRun(const QString &text, Ooxml::XmlElementPtr mathProps)
{
    auto run = std::make_shared<Node>();
    run->kind = NodeKind::Run;
    run->props = std::move(mathProps);
    run->pieces.append({text, {}, nullptr});
    return run;
}

NodePtr makeNode(NodeKind kind, int count, int columns)
{
    if (kind == NodeKind::Run) {
        return makeRun(QString());
    }
    auto node = std::make_shared<Node>();
    node->kind = kind;
    const KindRow *row = rowFor(kind);
    if (row == nullptr) {
        return node;
    }
    if (row->arity > 0) {
        node->args.resize(row->arity);
    } else if (kind == NodeKind::Matrix) {
        const int rows = qMax(1, count);
        const int cols = qMax(1, columns);
        node->args.resize(qsizetype(rows) * cols);
        node->rowSizes = QList<int>(rows, cols);
    } else {
        node->args.resize(qMax(1, count));
    }
    return node;
}

Equation makeEquation(bool display)
{
    Equation eq;
    eq.display = display;
    eq.lines.append(Argument());
    return eq;
}

QStringView elementName(NodeKind kind)
{
    const KindRow *row = rowFor(kind);
    return row != nullptr ? row->element : QStringView();
}

QStringView propertyName(NodeKind kind)
{
    const KindRow *row = rowFor(kind);
    return row != nullptr ? row->props : QStringView();
}

QList<QStringView> argumentNames(NodeKind kind)
{
    const KindRow *row = rowFor(kind);
    if (row == nullptr || kind == NodeKind::Run) {
        return {};
    }
    if (row->arity == 0) {
        return {u"e"};
    }
    QList<QStringView> out;
    for (int i = 0; i < row->arity; ++i) {
        out.append(row->args.at(size_t(i)));
    }
    return out;
}

bool hasFixedArity(NodeKind kind)
{
    const KindRow *row = rowFor(kind);
    return row != nullptr && row->arity > 0;
}

std::optional<NodeKind> kindForElement(QStringView localName)
{
    for (const KindRow &row : kKinds) {
        if (row.element == localName) {
            return row.kind;
        }
    }
    return std::nullopt;
}

QString plainText(const Argument &argument)
{
    QString out;
    for (const NodePtr &n : argument.items) {
        appendPlain(*n, out);
    }
    return out;
}

bool isPlainRun(const Node &node)
{
    if (node.kind != NodeKind::Run) {
        return false;
    }
    for (const RunPiece &p : node.pieces) {
        if (p.element) {
            return false;
        }
    }
    return true;
}

} // namespace QindaOffice::Math
