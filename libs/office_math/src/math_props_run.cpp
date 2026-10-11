// SPDX-License-Identifier: GPL-3.0-or-later
// Run, argument and equation properties and the document's math settings
// (math_props.h), split from math_props.cpp to keep both under 500 lines.
#include "math_props.h"
#include "math_props_p.h"

namespace QindaOffice::Math {

using Ooxml::XmlElement;
using detail::childVal;
using detail::onOffValue;
using detail::parseJc;
using detail::parseLimLoc;

namespace {

const QString &kM = detail::mathNamespace();

QString valueOr(const Node &node, QStringView name, const QString &fallback)
{
    const std::optional<QString> v = propValue(node, name);
    return v ? *v : fallback;
}

bool sameAttributes(const QList<Ooxml::XmlAttribute> &a, const QList<Ooxml::XmlAttribute> &b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (const Ooxml::XmlAttribute &x : a) {
        bool found = false;
        for (const Ooxml::XmlAttribute &y : b) {
            if (x.ns == y.ns && x.name == y.name) {
                found = x.value == y.value;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

} // namespace

MathStyle runStyle(const Node &run)
{
    const std::optional<QString> v = propValue(run, u"sty");
    if (!v) {
        return MathStyle::Default;
    }
    if (*v == u"p") {
        return MathStyle::Plain;
    }
    if (*v == u"b") {
        return MathStyle::Bold;
    }
    if (*v == u"bi") {
        return MathStyle::BoldItalic;
    }
    return MathStyle::Italic;
}

void setRunStyle(Node &run, MathStyle style)
{
    removeProp(run, u"nor");
    switch (style) {
    case MathStyle::Default:
        removeProp(run, u"sty");
        return;
    case MathStyle::Plain:
        setPropValue(run, u"sty", QStringLiteral("p"));
        return;
    case MathStyle::Bold:
        setPropValue(run, u"sty", QStringLiteral("b"));
        return;
    case MathStyle::Italic:
        setPropValue(run, u"sty", QStringLiteral("i"));
        return;
    case MathStyle::BoldItalic:
        setPropValue(run, u"sty", QStringLiteral("bi"));
        return;
    }
}

MathScript runScript(const Node &run)
{
    const QString v = valueOr(run, u"scr", QString());
    if (v == u"script") {
        return MathScript::Script;
    }
    if (v == u"fraktur") {
        return MathScript::Fraktur;
    }
    if (v == u"double-struck") {
        return MathScript::DoubleStruck;
    }
    if (v == u"sans-serif") {
        return MathScript::SansSerif;
    }
    if (v == u"monospace") {
        return MathScript::Monospace;
    }
    return MathScript::Roman;
}

void setRunScript(Node &run, MathScript script)
{
    static const QStringList names{QString(), QStringLiteral("script"), QStringLiteral("fraktur"),
                                   QStringLiteral("double-struck"), QStringLiteral("sans-serif"),
                                   QStringLiteral("monospace")};
    removeProp(run, u"nor");
    if (script == MathScript::Roman) {
        removeProp(run, u"scr");
    } else {
        setPropValue(run, u"scr", names.at(int(script)));
    }
}

bool runIsNormalText(const Node &run)
{
    return propOn(run, u"nor", false);
}

void setRunNormalText(Node &run, bool normal)
{
    if (!normal) {
        removeProp(run, u"nor");
        return;
    }
    // AGENT-GUARD: m:nor and m:scr/m:sty are a schema CHOICE (CT_RPR).
    removeProp(run, u"scr");
    removeProp(run, u"sty");
    setPropOn(run, u"nor", true);
}

bool runIsLiteral(const Node &run)
{
    return propOn(run, u"lit", false);
}

bool sameRunFormat(const Node &a, const Node &b)
{
    return sameXml(a.props.get(), b.props.get()) && sameXml(a.textProps.get(), b.textProps.get());
}

int argumentSize(const Argument &argument)
{
    if (!argument.argPr) {
        return 0;
    }
    bool ok = false;
    const int v = childVal(*argument.argPr, u"argSz").toInt(&ok);
    return ok ? qBound(-2, v, 2) : 0;
}

Justification justification(const Equation &equation, Justification fallback)
{
    if (!equation.paraProps) {
        return fallback;
    }
    return parseJc(childVal(*equation.paraProps, u"jc"), fallback);
}

void setJustification(Equation &equation, Justification justification)
{
    static const QStringList names{QStringLiteral("centerGroup"), QStringLiteral("center"), QStringLiteral("left"),
                                   QStringLiteral("right")};
    if (!equation.paraProps) {
        equation.paraProps = std::make_shared<XmlElement>(kM, QStringLiteral("oMathParaPr"));
    }
    XmlElement *jc = equation.paraProps->child(kM, u"jc");
    if (jc == nullptr) {
        jc = &equation.paraProps->appendElement(kM, QStringLiteral("jc"));
    }
    jc->setAttribute(kM, QStringLiteral("val"), names.at(int(justification)));
}

MathSettings MathSettings::fromXml(const XmlElement &mathPr)
{
    MathSettings s;
    const auto on = [&mathPr](QStringView name, bool fallback) {
        const XmlElement *e = mathPr.child(kM, name);
        if (e == nullptr) {
            return fallback;
        }
        const QString v = e->attr(u"val");
        return v.isEmpty() || onOffValue(v);
    };
    const auto twipsPt = [&mathPr](QStringView name) {
        bool ok = false;
        const qreal v = childVal(mathPr, name).toDouble(&ok);
        return ok ? v / 20.0 : 0.0;
    };
    if (const QString font = childVal(mathPr, u"mathFont"); !font.isEmpty()) {
        s.mathFont = font;
    }
    s.smallFraction = on(u"smallFrac", false);
    s.displayDefaults = on(u"dispDef", true);
    s.defaultJustification = parseJc(childVal(mathPr, u"defJc"), Justification::CenterGroup);
    s.integralLimits = parseLimLoc(childVal(mathPr, u"intLim"), LimitLocation::SubSup);
    s.naryLimits = parseLimLoc(childVal(mathPr, u"naryLim"), LimitLocation::UnderOver);
    s.leftMarginPt = twipsPt(u"lMargin");
    s.rightMarginPt = twipsPt(u"rMargin");
    s.spaceBeforePt = twipsPt(u"preSp");
    s.spaceAfterPt = twipsPt(u"postSp");
    return s;
}

bool sameXml(const XmlElement *a, const XmlElement *b)
{
    if (a == nullptr || b == nullptr) {
        return a == b;
    }
    if (a->ns != b->ns || a->name != b->name || !sameAttributes(a->attributes, b->attributes)
        || a->children.size() != b->children.size()) {
        return false;
    }
    for (qsizetype i = 0; i < a->children.size(); ++i) {
        const Ooxml::XmlChild &x = a->children.at(i);
        const Ooxml::XmlChild &y = b->children.at(i);
        if (bool(x.element) != bool(y.element)) {
            return false;
        }
        if (x.element ? !sameXml(x.element.get(), y.element.get()) : x.text != y.text) {
            return false;
        }
    }
    return true;
}

} // namespace QindaOffice::Math
