// SPDX-License-Identifier: GPL-3.0-or-later
// Text style levels for the pptx reader (pptx_context.h): run and
// paragraph properties as optional values, list styles (defPPr then
// lvl1pPr…lvl9pPr), their overlay order, and a:bodyPr.

#include "pptx_context.h"

#include "ooxml_units.h"

namespace QindaOffice::Show::Pptx {

namespace {

template <typename T>
void take(std::optional<T> &into, const std::optional<T> &from)
{
    if (from.has_value()) {
        into = from;
    }
}

std::optional<bool> onOff(const Ooxml::XmlElement *e, QStringView name)
{
    if (!e->hasAttribute(QString(), name)) {
        return std::nullopt;
    }
    return Ooxml::Units::parseOnOff(e->attr(name));
}

std::optional<Spacing> spacingFrom(const Ooxml::XmlElement *container)
{
    if (container == nullptr) {
        return std::nullopt;
    }
    if (const Ooxml::XmlElement *pct = container->child(nsA(), u"spcPct")) {
        return Spacing::percent(pct->attr(u"val").toDouble() / 1000.0);
    }
    if (const Ooxml::XmlElement *pts = container->child(nsA(), u"spcPts")) {
        return Spacing::points(pts->attr(u"val").toDouble() / 100.0);
    }
    return std::nullopt;
}

int alignmentOf(const QString &value)
{
    if (value == u"ctr") {
        return int(Qt::AlignHCenter);
    }
    if (value == u"r") {
        return int(Qt::AlignRight);
    }
    if (value == u"just" || value == u"dist" || value == u"thaiDist" || value == u"justLow") {
        return int(Qt::AlignJustify);
    }
    return int(Qt::AlignLeft);
}

} // namespace

void RunProps::overlay(const RunProps &more)
{
    take(size, more.size);
    take(bold, more.bold);
    take(italic, more.italic);
    take(underline, more.underline);
    take(strike, more.strike);
    take(caps, more.caps);
    take(latin, more.latin);
    take(baseline, more.baseline);
    take(spacing, more.spacing);
    take(color, more.color);
    take(highlight, more.highlight);
}

void ParaProps::overlay(const ParaProps &more)
{
    take(alignment, more.alignment);
    take(marginLeft, more.marginLeft);
    take(indent, more.indent);
    take(lineSpacing, more.lineSpacing);
    take(spaceBefore, more.spaceBefore);
    take(spaceAfter, more.spaceAfter);
    take(bulletKind, more.bulletKind);
    take(bulletChar, more.bulletChar);
    take(autoNumber, more.autoNumber);
    take(bulletFont, more.bulletFont);
    take(autoNumberStart, more.autoNumberStart);
    take(bulletColor, more.bulletColor);
    take(bulletSize, more.bulletSize);
    run.overlay(more.run);
}

void ListStyle::overlay(const ListStyle &more)
{
    for (std::size_t i = 0; i < levels.size(); ++i) {
        levels[i].overlay(more.levels[i]);
    }
}

RunProps parseRunProps(const PartContext &ctx, const Ooxml::XmlElement *rPr)
{
    RunProps run;
    if (rPr == nullptr) {
        return run;
    }
    if (rPr->hasAttribute(QString(), u"sz")) {
        run.size = rPr->attr(u"sz").toDouble() / 100.0;
    }
    run.bold = onOff(rPr, u"b");
    run.italic = onOff(rPr, u"i");
    if (rPr->hasAttribute(QString(), u"u")) {
        const QString u = rPr->attr(u"u");
        run.underline = u == u"none" ? QString() : u;
    }
    if (rPr->hasAttribute(QString(), u"strike")) {
        const QString strike = rPr->attr(u"strike");
        run.strike = strike == u"noStrike" ? QString() : strike;
    }
    if (rPr->hasAttribute(QString(), u"cap")) {
        const QString cap = rPr->attr(u"cap");
        run.caps = cap == u"none" ? QString() : cap;
    }
    if (rPr->hasAttribute(QString(), u"baseline")) {
        run.baseline = rPr->attr(u"baseline").toInt();
    }
    if (rPr->hasAttribute(QString(), u"spc")) {
        run.spacing = rPr->attr(u"spc").toDouble() / 100.0;
    }
    for (const Ooxml::XmlElement *e : rPr->elements()) {
        if (e->ns != nsA()) {
            continue;
        }
        if (e->name == u"solidFill") {
            const QColor c = childColor(ctx, e);
            if (c.isValid()) {
                run.color = c;
            }
        } else if (e->name == u"gradFill") {
            const ResolvedFill fill = fillFrom(ctx, rPr);
            if (fill.plain.isValid()) {
                run.color = fill.plain;
            }
        } else if (e->name == u"noFill") {
            run.color = QColor(Qt::transparent);
        } else if (e->name == u"highlight") {
            run.highlight = childColor(ctx, e);
        } else if (e->name == u"latin") {
            run.latin = resolveTypeface(ctx, e->attr(u"typeface"));
        }
    }
    return run;
}

ParaProps parseParaProps(const PartContext &ctx, const Ooxml::XmlElement *pPr)
{
    ParaProps p;
    if (pPr == nullptr) {
        return p;
    }
    if (pPr->hasAttribute(QString(), u"algn")) {
        p.alignment = alignmentOf(pPr->attr(u"algn"));
    }
    if (pPr->hasAttribute(QString(), u"marL")) {
        p.marginLeft = emuToPt(pPr->attr(u"marL"));
    }
    if (pPr->hasAttribute(QString(), u"indent")) {
        p.indent = emuToPt(pPr->attr(u"indent"));
    }
    p.lineSpacing = spacingFrom(pPr->child(nsA(), u"lnSpc"));
    p.spaceBefore = spacingFrom(pPr->child(nsA(), u"spcBef"));
    p.spaceAfter = spacingFrom(pPr->child(nsA(), u"spcAft"));
    for (const Ooxml::XmlElement *e : pPr->elements()) {
        if (e->ns != nsA()) {
            continue;
        }
        if (e->name == u"buNone") {
            p.bulletKind = 0;
        } else if (e->name == u"buChar") {
            p.bulletKind = 1;
            p.bulletChar = e->attr(u"char", QStringLiteral("•"));
        } else if (e->name == u"buBlip") {
            p.bulletKind = 1;
            p.bulletChar = QStringLiteral("•");
        } else if (e->name == u"buAutoNum") {
            p.bulletKind = 2;
            p.autoNumber = e->attr(u"type", QStringLiteral("arabicPeriod"));
            p.autoNumberStart = e->attr(u"startAt", QStringLiteral("1")).toInt();
        } else if (e->name == u"buFont") {
            p.bulletFont = resolveTypeface(ctx, e->attr(u"typeface"));
        } else if (e->name == u"buFontTx") {
            p.bulletFont = QString();
        } else if (e->name == u"buClr") {
            p.bulletColor = childColor(ctx, e);
        } else if (e->name == u"buClrTx") {
            p.bulletColor = QColor();
        } else if (e->name == u"buSzPct") {
            p.bulletSize = e->attr(u"val").toDouble() / 1000.0;
        } else if (e->name == u"buSzPts") {
            p.bulletSize = -e->attr(u"val").toDouble() / 100.0;
        } else if (e->name == u"buSzTx") {
            p.bulletSize = 100.0;
        } else if (e->name == u"defRPr") {
            p.run = parseRunProps(ctx, e);
        }
    }
    return p;
}

ListStyle parseListStyle(const PartContext &ctx, const Ooxml::XmlElement *list)
{
    ListStyle style;
    if (list == nullptr) {
        return style;
    }
    // a:defPPr is the base of every level; a:lvlNpPr refine it.
    const ParaProps base = parseParaProps(ctx, list->child(nsA(), u"defPPr"));
    for (int level = 0; level < 9; ++level) {
        style.levels[level] = base;
        const QString name = QStringLiteral("lvl%1pPr").arg(level + 1);
        style.levels[level].overlay(parseParaProps(ctx, list->child(nsA(), name)));
    }
    return style;
}

TextBodyStyle officeBodyDefaults()
{
    TextBodyStyle style;
    style.insetLeft = 7.2;  // 91440 EMU
    style.insetRight = 7.2;
    style.insetTop = 3.6;   // 45720 EMU
    style.insetBottom = 3.6;
    return style;
}

void applyBodyProps(TextBodyStyle &style, const Ooxml::XmlElement *bodyPr)
{
    if (bodyPr == nullptr) {
        return;
    }
    const auto inset = [bodyPr](const char16_t *name, qreal &into) {
        if (bodyPr->hasAttribute(QString(), name)) {
            into = emuToPt(bodyPr->attr(name));
        }
    };
    inset(u"lIns", style.insetLeft);
    inset(u"tIns", style.insetTop);
    inset(u"rIns", style.insetRight);
    inset(u"bIns", style.insetBottom);
    if (bodyPr->hasAttribute(QString(), u"anchor")) {
        const QString anchor = bodyPr->attr(u"anchor");
        style.anchor = anchor == u"ctr"   ? TextAnchor::Middle
            : anchor == u"b"              ? TextAnchor::Bottom
            : anchor == u"just"           ? TextAnchor::Justified
            : anchor == u"dist"           ? TextAnchor::Distributed
                                          : TextAnchor::Top;
    }
    if (bodyPr->hasAttribute(QString(), u"anchorCtr")) {
        style.anchorCenter = Ooxml::Units::parseOnOff(bodyPr->attr(u"anchorCtr"));
    }
    if (bodyPr->hasAttribute(QString(), u"wrap")) {
        style.wrap = bodyPr->attr(u"wrap") != u"none";
    }
    if (bodyPr->hasAttribute(QString(), u"vert")) {
        const QString vert = bodyPr->attr(u"vert");
        style.vertical = vert == u"horz" ? QString() : vert;
    }
    if (bodyPr->child(nsA(), u"noAutofit") != nullptr) {
        style.autoFit = AutoFit::None;
        style.fontScale = 100;
        style.lineSpaceReduction = 0;
    } else if (const Ooxml::XmlElement *norm = bodyPr->child(nsA(), u"normAutofit")) {
        style.autoFit = AutoFit::Normal;
        style.fontScale = norm->attr(u"fontScale", QStringLiteral("100000")).toDouble() / 1000.0;
        style.lineSpaceReduction = norm->attr(u"lnSpcReduction", QStringLiteral("0")).toDouble() / 1000.0;
    } else if (bodyPr->child(nsA(), u"spAutoFit") != nullptr) {
        style.autoFit = AutoFit::Shape;
        style.fontScale = 100;
        style.lineSpaceReduction = 0;
    }
}

} // namespace QindaOffice::Show::Pptx
