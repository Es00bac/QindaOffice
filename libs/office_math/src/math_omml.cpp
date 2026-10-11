// SPDX-License-Identifier: GPL-3.0-or-later
#include "math_omml.h"

#include "ooxml_namespaces.h"

namespace QindaOffice::Math {

using Ooxml::XmlChild;
using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;

namespace {

const QString kM = QString::fromUtf16(Ooxml::Ns::M);
const QString kW = QString::fromUtf16(Ooxml::Ns::W);
const QString kA = QString::fromUtf16(Ooxml::Ns::A);
const QString kXml = QString::fromUtf16(Ooxml::Ns::XML);

NodePtr readNode(const XmlElement &e);

NodePtr keptNode(const XmlElement &e)
{
    auto node = std::make_shared<Node>();
    node->kind = NodeKind::Kept;
    node->kept = e.clone();
    return node;
}

Argument readArgument(const XmlElement &e)
{
    Argument a;
    a.attributes = e.attributes;
    const QList<const XmlElement *> children = e.elements();
    for (qsizetype i = 0; i < children.size(); ++i) {
        const XmlElement &c = *children.at(i);
        if (c.ns == kM && c.name == u"argPr" && i == 0) {
            a.argPr = c.clone();
        } else if (c.ns == kM && c.name == u"ctrlPr" && i == children.size() - 1) {
            a.ctrlPr = c.clone();
        } else {
            a.items.append(readNode(c));
        }
    }
    return a;
}

NodePtr readRun(const XmlElement &e)
{
    auto run = std::make_shared<Node>();
    run->kind = NodeKind::Run;
    run->attributes = e.attributes;
    for (const XmlChild &child : e.children) {
        if (!child.element) {
            continue; // character data outside m:t is not OMML
        }
        const XmlElement &c = *child.element;
        const bool leading = run->pieces.isEmpty();
        if (c.ns == kM && c.name == u"rPr" && leading && !run->props && !run->textProps) {
            run->props = c.clone();
        } else if (c.name == u"rPr" && (c.ns == kW || c.ns == kA) && leading && !run->textProps) {
            run->textProps = c.clone();
        } else if (c.ns == kM && c.name == u"t") {
            run->pieces.append({c.text(), c.attributes, nullptr});
        } else {
            run->pieces.append({QString(), {}, c.clone()});
        }
    }
    return run;
}

// m:m: rows of m:e only; anything else in a row keeps the matrix verbatim.
bool readRow(const XmlElement &row, Node &matrix)
{
    if (!row.attributes.isEmpty()) {
        return false;
    }
    int cells = 0;
    for (const XmlChild &child : row.children) {
        if (!child.element) {
            continue;
        }
        if (!(child.element->ns == kM && child.element->name == u"e")) {
            return false;
        }
        matrix.args.append(readArgument(*child.element));
        ++cells;
    }
    if (cells == 0) {
        return false;
    }
    matrix.rowSizes.append(cells);
    return true;
}

NodePtr readNode(const XmlElement &e)
{
    if (e.ns != kM) {
        return keptNode(e);
    }
    if (e.name == u"r") {
        return readRun(e);
    }
    const std::optional<NodeKind> kind = kindForElement(e.name);
    if (!kind || *kind == NodeKind::Run) {
        return keptNode(e);
    }
    auto node = std::make_shared<Node>();
    node->kind = *kind;
    node->attributes = e.attributes;
    const QStringView props = propertyName(*kind);
    const QList<QStringView> names = argumentNames(*kind);
    const bool fixed = hasFixedArity(*kind);
    const bool matrix = *kind == NodeKind::Matrix;
    for (const XmlChild &child : e.children) {
        if (!child.element) {
            continue;
        }
        const XmlElement &c = *child.element;
        const int position = matrix ? int(node->rowSizes.size()) : int(node->args.size());
        if (c.ns == kM && c.name == props && !node->props && position == 0 && node->extras.isEmpty()) {
            node->props = c.clone();
            continue;
        }
        if (matrix && c.ns == kM && c.name == u"mr") {
            if (!readRow(c, *node)) {
                return keptNode(e);
            }
            continue;
        }
        if (!matrix && c.ns == kM) {
            const QStringView expected = fixed ? (position < names.size() ? names.at(position) : QStringView())
                                               : QStringView(u"e");
            if (!expected.isNull() && c.name == expected) {
                node->args.append(readArgument(c));
                continue;
            }
        }
        node->extras.append({position, c.clone()});
    }
    const bool complete = fixed ? node->args.size() == names.size() : !node->args.isEmpty();
    return complete ? node : keptNode(e);
}

XmlElementPtr writeNode(const Node &node);

XmlElementPtr writeArgument(const Argument &a, const QString &name)
{
    auto e = std::make_shared<XmlElement>(kM, name);
    e->attributes = a.attributes;
    if (a.argPr) {
        e->appendElement(a.argPr->clone());
    }
    for (const NodePtr &item : a.items) {
        e->appendElement(writeNode(*item));
    }
    if (a.ctrlPr) {
        e->appendElement(a.ctrlPr->clone());
    }
    return e;
}

bool hasXmlSpace(const QList<Ooxml::XmlAttribute> &attributes)
{
    for (const Ooxml::XmlAttribute &a : attributes) {
        if (a.ns == kXml && a.name == u"space") {
            return true;
        }
    }
    return false;
}

XmlElementPtr writeRun(const Node &run)
{
    auto e = std::make_shared<XmlElement>(kM, QStringLiteral("r"));
    e->attributes = run.attributes;
    if (run.props) {
        e->appendElement(run.props->clone());
    }
    if (run.textProps) {
        e->appendElement(run.textProps->clone());
    }
    for (const RunPiece &p : run.pieces) {
        if (p.element) {
            e->appendElement(p.element->clone());
            continue;
        }
        XmlElement &t = e->appendElement(kM, QStringLiteral("t"));
        t.attributes = p.attributes;
        const bool edgeSpace = !p.text.isEmpty() && (p.text.front().isSpace() || p.text.back().isSpace());
        if (edgeSpace && !hasXmlSpace(p.attributes)) {
            t.setAttribute(kXml, QStringLiteral("space"), QStringLiteral("preserve"));
        }
        if (!p.text.isEmpty()) {
            t.appendText(p.text);
        }
    }
    return e;
}

void appendExtras(XmlElement &e, const QList<KeptChild> &extras, int after)
{
    for (const KeptChild &k : extras) {
        if (k.after == after && k.element) {
            e.appendElement(k.element->clone());
        }
    }
}

XmlElementPtr writeNode(const Node &node)
{
    if (node.kind == NodeKind::Run) {
        return writeRun(node);
    }
    if (node.kind == NodeKind::Kept) {
        // A Kept node always holds its element; an empty run stands in for a broken one.
        return node.kept ? node.kept->clone() : std::make_shared<XmlElement>(kM, QStringLiteral("r"));
    }
    auto e = std::make_shared<XmlElement>(kM, elementName(node.kind).toString());
    e->attributes = node.attributes;
    if (node.props) {
        e->appendElement(node.props->clone());
    }
    appendExtras(*e, node.extras, 0);
    if (node.kind == NodeKind::Matrix) {
        qsizetype cell = 0;
        for (int r = 0; r < node.rowSizes.size(); ++r) {
            XmlElement &row = e->appendElement(kM, QStringLiteral("mr"));
            for (int c = 0; c < node.rowSizes.at(r) && cell < node.args.size(); ++c) {
                row.appendElement(writeArgument(node.args.at(cell++), QStringLiteral("e")));
            }
            appendExtras(*e, node.extras, r + 1);
        }
        return e;
    }
    const QList<QStringView> names = argumentNames(node.kind);
    const bool fixed = hasFixedArity(node.kind);
    for (int i = 0; i < node.args.size(); ++i) {
        const QStringView name = fixed && i < names.size() ? names.at(i) : QStringView(u"e");
        e->appendElement(writeArgument(node.args.at(i), name.toString()));
        appendExtras(*e, node.extras, i + 1);
    }
    return e;
}

// Wrappers whose content shows (Word draws inserted math, hides deleted).
bool showsContent(const XmlElement &e)
{
    if (e.ns != kW) {
        return false;
    }
    static const QStringList names{QStringLiteral("ins"), QStringLiteral("moveTo"), QStringLiteral("smartTag"),
                                   QStringLiteral("customXml"), QStringLiteral("hyperlink"),
                                   QStringLiteral("fldSimple"), QStringLiteral("sdtContent")};
    return names.contains(e.name);
}

} // namespace

std::optional<Equation> readOmml(const XmlElement &element)
{
    if (element.ns != kM) {
        return std::nullopt;
    }
    if (element.name == u"oMath") {
        Equation eq;
        eq.display = false;
        eq.lines.append(readArgument(element));
        return eq;
    }
    if (element.name != u"oMathPara") {
        return std::nullopt;
    }
    Equation eq;
    eq.display = true;
    eq.attributes = element.attributes;
    for (const XmlChild &child : element.children) {
        if (!child.element) {
            continue;
        }
        const XmlElement &c = *child.element;
        if (c.ns == kM && c.name == u"oMathParaPr" && !eq.paraProps && eq.lines.isEmpty() && eq.extras.isEmpty()) {
            eq.paraProps = c.clone();
        } else if (c.ns == kM && c.name == u"oMath") {
            eq.lines.append(readArgument(c));
        } else {
            eq.extras.append({int(eq.lines.size()), c.clone()});
        }
    }
    return eq;
}

std::optional<Equation> readOmmlFragment(const QString &fragment)
{
    const XmlElementPtr root = Ooxml::parseFragment(fragment);
    return root ? readOmml(*root) : std::nullopt;
}

XmlElementPtr writeOmml(const Equation &equation)
{
    const bool lone = !equation.display && equation.lines.size() == 1 && equation.extras.isEmpty()
        && equation.attributes.isEmpty() && !equation.paraProps;
    if (lone) {
        return writeArgument(equation.lines.first(), QStringLiteral("oMath"));
    }
    auto para = std::make_shared<XmlElement>(kM, QStringLiteral("oMathPara"));
    para->attributes = equation.attributes;
    if (equation.paraProps) {
        para->appendElement(equation.paraProps->clone());
    }
    appendExtras(*para, equation.extras, 0);
    for (int i = 0; i < equation.lines.size(); ++i) {
        para->appendElement(writeArgument(equation.lines.at(i), QStringLiteral("oMath")));
        appendExtras(*para, equation.extras, i + 1);
    }
    return para;
}

QString writeOmmlFragment(const Equation &equation)
{
    return Ooxml::serializeFragment(*writeOmml(equation));
}

std::optional<Argument> keptMath(const Node &kept)
{
    if (kept.kind != NodeKind::Kept || !kept.kept) {
        return std::nullopt;
    }
    const XmlElement &e = *kept.kept;
    if (e.ns == kM) {
        // A math element this model does not know (a later Office's): its text.
        Argument a;
        if (const QString text = e.text(); !text.isEmpty()) {
            a.items.append(makeRun(text));
        }
        return a;
    }
    if (e.ns == kW && (e.name == u"del" || e.name == u"moveFrom")) {
        return Argument();
    }
    if (e.ns == kW && e.name == u"sdt") {
        if (const XmlElement *content = e.child(kW, u"sdtContent")) {
            return readArgument(*content);
        }
        return Argument();
    }
    if (!showsContent(e)) {
        return std::nullopt;
    }
    Argument a;
    for (const XmlElement *c : e.elements()) {
        if (c->ns == kM) {
            a.items.append(readNode(*c));
        } else if (c->ns == kW && c->name == u"r") {
            a.items.append(makeRun(c->text())); // a plain Word run inside math
        }
    }
    return a;
}

} // namespace QindaOffice::Math
