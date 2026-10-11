// SPDX-License-Identifier: GPL-3.0-or-later
#include "math_props.h"
#include "math_props_p.h"

#include "ooxml_namespaces.h"

namespace QindaOffice::Math {

using Ooxml::XmlElement;
using detail::childVal;
using detail::onOffValue;
using detail::parseJc;
using detail::parseLimLoc;

namespace detail {

const QString &mathNamespace()
{
    static const QString ns = QString::fromUtf16(Ooxml::Ns::M);
    return ns;
}

bool onOffValue(const QString &v)
{
    return !(v == u"0" || v == u"false" || v == u"off");
}

Justification parseJc(const QString &v, Justification fallback)
{
    if (v == u"left") {
        return Justification::Left;
    }
    if (v == u"right") {
        return Justification::Right;
    }
    if (v == u"center") {
        return Justification::Center;
    }
    if (v == u"centerGroup") {
        return Justification::CenterGroup;
    }
    return fallback;
}

LimitLocation parseLimLoc(const QString &v, LimitLocation fallback)
{
    if (v == u"undOvr") {
        return LimitLocation::UnderOver;
    }
    if (v == u"subSup") {
        return LimitLocation::SubSup;
    }
    return fallback;
}

QString childVal(const XmlElement &parent, QStringView name)
{
    const XmlElement *e = parent.child(mathNamespace(), name);
    return e != nullptr ? e->attr(u"val") : QString();
}

} // namespace detail

namespace {

const QString &kM = detail::mathNamespace();

// Children of each property element in schema order (shared-math.xsd).
QList<QStringView> childOrder(NodeKind kind)
{
    switch (kind) {
    case NodeKind::Run:
        return {u"lit", u"nor", u"scr", u"sty", u"brk", u"aln"};
    case NodeKind::Fraction:
        return {u"type", u"ctrlPr"};
    case NodeKind::Radical:
        return {u"degHide", u"ctrlPr"};
    case NodeKind::Nary:
        return {u"chr", u"limLoc", u"grow", u"subHide", u"supHide", u"ctrlPr"};
    case NodeKind::Delimiter:
        return {u"begChr", u"sepChr", u"endChr", u"grow", u"shp", u"ctrlPr"};
    case NodeKind::Accent:
        return {u"chr", u"ctrlPr"};
    case NodeKind::Bar:
        return {u"pos", u"ctrlPr"};
    case NodeKind::Box:
        return {u"opEmu", u"noBreak", u"diff", u"brk", u"aln", u"ctrlPr"};
    case NodeKind::BorderBox:
        return {u"hideTop", u"hideBot", u"hideLeft", u"hideRight", u"strikeH",
                u"strikeV", u"strikeBLTR", u"strikeTLBR", u"ctrlPr"};
    case NodeKind::EqArray:
        return {u"baseJc", u"maxDist", u"objDist", u"rSpRule", u"rSp", u"ctrlPr"};
    case NodeKind::Matrix:
        return {u"baseJc", u"plcHide", u"rSpRule", u"cGpRule", u"rSp", u"cSp", u"cGp", u"mcs", u"ctrlPr"};
    case NodeKind::GroupChar:
        return {u"chr", u"pos", u"vertJc", u"ctrlPr"};
    case NodeKind::Phantom:
        return {u"show", u"zeroWid", u"zeroAsc", u"zeroDesc", u"transp", u"ctrlPr"};
    case NodeKind::SubSup:
        return {u"alnScr", u"ctrlPr"};
    default:
        return {u"ctrlPr"};
    }
}

QString valueOr(const Node &node, QStringView name, const QString &fallback)
{
    const std::optional<QString> v = propValue(node, name);
    return v ? *v : fallback;
}

int intValue(const Node &node, QStringView name, int fallback)
{
    const std::optional<QString> v = propValue(node, name);
    bool ok = false;
    const int n = v ? v->toInt(&ok) : 0;
    return ok ? n : fallback;
}

VerticalPosition topBottom(const Node &node, QStringView name, VerticalPosition fallback)
{
    const std::optional<QString> v = propValue(node, name);
    if (!v) {
        return fallback;
    }
    return *v == u"top" ? VerticalPosition::Top : VerticalPosition::Bottom;
}

} // namespace

std::optional<QString> propValue(const Node &node, QStringView name)
{
    if (!node.props) {
        return std::nullopt;
    }
    const XmlElement *e = node.props->child(kM, name);
    if (e == nullptr) {
        return std::nullopt;
    }
    return e->attr(u"val");
}

bool propOn(const Node &node, QStringView name, bool fallback)
{
    const std::optional<QString> v = propValue(node, name);
    if (!v) {
        return fallback;
    }
    return v->isEmpty() || onOffValue(*v);
}

void setPropValue(Node &node, QStringView name, const QString &value)
{
    if (!node.props) {
        node.props = std::make_shared<XmlElement>(kM, propertyName(node.kind).toString());
    }
    if (XmlElement *e = node.props->child(kM, name)) {
        e->removeAttribute(QString(), u"val");
        e->setAttribute(kM, QStringLiteral("val"), value);
        return;
    }
    auto e = std::make_shared<XmlElement>(kM, name.toString());
    e->setAttribute(kM, QStringLiteral("val"), value);
    const QList<QStringView> order = childOrder(node.kind);
    const qsizetype at = order.indexOf(name);
    node.props->insertElementBefore(std::move(e), at >= 0 ? order.mid(at + 1) : QList<QStringView>());
}

void setPropOn(Node &node, QStringView name, bool on)
{
    setPropValue(node, name, on ? QStringLiteral("1") : QStringLiteral("0"));
}

void removeProp(Node &node, QStringView name)
{
    if (node.props) {
        node.props->removeChildren(kM, name);
    }
}

FractionType fractionType(const Node &fraction)
{
    const QString v = valueOr(fraction, u"type", QString());
    if (v == u"skw") {
        return FractionType::Skewed;
    }
    if (v == u"lin") {
        return FractionType::Linear;
    }
    if (v == u"noBar") {
        return FractionType::NoBar;
    }
    return FractionType::Bar;
}

void setFractionType(Node &fraction, FractionType type)
{
    switch (type) {
    case FractionType::Bar:
        removeProp(fraction, u"type");
        return;
    case FractionType::Skewed:
        setPropValue(fraction, u"type", QStringLiteral("skw"));
        return;
    case FractionType::Linear:
        setPropValue(fraction, u"type", QStringLiteral("lin"));
        return;
    case FractionType::NoBar:
        setPropValue(fraction, u"type", QStringLiteral("noBar"));
        return;
    }
}

QString naryChar(const Node &nary)
{
    return valueOr(nary, u"chr", QStringLiteral("∫"));
}

LimitLocation limitLocation(const Node &nary)
{
    return parseLimLoc(valueOr(nary, u"limLoc", QString()), LimitLocation::Default);
}

bool naryGrows(const Node &nary)
{
    return propOn(nary, u"grow", false);
}

bool subHidden(const Node &nary)
{
    return propOn(nary, u"subHide", false);
}

bool supHidden(const Node &nary)
{
    return propOn(nary, u"supHide", false);
}

QString delimiterBegin(const Node &d)
{
    return valueOr(d, u"begChr", QStringLiteral("("));
}

QString delimiterSeparator(const Node &d)
{
    return valueOr(d, u"sepChr", QStringLiteral("│"));
}

QString delimiterEnd(const Node &d)
{
    return valueOr(d, u"endChr", QStringLiteral(")"));
}

bool delimiterGrows(const Node &d)
{
    return propOn(d, u"grow", true);
}

DelimiterShape delimiterShape(const Node &d)
{
    return valueOr(d, u"shp", QString()) == u"match" ? DelimiterShape::Match : DelimiterShape::Centered;
}

QString accentChar(const Node &acc)
{
    return valueOr(acc, u"chr", QStringLiteral("\u0302"));
}

VerticalPosition barPosition(const Node &bar)
{
    return topBottom(bar, u"pos", VerticalPosition::Bottom);
}

QString groupChar(const Node &groupChr)
{
    return valueOr(groupChr, u"chr", QStringLiteral("⏟"));
}

VerticalPosition groupCharPosition(const Node &groupChr)
{
    return topBottom(groupChr, u"pos", VerticalPosition::Bottom);
}

bool degreeHidden(const Node &radical)
{
    return propOn(radical, u"degHide", false);
}

bool alignedScripts(const Node &subSup)
{
    return propOn(subSup, u"alnScr", false);
}

PhantomFlags phantomFlags(const Node &phant)
{
    PhantomFlags f;
    f.show = propOn(phant, u"show", true);
    f.zeroWidth = propOn(phant, u"zeroWid", false);
    f.zeroAscent = propOn(phant, u"zeroAsc", false);
    f.zeroDescent = propOn(phant, u"zeroDesc", false);
    f.transparent = propOn(phant, u"transp", false);
    return f;
}

void setPhantomFlags(Node &phant, const PhantomFlags &flags)
{
    const auto apply = [&phant](QStringView name, bool on, bool fallback) {
        if (on == fallback) {
            removeProp(phant, name);
        } else {
            setPropOn(phant, name, on);
        }
    };
    apply(u"show", flags.show, true);
    apply(u"zeroWid", flags.zeroWidth, false);
    apply(u"zeroAsc", flags.zeroAscent, false);
    apply(u"zeroDesc", flags.zeroDescent, false);
    apply(u"transp", flags.transparent, false);
}

BorderFlags borderFlags(const Node &borderBox)
{
    BorderFlags f;
    f.hideTop = propOn(borderBox, u"hideTop", false);
    f.hideBottom = propOn(borderBox, u"hideBot", false);
    f.hideLeft = propOn(borderBox, u"hideLeft", false);
    f.hideRight = propOn(borderBox, u"hideRight", false);
    f.strikeHorizontal = propOn(borderBox, u"strikeH", false);
    f.strikeVertical = propOn(borderBox, u"strikeV", false);
    f.strikeUp = propOn(borderBox, u"strikeBLTR", false);
    f.strikeDown = propOn(borderBox, u"strikeTLBR", false);
    return f;
}

bool boxIsOperator(const Node &box)
{
    return propOn(box, u"opEmu", false);
}

bool boxIsDifferential(const Node &box)
{
    return propOn(box, u"diff", false);
}

VerticalJustification baseJustification(const Node &node)
{
    const QString v = valueOr(node, u"baseJc", QString());
    if (v == u"top") {
        return VerticalJustification::Top;
    }
    if (v == u"bottom" || v == u"bot") {
        return VerticalJustification::Bottom;
    }
    return VerticalJustification::Center;
}

int rowSpacingRule(const Node &node)
{
    return qBound(0, intValue(node, u"rSpRule", 0), 4);
}

int rowSpacing(const Node &node)
{
    return qMax(0, intValue(node, u"rSp", 0));
}

int columnCount(const Node &matrix)
{
    int columns = 0;
    for (const int n : matrix.rowSizes) {
        columns = qMax(columns, n);
    }
    return columns;
}

ColumnAlignment columnAlignment(const Node &matrix, int column)
{
    const XmlElement *mcs = matrix.props ? matrix.props->child(kM, u"mcs") : nullptr;
    if (mcs == nullptr) {
        return ColumnAlignment::Center;
    }
    int first = 0;
    for (const XmlElement *mc : mcs->elements(kM, u"mc")) {
        const XmlElement *pr = mc->child(kM, u"mcPr");
        bool ok = false;
        const int count = pr != nullptr ? childVal(*pr, u"count").toInt(&ok) : 1;
        const int span = ok ? qMax(1, count) : 1;
        if (column < first + span) {
            const QString jc = pr != nullptr ? childVal(*pr, u"mcJc") : QString();
            if (jc == u"left" || jc == u"inside") {
                return ColumnAlignment::Left;
            }
            if (jc == u"right" || jc == u"outside") {
                return ColumnAlignment::Right;
            }
            return ColumnAlignment::Center;
        }
        first += span;
    }
    return ColumnAlignment::Center;
}

bool placeholdersHidden(const Node &matrix)
{
    return propOn(matrix, u"plcHide", false);
}

int columnGapRule(const Node &matrix)
{
    return qBound(0, intValue(matrix, u"cGpRule", 0), 4);
}

int columnGap(const Node &matrix)
{
    return qMax(0, intValue(matrix, u"cGp", 0));
}

int columnSpacing(const Node &matrix)
{
    return qMax(0, intValue(matrix, u"cSp", 0));
}

} // namespace QindaOffice::Math
