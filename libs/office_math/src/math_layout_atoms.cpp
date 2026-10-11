// SPDX-License-Identifier: GPL-3.0-or-later
// What the layout reads from a run's formatting and a node's class: the
// colour and size a Word or DrawingML run asks for, a structure's control
// colour, and the TeX atom class a node presents to its neighbours. Split
// from math_layout.cpp.
#include "math_layout_p.h"

#include "ooxml_namespaces.h"

namespace QindaOffice::Math {

using Ooxml::XmlElement;

namespace {

const QString kM = QString::fromUtf16(Ooxml::Ns::M);
const QString kW = QString::fromUtf16(Ooxml::Ns::W);
const QString kA = QString::fromUtf16(Ooxml::Ns::A);

bool onOff(const XmlElement *e)
{
    if (e == nullptr) {
        return false;
    }
    const QString v = e->attr(u"val");
    return v.isEmpty() || !(v == u"0" || v == u"false" || v == u"off");
}

QColor hexColor(const QString &v)
{
    if (v.size() == 6) {
        const QColor c(QLatin1Char('#') + v);
        if (c.isValid()) {
            return c;
        }
    }
    return {};
}

// A Word (w:rPr) or DrawingML (a:rPr) run's colour; invalid when none.
QColor colorOf(const XmlElement &rPr)
{
    if (rPr.ns == kW) {
        if (const XmlElement *c = rPr.child(kW, u"color")) {
            return hexColor(c->attr(u"val"));
        }
    } else if (rPr.ns == kA) {
        if (const XmlElement *fill = rPr.child(kA, u"solidFill")) {
            if (const XmlElement *rgb = fill->child(kA, u"srgbClr")) {
                return hexColor(rgb->attr(u"val"));
            }
        }
    }
    return {};
}

char32_t firstChar(const QString &text, bool last)
{
    const QList<uint> chars = text.toUcs4();
    if (chars.isEmpty()) {
        return 0;
    }
    return last ? chars.last() : chars.first();
}

AtomClass limitClass(const Node &n)
{
    if (n.args.isEmpty() || n.args.first().items.size() != 1) {
        return AtomClass::Ord;
    }
    const Node &base = *n.args.first().items.first();
    if (base.kind == NodeKind::Run) {
        const QString text = base.text();
        if (codePointCount(text) == 1) {
            return atomClassOf(displayChar(firstChar(text, false)));
        }
        return text.isEmpty() ? AtomClass::Ord : AtomClass::Op; // lim, max, …
    }
    if (base.kind == NodeKind::GroupChar) {
        return atomClassOf(firstChar(groupChar(base), false)) == AtomClass::Rel ? AtomClass::Rel : AtomClass::Ord;
    }
    return AtomClass::Ord;
}

} // namespace

RunFormat Layouter::runFormat(const Node &run) const
{
    RunFormat f;
    f.color = m_options.color;
    f.style = runStyle(run);
    f.script = runScript(run);
    f.normalText = runIsNormalText(run);
    f.textFamily = m_options.textFont;
    f.bold = f.style == MathStyle::Bold || f.style == MathStyle::BoldItalic;
    f.italic = f.style == MathStyle::Italic || f.style == MathStyle::BoldItalic;
    if (!run.textProps) {
        return f;
    }
    const XmlElement &rp = *run.textProps;
    if (const QColor c = colorOf(rp); c.isValid()) {
        f.color = c;
    }
    QString family;
    qreal sizePt = 0;
    if (rp.ns == kW) {
        if (const XmlElement *sz = rp.child(kW, u"sz")) {
            sizePt = sz->attr(u"val").toDouble() / 2.0;
        }
        if (const XmlElement *fonts = rp.child(kW, u"rFonts")) {
            family = fonts->attr(u"ascii", fonts->attr(u"hAnsi"));
        }
        if (f.normalText && rp.child(kW, u"b") != nullptr) {
            f.bold = onOff(rp.child(kW, u"b"));
        }
        if (f.normalText && rp.child(kW, u"i") != nullptr) {
            f.italic = onOff(rp.child(kW, u"i"));
        }
    } else if (rp.ns == kA) {
        sizePt = rp.attr(u"sz").toDouble() / 100.0;
        if (const XmlElement *latin = rp.child(kA, u"latin")) {
            family = latin->attr(u"typeface");
        }
        if (f.normalText) {
            f.bold = rp.attr(u"b") == u"1";
            f.italic = rp.attr(u"i") == u"1";
        }
    }
    if (sizePt > 0 && m_options.fontSizePt > 0) {
        f.sizeFactor = sizePt / m_options.fontSizePt;
    }
    if (!family.isEmpty() && !family.contains(u"Math")) {
        f.textFamily = family;
    }
    return f;
}

QColor Layouter::structureColor(const Node &node) const
{
    const XmlElement *ctrl = node.props ? node.props->child(kM, u"ctrlPr") : nullptr;
    if (ctrl != nullptr) {
        for (const XmlElement *e : ctrl->elements()) {
            if (e->name == u"rPr") {
                if (const QColor c = colorOf(*e); c.isValid()) {
                    return c;
                }
            }
        }
    }
    return m_options.color;
}

AtomClass Layouter::leftClass(const Node &node)
{
    switch (node.kind) {
    case NodeKind::Run:
        if (runIsNormalText(node) || node.text().isEmpty()) {
            return AtomClass::Ord;
        }
        return atomClassOf(displayChar(firstChar(node.text(), false)));
    case NodeKind::Fraction:
        return AtomClass::Inner;
    case NodeKind::Delimiter:
        return AtomClass::Open;
    case NodeKind::Nary:
    case NodeKind::Function:
        return AtomClass::Op;
    case NodeKind::Superscript:
    case NodeKind::Subscript:
    case NodeKind::SubSup:
        return node.args.isEmpty() || node.args.first().items.isEmpty() ? AtomClass::Ord
                                                                         : leftClass(*node.args.first().items.first());
    case NodeKind::LimLow:
    case NodeKind::LimUpp:
        return limitClass(node);
    case NodeKind::Box:
        return boxIsOperator(node) ? AtomClass::Rel : AtomClass::Ord;
    default:
        return AtomClass::Ord;
    }
}

AtomClass Layouter::rightClass(const Node &node)
{
    switch (node.kind) {
    case NodeKind::Run:
        if (runIsNormalText(node) || node.text().isEmpty()) {
            return AtomClass::Ord;
        }
        return atomClassOf(displayChar(firstChar(node.text(), true)));
    case NodeKind::Fraction:
        return AtomClass::Inner;
    case NodeKind::Delimiter:
        return AtomClass::Close;
    case NodeKind::Superscript:
    case NodeKind::Subscript:
    case NodeKind::SubSup:
        return node.args.isEmpty() || node.args.first().items.isEmpty() ? AtomClass::Ord
                                                                         : rightClass(*node.args.first().items.last());
    case NodeKind::LimLow:
    case NodeKind::LimUpp:
        return limitClass(node);
    case NodeKind::Box:
        return boxIsOperator(node) ? AtomClass::Rel : AtomClass::Ord;
    default:
        return AtomClass::Ord;
    }
}

bool Layouter::isSingleChar(const Argument &arg)
{
    return arg.items.size() == 1 && arg.items.first()->kind == NodeKind::Run
        && codePointCount(arg.items.first()->text()) == 1;
}

} // namespace QindaOffice::Math
