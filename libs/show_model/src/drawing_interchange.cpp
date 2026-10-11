// SPDX-License-Identifier: GPL-3.0-or-later
#include "drawing_interchange.h"
#include "pptx_read_shapes.h"
#include "ooxml_namespaces.h"

namespace QindaOffice::Show::DrawingIO {
namespace {
using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;
const QString kW = QString::fromUtf16(Ooxml::Ns::W);
const QString kA = QString::fromUtf16(Ooxml::Ns::A);
const QString kP = QString::fromUtf16(Ooxml::Ns::P);
XmlElementPtr make(const QString &ns, const QString &name) { return std::make_shared<XmlElement>(ns, name); }
void texts(const XmlElement &element, QString &text)
{
    if (element.ns == kW && element.name == u"t") { text += element.text(); return; }
    if (element.ns == kW && element.name == u"br") { text += QLatin1Char('\n'); return; }
    for (const auto *child : element.elements()) texts(*child, text);
}
XmlElementPtr textBody(const XmlElement &wsp)
{
    auto body = make(kP, QStringLiteral("txBody"));
    const auto *own = wsp.child(wsp.ns, u"bodyPr");
    auto props = own ? own->clone() : make(kA, QStringLiteral("bodyPr"));
    props->ns = kA;
    body->appendElement(props);
    body->appendElement(make(kA, QStringLiteral("lstStyle")));
    const auto *box = wsp.descendant(kW, u"txbxContent");
    if (!box) return body;
    for (const auto *paragraph : box->elements(kW, u"p")) {
        auto p = make(kA, QStringLiteral("p"));
        if (const auto *pr = paragraph->child(kW, u"pPr")) {
            if (const auto *align = pr->child(kW, u"jc")) {
                auto pPr = make(kA, QStringLiteral("pPr"));
                const QString val = align->attribute(kW, u"val");
                pPr->setAttribute({}, QStringLiteral("algn"), val == u"center" ? QStringLiteral("ctr") : val == u"right" ? QStringLiteral("r") : QStringLiteral("l"));
                p->appendElement(pPr);
            }
        }
        for (const auto *run : paragraph->elements(kW, u"r")) {
            auto r = make(kA, QStringLiteral("r"));
            auto rPr = make(kA, QStringLiteral("rPr"));
            if (const auto *pr = run->child(kW, u"rPr")) {
                if (const auto *sz = pr->child(kW, u"sz"))
                    rPr->setAttribute({}, QStringLiteral("sz"), QString::number(sz->attribute(kW, u"val").toInt() * 50));
                for (const auto *name : {u"b", u"i"}) if (const auto *flag = pr->child(kW, name))
                    rPr->setAttribute({}, QString::fromUtf16(name), flag->attribute(kW, u"val") == u"0" ? QStringLiteral("0") : QStringLiteral("1"));
                if (const auto *color = pr->child(kW, u"color")) {
                    const QString value = color->attribute(kW, u"val");
                    if (value.size() == 6) {
                        auto fill = make(kA, QStringLiteral("solidFill"));
                        auto rgb = make(kA, QStringLiteral("srgbClr"));
                        rgb->setAttribute({}, QStringLiteral("val"), value);
                        fill->appendElement(rgb); rPr->appendElement(fill);
                    }
                }
            }
            r->appendElement(rPr);
            auto t = make(kA, QStringLiteral("t"));
            QString text; texts(*run, text); t->appendText(text);
            r->appendElement(t); p->appendElement(r);
        }
        body->appendElement(p);
    }
    return body;
}
} // namespace
Shape wordShape(const XmlElement &wsp, const std::shared_ptr<const Ooxml::Package> &package,
                const QString &part, const Ooxml::Theme &theme)
{
    auto sp = make(kP, QStringLiteral("sp"));
    for (const auto *name : {u"spPr", u"style"}) if (const auto *child = wsp.child(wsp.ns, name)) {
        auto copy = child->clone(); copy->ns = kP; sp->appendElement(copy);
    }
    sp->appendElement(textBody(wsp));
    Pptx::ThemeContext themeContext; themeContext.theme = theme;
    Pptx::ShapeScope scope; scope.ctx = {package, part, &themeContext, {}, nullptr};
    Pptx::MasterTextStyles styles; scope.styles = &styles;
    scope.keepOrigin = false;
    auto shape = Pptx::readElement(scope, sp, nullptr);
    if (const auto *preset = wsp.descendant(kA, u"prstGeom")) shape.geometry = preset->attr(u"prst");
    return shape;
}
QList<Shape> diagram(const XmlElement &drawing, const std::shared_ptr<const Ooxml::Package> &package,
                    const QString &part, const Ooxml::Theme &theme)
{
    const QString dsp = QStringLiteral("http://schemas.microsoft.com/office/drawing/2008/diagram");
    const XmlElement *tree = drawing.name == u"spTree" ? &drawing : drawing.child(dsp, u"spTree");
    if (!tree) return {};
    Pptx::ThemeContext themeContext; themeContext.theme = theme;
    Pptx::ShapeScope scope; scope.ctx = {package, part, &themeContext, {}, nullptr};
    Pptx::MasterTextStyles styles; scope.styles = &styles;
    scope.keepOrigin = false;
    auto shapes = Pptx::readShapeTree(scope, *tree);
    // DrawingML's presentation point id joins the cached shape to the
    // diagram data model. Retain it without exposing the private parser.
    qsizetype index = 0;
    for (const auto *element : tree->elements()) {
        if (element->name != u"sp" && element->name != u"grpSp" && element->name != u"cxnSp" && element->name != u"pic") continue;
        if (index < shapes.size()) shapes[index++].extra.insert(QStringLiteral("smartArtModelId"), element->attr(u"modelId"));
    }
    return shapes;
}
} // namespace QindaOffice::Show::DrawingIO
