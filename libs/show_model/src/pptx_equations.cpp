// SPDX-License-Identifier: GPL-3.0-or-later
#include "pptx_equations.h"
#include "math_omml.h"
#include "ooxml_namespaces.h"
#include "pptx_read_shapes.h"
#include <functional>
namespace QindaOffice::Show::Pptx {
namespace {
using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;
const QString kM = QString::fromUtf16(Ooxml::Ns::M);
const QString kA14 = QString::fromUtf16(Ooxml::Ns::A14);
XmlElement *find(XmlElement &element, QStringView ns, QStringView name)
{
    if (element.is(ns, name)) return &element;
    for (auto &child : element.children) if (child.element) {
        if (auto *found = find(*child.element, ns, name)) return found;
    }
    return nullptr;
}
void set(XmlElement &element, const QString &name, const QString &value) { element.setAttribute({}, name, value); }
XmlElementPtr nativeShape(PartWriter &out, const Shape &shape, quint32 id)
{
    Shape text = shape; text.kind = ShapeKind::TextBox; text.paragraphs.clear(); text.framed = false;
    auto element = generateShape(out, text, id);
    element->removeChildren(nsP(), u"txBody");
    auto &body = element->appendElement(nsP(), QStringLiteral("txBody"));
    auto &props = body.appendElement(nsA(), QStringLiteral("bodyPr"));
    for (const auto *key : {"lIns", "rIns", "tIns", "bIns"}) set(props, QLatin1String(key), QStringLiteral("0"));
    set(props, QStringLiteral("anchor"), QStringLiteral("ctr"));
    body.appendElement(nsA(), QStringLiteral("lstStyle"));
    auto &paragraph = body.appendElement(nsA(), QStringLiteral("p"));
    auto &ppr = paragraph.appendElement(nsA(), QStringLiteral("pPr"));
    set(ppr, QStringLiteral("algn"), QStringLiteral("ctr"));
    set(ppr.appendElement(nsA(), QStringLiteral("defRPr")), QStringLiteral("sz"), QString::number(qRound(shape.equationSize * 100)));
    auto &math = paragraph.appendElement(kA14, QStringLiteral("m"));
    if (const auto xml = Ooxml::parseFragment(shape.equationXml)) math.appendElement(xml);
    return element;
}
}
bool readEquation(Shape &shape, const XmlElement &element)
{
    const auto *choice = element.child(nsMc(), u"Choice");
    const auto &native = choice ? *choice : element;
    const auto *math = native.descendant(kA14, u"m");
    if (!math) return false;
    // Never replace a compound text box with just its first math fragment.
    QList<const XmlElement *> zones;
    std::function<void(const XmlElement &)> collect = [&](const XmlElement &node) {
        if (node.is(kA14, u"m")) { zones.append(&node); return; }
        if (node.is(nsA(), u"t") && !node.text().isEmpty()) { zones.append(nullptr); return; }
        for (const auto *child : node.elements()) collect(*child);
    };
    collect(native);
    if (zones.size() != 1 || zones.first() == nullptr) return false;
    const auto *omml = math->child(kM, u"oMathPara");
    if (!omml) omml = math->child(kM, u"oMath");
    if (!omml || !Math::readOmml(*omml)) return false;
    shape.kind = ShapeKind::Equation;
    shape.equationXml = Ooxml::serializeFragment(*omml);
    if (const auto *sp = native.descendant(nsP(), u"sp")) {
        if (const auto *sppr = sp->child(nsP(), u"spPr")) readTransform(sppr->child(nsA(), u"xfrm"), shape);
    }
    const auto *size = native.descendant(nsA(), u"defRPr");
    if (!size) size = native.descendant(nsA(), u"rPr");
    if (size && size->attr(u"sz").toDouble() > 0) shape.equationSize = size->attr(u"sz").toDouble() / 100;
    shape.children.clear(); shape.imagePng.clear();
    return true;
}
XmlElementPtr generateEquation(PartWriter &out, const Shape &shape, quint32 id)
{
    auto alternate = std::make_shared<XmlElement>(nsMc(), QStringLiteral("AlternateContent"));
    auto &choice = alternate->appendElement(nsMc(), QStringLiteral("Choice"));
    set(choice, QStringLiteral("Requires"), QStringLiteral("{%1}").arg(kA14));
    choice.appendElement(nativeShape(out, shape, id));
    if (auto picture = pictureFallback(out, shape, id))
        alternate->appendElement(nsMc(), QStringLiteral("Fallback")).appendElement(picture);
    return alternate;
}
void patchEquation(PartWriter &out, XmlElement &element, const Shape &shape, quint32 id)
{
    auto *choice = element.child(nsMc(), u"Choice");
    auto *math = find(choice ? *choice : element, kA14, u"m");
    const auto replacement = Ooxml::parseFragment(shape.equationXml);
    if (!math || !replacement) return;
    math->removeChildren(kM, u"oMath"); math->removeChildren(kM, u"oMathPara");
    math->appendElement(replacement);
    if (element.is(nsMc(), u"AlternateContent")) {
        element.removeChildren(nsMc(), u"Fallback");
        if (auto picture = pictureFallback(out, shape, id))
            element.appendElement(nsMc(), QStringLiteral("Fallback")).appendElement(picture);
    }
}
}
