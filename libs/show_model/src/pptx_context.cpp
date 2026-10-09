// SPDX-License-Identifier: GPL-3.0-or-later
// Colours, fills, outlines, the theme's style matrix, related parts and
// hyperlinks for the pptx reader (pptx_context.h).

#include "pptx_context.h"

#include "ooxml_namespaces.h"


namespace QindaOffice::Show::Pptx {

const QString &nsA()
{
    static const QString s = QString::fromUtf16(Ooxml::Ns::A);
    return s;
}

const QString &nsP()
{
    static const QString s = QString::fromUtf16(Ooxml::Ns::P);
    return s;
}

const QString &nsR()
{
    static const QString s = QString::fromUtf16(Ooxml::Ns::R);
    return s;
}

const QString &nsC()
{
    static const QString s = QString::fromUtf16(Ooxml::Ns::C);
    return s;
}

const QString &nsMc()
{
    static const QString s = QString::fromUtf16(Ooxml::Ns::MC);
    return s;
}

ThemeContext parseThemeContext(const Ooxml::XmlElement *themeRoot)
{
    ThemeContext context;
    if (themeRoot == nullptr) {
        context.theme = Ooxml::defaultOfficeTheme();
        return context;
    }
    context.theme = Ooxml::parseTheme(*themeRoot);
    if (!context.theme.isValid()) {
        context.theme.colors = Ooxml::defaultOfficeTheme().colors;
    }
    const Ooxml::XmlElement *elements = themeRoot->child(nsA(), u"themeElements");
    const Ooxml::XmlElement *format = elements != nullptr ? elements->child(nsA(), u"fmtScheme") : nullptr;
    if (format != nullptr) {
        const auto collect = [format](const char16_t *list, QList<Ooxml::XmlElementPtr> &into) {
            if (const Ooxml::XmlElement *e = format->child(nsA(), list)) {
                for (const Ooxml::XmlElement *item : e->elements()) {
                    into.append(item->clone());
                }
            }
        };
        collect(u"fillStyleLst", context.fillStyles);
        collect(u"lnStyleLst", context.lineStyles);
        collect(u"bgFillStyleLst", context.backgroundStyles);
    }
    return context;
}

QColor eightBit(const QColor &color)
{
    // AGENT-NOTE: DrawingML's colour transforms (tint, shade, lumMod …)
    // compute in floating point and QColor keeps 16 bits a channel, but
    // PowerPoint draws 8-bit sRGB and every spelling the model goes through
    // (a:srgbClr, .fodp, the clipboard JSON) stores 8 bits: a finer colour
    // would not come back equal, and would not draw the same.
    return color.isValid() ? QColor::fromRgba(color.rgba()) : color;
}

QColor color(const PartContext &ctx, const Ooxml::XmlElement *colorElement, const QColor &phColor)
{
    const Ooxml::Theme &theme = ctx.theme != nullptr ? ctx.theme->theme : Ooxml::defaultOfficeTheme();
    return eightBit(Ooxml::resolveColor(colorElement, theme, ctx.map, phColor));
}

QColor childColor(const PartContext &ctx, const Ooxml::XmlElement *container, const QColor &phColor)
{
    const Ooxml::Theme &theme = ctx.theme != nullptr ? ctx.theme->theme : Ooxml::defaultOfficeTheme();
    return eightBit(Ooxml::resolveChildColor(container, theme, ctx.map, phColor));
}

RelatedData relatedData(const PartContext &ctx, const QString &relationshipId)
{
    RelatedData data;
    if (!ctx.package || relationshipId.isEmpty()) {
        return data;
    }
    const std::optional<Ooxml::Relationship> rel = ctx.package->relationship(ctx.part, relationshipId);
    if (!rel || rel->external) {
        return data;
    }
    data.partName = ctx.package->targetPart(ctx.part, *rel);
    if (data.partName.isEmpty()) {
        return data;
    }
    data.bytes = ctx.package->data(data.partName);
    data.mime = ctx.package->contentType(data.partName);
    return data;
}

QString hyperlinkFrom(const PartContext &ctx, const Ooxml::XmlElement *hlink)
{
    if (hlink == nullptr) {
        return {};
    }
    const QString id = hlink->attribute(nsR(), u"id");
    const QString action = hlink->attr(u"action");
    if (!id.isEmpty() && ctx.package) {
        if (const std::optional<Ooxml::Relationship> rel = ctx.package->relationship(ctx.part, id)) {
            if (rel->external) {
                return rel->target;
            }
            const QString target = ctx.package->targetPart(ctx.part, *rel);
            if (ctx.slideIds != nullptr && ctx.slideIds->contains(target)) {
                return QLatin1Char('#') + ctx.slideIds->value(target);
            }
        }
    }
    return action;
}

namespace {

ResolvedFill gradient(const PartContext &ctx, const Ooxml::XmlElement *grad, const QColor &phColor)
{
    ResolvedFill fill;
    fill.present = true;
    fill.style.kind = FillKind::Gradient;
    if (const Ooxml::XmlElement *list = grad->child(nsA(), u"gsLst")) {
        for (const Ooxml::XmlElement *gs : list->elements(nsA(), u"gs")) {
            fill.style.stops.append({gs->attr(u"pos").toDouble() / 100000.0, childColor(ctx, gs, phColor)});
        }
    }
    std::sort(fill.style.stops.begin(), fill.style.stops.end(),
              [](const GradientStop &a, const GradientStop &b) { return a.position < b.position; });
    if (const Ooxml::XmlElement *lin = grad->child(nsA(), u"lin")) {
        fill.style.angle = lin->attr(u"ang").toDouble() / 60000.0;
    } else if (const Ooxml::XmlElement *path = grad->child(nsA(), u"path")) {
        fill.style.path = path->attr(u"path", QStringLiteral("circle"));
    } else {
        fill.style.angle = 90.0; // no direction given: PowerPoint's top-to-bottom
    }
    fill.plain = fill.style.stops.isEmpty() ? QColor(Qt::transparent) : fill.style.stops.first().color;
    return fill;
}

} // namespace

ResolvedFill fillFrom(const PartContext &ctx, const Ooxml::XmlElement *parent, const QColor &phColor)
{
    ResolvedFill fill;
    if (parent == nullptr) {
        return fill;
    }
    for (const Ooxml::XmlElement *e : parent->elements()) {
        if (e->ns != nsA()) {
            continue;
        }
        if (e->name == u"noFill") {
            fill.present = true;
            return fill;
        }
        if (e->name == u"solidFill") {
            fill.present = true;
            fill.plain = childColor(ctx, e, phColor);
            if (!fill.plain.isValid()) {
                fill.plain = Qt::transparent;
            }
            return fill;
        }
        if (e->name == u"gradFill") {
            return gradient(ctx, e, phColor);
        }
        if (e->name == u"blipFill") {
            fill.present = true;
            fill.style.kind = FillKind::Picture;
            if (const Ooxml::XmlElement *blip = e->child(nsA(), u"blip")) {
                const RelatedData data = relatedData(ctx, blip->attribute(nsR(), u"embed"));
                fill.style.image = data.bytes;
                fill.style.imageMime = data.mime;
                // M75: Format Background's Transparency.
                if (const Ooxml::XmlElement *alpha = blip->child(nsA(), u"alphaModFix")) {
                    fill.style.imageAlpha = qBound(0.0, alpha->attr(u"amt", QStringLiteral("100000")).toDouble() / 100000.0, 1.0);
                }
            }
            fill.style.tile = e->child(nsA(), u"tile") != nullptr;
            fill.plain = QColor(200, 200, 200);
            return fill;
        }
        if (e->name == u"pattFill") {
            fill.present = true;
            fill.style.kind = FillKind::Pattern;
            fill.style.pattern = e->attr(u"prst", QStringLiteral("pct5"));
            fill.style.foreground = childColor(ctx, e->child(nsA(), u"fgClr"), phColor);
            fill.style.background = childColor(ctx, e->child(nsA(), u"bgClr"), phColor);
            fill.plain = fill.style.foreground.isValid() ? fill.style.foreground : QColor(Qt::black);
            return fill;
        }
        if (e->name == u"grpFill") {
            fill.present = true;
            fill.style.pattern = QStringLiteral("group"); // the caller substitutes the group's fill
            return fill;
        }
    }
    return fill;
}

ResolvedFill fillFromReference(const PartContext &ctx, const Ooxml::XmlElement *ref)
{
    if (ref == nullptr || ctx.theme == nullptr) {
        return {};
    }
    const int idx = ref->attr(u"idx").toInt();
    const QColor phColor = childColor(ctx, ref);
    const QList<Ooxml::XmlElementPtr> &list = idx >= 1001 ? ctx.theme->backgroundStyles : ctx.theme->fillStyles;
    const int index = (idx >= 1001 ? idx - 1001 : idx - 1);
    if (idx == 0 || index < 0 || index >= list.size()) {
        return ResolvedFill{true, Qt::transparent, {}}; // idx 0: no fill
    }
    // The style list holds bare fill elements; wrap one to reuse fillFrom.
    Ooxml::XmlElement holder(nsA(), QStringLiteral("holder"));
    holder.appendElement(list.at(index)->clone());
    return fillFrom(ctx, &holder, phColor);
}

ResolvedLine lineFrom(const PartContext &ctx, const Ooxml::XmlElement *ln, const QColor &phColor)
{
    ResolvedLine line;
    if (ln == nullptr) {
        return line;
    }
    line.present = true;
    line.width = ln->hasAttribute(QString(), u"w") ? emuToPt(ln->attr(u"w")) : 0.75;
    const ResolvedFill fill = fillFrom(ctx, ln, phColor);
    line.colorSet = fill.present;
    if (fill.present) {
        line.color = fill.plain;
    } else {
        // AGENT-NOTE: an a:ln without a fill takes its colour from the
        // shape's style (lnRef); with none, PowerPoint draws no line — an
        // empty <a:ln/> is common in generated decks and is not black.
        line.color = phColor.isValid() ? phColor : QColor(Qt::transparent);
    }
    const QString cap = ln->attr(u"cap");
    line.style.cap = cap == u"flat" ? QString() : cap;
    if (const Ooxml::XmlElement *dash = ln->child(nsA(), u"prstDash")) {
        const QString value = dash->attr(u"val");
        line.style.dash = value == u"solid" ? QString() : value;
    } else if (ln->child(nsA(), u"custDash") != nullptr) {
        line.style.dash = QStringLiteral("dash");
    }
    if (ln->child(nsA(), u"bevel") != nullptr) {
        line.style.join = QStringLiteral("bevel");
    } else if (ln->child(nsA(), u"miter") != nullptr) {
        line.style.join = QStringLiteral("miter");
    }
    const auto end = [ln](const char16_t *name) {
        LineEnd result;
        if (const Ooxml::XmlElement *e = ln->child(nsA(), name)) {
            const QString type = e->attr(u"type");
            result.type = type == u"none" ? QString() : type;
            result.width = e->attr(u"w");
            result.length = e->attr(u"len");
        }
        return result;
    };
    line.style.head = end(u"headEnd");
    line.style.tail = end(u"tailEnd");
    return line;
}

ResolvedLine lineFromReference(const PartContext &ctx, const Ooxml::XmlElement *lnRef)
{
    if (lnRef == nullptr || ctx.theme == nullptr) {
        return {};
    }
    const int idx = lnRef->attr(u"idx").toInt();
    if (idx <= 0 || idx > ctx.theme->lineStyles.size()) {
        ResolvedLine none;
        none.present = true;
        return none;
    }
    return lineFrom(ctx, ctx.theme->lineStyles.at(idx - 1).get(), childColor(ctx, lnRef));
}

QString appLayoutName(const QString &type)
{
    if (type == u"title" || type == u"secHead" || type == u"titleOnly") {
        return QStringLiteral("title");
    }
    if (type == u"blank") {
        return QStringLiteral("blank");
    }
    if (type == u"objOnly") {
        return QStringLiteral("content");
    }
    if (type.startsWith(QLatin1String("two")) || type == u"objAndTx" || type == u"txAndObj" || type == u"fourObj") {
        return QStringLiteral("title+2content");
    }
    return QStringLiteral("title+content");
}

QString resolveTypeface(const PartContext &ctx, const QString &typeface)
{
    if (!typeface.startsWith(QLatin1Char('+')) || ctx.theme == nullptr) {
        return typeface;
    }
    const Ooxml::Theme &theme = ctx.theme->theme;
    if (typeface.startsWith(QLatin1String("+mj"))) {
        return typeface.endsWith(QLatin1String("-ea")) ? theme.majorEastAsian : theme.majorLatin;
    }
    return typeface.endsWith(QLatin1String("-ea")) ? theme.minorEastAsian : theme.minorLatin;
}

} // namespace QindaOffice::Show::Pptx
