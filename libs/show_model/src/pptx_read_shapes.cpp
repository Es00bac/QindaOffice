// SPDX-License-Identifier: GPL-3.0-or-later
// p:sp, p:pic, p:cxnSp and p:grpSp for the pptx reader, with placeholder
// inheritance (pptx_read_shapes.h).

#include "pptx_read_shapes.h"

#include "pptx_origin.h"
#include "preset_geometry.h"
#include "pptx_read_text.h"
#include "pptx_reader.h"

#include "ooxml_units.h"

#include <QBuffer>
#include <QImageReader>

namespace QindaOffice::Show::Pptx {

namespace {

const QString kPresentation2010 = QStringLiteral("http://schemas.microsoft.com/office/powerpoint/2010/main");

const Ooxml::XmlElement *templateChild(const PlaceholderTemplate *t, const char16_t *name)
{
    return t != nullptr && t->element ? t->element->child(t->element->ns, name) : nullptr;
}

const Ooxml::XmlElement *spPrOf(const PlaceholderTemplate *t)
{
    return templateChild(t, u"spPr");
}

const Ooxml::XmlElement *bodyPrOf(const PlaceholderTemplate *t)
{
    const Ooxml::XmlElement *body = templateChild(t, u"txBody");
    return body != nullptr ? body->child(nsA(), u"bodyPr") : nullptr;
}

RunProps fontReference(const PartContext &ctx, const Ooxml::XmlElement *style)
{
    RunProps run;
    const Ooxml::XmlElement *ref = style != nullptr ? style->child(nsA(), u"fontRef") : nullptr;
    if (ref == nullptr) {
        return run;
    }
    const QColor c = childColor(ctx, ref);
    if (c.isValid()) {
        run.color = c;
    }
    const QString idx = ref->attr(u"idx");
    if (idx == u"major" || idx == u"minor") {
        run.latin = resolveTypeface(ctx, idx == u"major" ? QStringLiteral("+mj-lt") : QStringLiteral("+mn-lt"));
    }
    return run;
}

void readGeometry(const Ooxml::XmlElement *spPr, QString *preset, Geometry::Adjustments *adjust, QString *custom)
{
    if (spPr == nullptr) {
        return;
    }
    if (const Ooxml::XmlElement *prst = spPr->child(nsA(), u"prstGeom")) {
        *preset = prst->attr(u"prst", QStringLiteral("rect"));
        if (const Ooxml::XmlElement *av = prst->child(nsA(), u"avLst")) {
            for (const Ooxml::XmlElement *gd : av->elements(nsA(), u"gd")) {
                const QString formula = gd->attr(u"fmla");
                if (formula.startsWith(QLatin1String("val "))) {
                    adjust->append({gd->attr(u"name"), formula.mid(4).trimmed().toLongLong()});
                }
            }
        }
    } else if (const Ooxml::XmlElement *cust = spPr->child(nsA(), u"custGeom")) {
        *custom = Ooxml::serializeFragment(*cust);
    }
}

// Line in effect: the shape's own a:ln over its style's lnRef.
ResolvedLine effectiveLine(const PartContext &ctx, const Ooxml::XmlElement *spPr, const Ooxml::XmlElement *style,
                           const ResolvedLine *inherited = nullptr)
{
    ResolvedLine reference = lineFromReference(ctx, style != nullptr ? style->child(nsA(), u"lnRef") : nullptr);
    if (inherited != nullptr && inherited->present) {
        reference = *inherited;
    }
    ResolvedLine own = lineFrom(ctx, spPr != nullptr ? spPr->child(nsA(), u"ln") : nullptr);
    if (!own.present) {
        return reference;
    }
    if (!own.colorSet) {
        own.color = reference.present ? reference.color : QColor(Qt::transparent);
    }
    if (!spPr->child(nsA(), u"ln")->hasAttribute(QString(), u"w") && reference.present) {
        own.width = reference.width;
    }
    return own;
}

void applyLine(Shape &shape, const ResolvedLine &line)
{
    shape.stroke = line.present ? line.color : QColor(Qt::transparent);
    shape.strokeWidth = line.present && line.color.alpha() > 0 ? line.width : 0;
    shape.line = line.style;
}

void applyFill(Shape &shape, const ResolvedFill &fill)
{
    shape.fill = fill.present ? fill.plain : QColor(Qt::transparent);
    shape.fillStyle = fill.present && fill.style.kind != FillKind::Plain ? fill.style : FillStyle();
}

ResolvedFill resolveGroupFill(ResolvedFill fill, const ResolvedFill *groupFill)
{
    if (fill.present && fill.style.kind == FillKind::Plain && fill.style.pattern == u"group") {
        return groupFill != nullptr ? *groupFill : ResolvedFill{true, Qt::transparent, {}};
    }
    return fill;
}

Shape readSp(const ShapeScope &scope, const Ooxml::XmlElementPtr &element, const ResolvedFill *groupFill)
{
    const Ooxml::XmlElement &e = *element;
    const QString &ns = e.ns;
    Shape s;
    const Ooxml::XmlElement *nv = e.child(ns, u"nvSpPr");
    readNonVisual(scope, nv != nullptr ? nv->child(ns, u"cNvPr") : nullptr, s);
    const Ooxml::XmlElement *cNvSpPr = nv != nullptr ? nv->child(ns, u"cNvSpPr") : nullptr;
    const bool txBox = cNvSpPr != nullptr && Ooxml::Units::parseOnOff(cNvSpPr->attr(u"txBox"), false);
    const Ooxml::XmlElement *nvPr = nv != nullptr ? nv->child(ns, u"nvPr") : nullptr;
    if (const Ooxml::XmlElement *ph = nvPr != nullptr ? nvPr->child(nsP(), u"ph") : nullptr) {
        s.placeholder.type = ph->attr(u"type", QStringLiteral("obj"));
        s.placeholder.index = ph->hasAttribute(QString(), u"idx") ? ph->attr(u"idx").toLongLong() : -1;
        s.placeholder.size = ph->attr(u"sz");
        s.placeholder.orient = ph->attr(u"orient");
    }
    const PlaceholderTemplate *lay = nullptr;
    const PlaceholderTemplate *mas = nullptr;
    if (s.placeholder.isPlaceholder()) {
        lay = scope.layout != nullptr ? scope.layout->match(s.placeholder) : nullptr;
        mas = scope.master != nullptr ? scope.master->matchType(lay != nullptr ? lay->ph.type : s.placeholder.type)
                                      : nullptr;
    }
    const Ooxml::XmlElement *spPr = e.child(ns, u"spPr");
    if (!readTransform(spPr != nullptr ? spPr->child(nsA(), u"xfrm") : nullptr, s)) {
        const PlaceholderTemplate *from = lay != nullptr && lay->hasXfrm ? lay : (mas != nullptr && mas->hasXfrm ? mas : nullptr);
        if (from != nullptr) {
            s.rect = from->rect;
            s.rotation = from->rotation;
            s.flipH = from->flipH;
            s.flipV = from->flipV;
        }
    }
    QString preset;
    QString custom;
    readGeometry(spPr, &preset, &s.adjust, &custom);
    for (const PlaceholderTemplate *t : {lay, mas}) {
        if (preset.isEmpty() && custom.isEmpty()) {
            readGeometry(spPrOf(t), &preset, &s.adjust, &custom);
        }
    }
    const Ooxml::XmlElement *style = e.child(ns, u"style");
    const Ooxml::XmlElement *inheritedStyle = style;
    for (const PlaceholderTemplate *t : {lay, mas}) {
        if (inheritedStyle == nullptr) {
            inheritedStyle = templateChild(t, u"style");
        }
    }
    // Fill: own, the layout's, the master's placeholder, then the style.
    ResolvedFill fill = fillFrom(scope.ctx, spPr);
    for (const PlaceholderTemplate *t : {lay, mas}) {
        if (!fill.present && t != nullptr) {
            fill = fillFrom(t->ctx, spPrOf(t));
        }
    }
    if (!fill.present) {
        fill = fillFromReference(scope.ctx, inheritedStyle != nullptr ? inheritedStyle->child(nsA(), u"fillRef") : nullptr);
    }
    applyFill(s, resolveGroupFill(fill, groupFill));
    ResolvedLine inheritedLine;
    for (const PlaceholderTemplate *t : {lay, mas}) {
        if (!inheritedLine.present && t != nullptr && spPrOf(t) != nullptr && spPrOf(t)->child(nsA(), u"ln") != nullptr) {
            inheritedLine = lineFrom(t->ctx, spPrOf(t)->child(nsA(), u"ln"));
        }
    }
    applyLine(s, effectiveLine(scope.ctx, spPr, inheritedStyle, &inheritedLine));
    // Text: body properties and styles down the inheritance chain.
    const Ooxml::XmlElement *txBody = e.child(ns, u"txBody");
    s.textBody = officeBodyDefaults();
    applyBodyProps(s.textBody, bodyPrOf(mas));
    applyBodyProps(s.textBody, bodyPrOf(lay));
    applyBodyProps(s.textBody, txBody != nullptr ? txBody->child(nsA(), u"bodyPr") : nullptr);
    if (scope.styles != nullptr) {
        ListStyle inherited = s.placeholder.isPlaceholder() ? scope.styles->forPlaceholder(s.placeholder.type)
                                                            : scope.styles->other;
        if (mas != nullptr) {
            inherited.overlay(mas->lstStyle);
        }
        if (lay != nullptr) {
            inherited.overlay(lay->lstStyle);
        }
        s.paragraphs = readTextBody(scope.ctx, txBody, inherited, fontReference(scope.ctx, inheritedStyle));
    }
    // Kind.
    const QString geometry = preset.isEmpty() && custom.isEmpty() ? QStringLiteral("rect") : preset;
    if (!custom.isEmpty()) {
        s.kind = ShapeKind::AutoShape;
        s.customGeometry = custom;
    } else if (geometry == u"rect") {
        s.kind = txBox || s.placeholder.isPlaceholder() ? ShapeKind::TextBox : ShapeKind::Rectangle;
        s.framed = s.kind == ShapeKind::TextBox && s.strokeWidth > 0;
    } else if (geometry == u"ellipse") {
        s.kind = ShapeKind::Ellipse;
    } else if (geometry == u"line" || geometry == u"straightConnector1") {
        s.kind = s.line.head.isNone() && s.line.tail.isNone() ? ShapeKind::Line : ShapeKind::Arrow;
        s.geometry = geometry;
    } else {
        s.kind = ShapeKind::AutoShape;
        s.geometry = geometry;
    }
    if (s.kind == ShapeKind::TextBox || s.kind == ShapeKind::Rectangle || s.kind == ShapeKind::Ellipse) {
        s.adjust.clear();
    }
    // A SmartArt drawing shape (dsp:sp) lays its text out in its own box
    // (dsp:txXfrm): render it as the shape plus a text box over that box.
    if (const Ooxml::XmlElement *txXfrm = e.child(ns, u"txXfrm"); txXfrm != nullptr && !s.paragraphs.isEmpty()) {
        Shape text;
        text.kind = ShapeKind::TextBox;
        text.id = s.id + QStringLiteral("_text");
        readTransform(txXfrm, text);
        text.paragraphs = s.paragraphs;
        text.textBody = s.textBody;
        text.strokeWidth = 0;
        s.paragraphs.clear();
        Shape pair;
        pair.kind = ShapeKind::Group;
        pair.id = s.id + QStringLiteral("_pair");
        const QRectF bounds = s.rect.normalized().united(text.rect.normalized());
        pair.rect = bounds;
        pair.childRect = bounds;
        pair.strokeWidth = 0;
        pair.fill = Qt::transparent;
        pair.children = {s, text};
        return pair;
    }
    return s;
}

Shape readPic(const ShapeScope &scope, const Ooxml::XmlElementPtr &element)
{
    const Ooxml::XmlElement &e = *element;
    const QString &ns = e.ns;
    Shape s;
    s.kind = ShapeKind::Image;
    const Ooxml::XmlElement *nv = e.child(ns, u"nvPicPr");
    readNonVisual(scope, nv != nullptr ? nv->child(ns, u"cNvPr") : nullptr, s);
    const Ooxml::XmlElement *nvPr = nv != nullptr ? nv->child(ns, u"nvPr") : nullptr;
    QString media;
    if (nvPr != nullptr) {
        if (nvPr->child(nsA(), u"videoFile") != nullptr || nvPr->child(nsA(), u"quickTimeFile") != nullptr) {
            media = QStringLiteral("Video");
        } else if (nvPr->child(nsA(), u"audioFile") != nullptr || nvPr->child(nsA(), u"wavAudioFile") != nullptr) {
            media = QStringLiteral("Audio");
        } else if (nvPr->descendant(kPresentation2010, u"media") != nullptr) {
            media = QStringLiteral("Media");
        }
        if (const Ooxml::XmlElement *ph = nvPr->child(nsP(), u"ph")) {
            s.placeholder.type = ph->attr(u"type", QStringLiteral("obj"));
            s.placeholder.index = ph->hasAttribute(QString(), u"idx") ? ph->attr(u"idx").toLongLong() : -1;
        }
    }
    const Ooxml::XmlElement *blipFill = e.child(ns, u"blipFill");
    if (blipFill == nullptr) {
        blipFill = e.child(nsA(), u"blipFill");
    }
    if (const Ooxml::XmlElement *blip = blipFill != nullptr ? blipFill->child(nsA(), u"blip") : nullptr) {
        const RelatedData data = relatedData(scope.ctx, blip->attribute(nsR(), u"embed"));
        s.imagePng = data.bytes;
        s.image.mime = data.mime;
        QBuffer buffer(&s.imagePng);
        buffer.open(QIODevice::ReadOnly);
        s.image.pixels = QImageReader(&buffer).size();
    }
    if (const Ooxml::XmlElement *src = blipFill != nullptr ? blipFill->child(nsA(), u"srcRect") : nullptr) {
        s.image.cropLeft = src->attr(u"l").toDouble() / 100000.0;
        s.image.cropTop = src->attr(u"t").toDouble() / 100000.0;
        s.image.cropRight = src->attr(u"r").toDouble() / 100000.0;
        s.image.cropBottom = src->attr(u"b").toDouble() / 100000.0;
    }
    const Ooxml::XmlElement *spPr = e.child(ns, u"spPr");
    if (!readTransform(spPr != nullptr ? spPr->child(nsA(), u"xfrm") : nullptr, s) && s.placeholder.isPlaceholder()) {
        const PlaceholderTemplate *lay = scope.layout != nullptr ? scope.layout->match(s.placeholder) : nullptr;
        if (lay != nullptr && lay->hasXfrm) {
            s.rect = lay->rect;
        }
    }
    QString preset;
    QString custom;
    readGeometry(spPr, &preset, &s.adjust, &custom);
    s.geometry = preset == u"rect" ? QString() : preset;
    s.customGeometry = custom;
    if (s.geometry.isEmpty()) {
        s.adjust.clear();
    }
    applyFill(s, fillFrom(scope.ctx, spPr));
    applyLine(s, effectiveLine(scope.ctx, spPr, e.child(ns, u"style")));
    s.framed = s.strokeWidth > 0;
    if (!media.isEmpty()) {
        // A video or sound: the poster frame shows, the media part travels.
        s.kind = ShapeKind::Preserved;
        s.preserved = {Ooxml::serializeFragment(e), scope.ctx.part, media};
    }
    return s;
}

Shape readCxnSp(const ShapeScope &scope, const Ooxml::XmlElementPtr &element)
{
    const Ooxml::XmlElement &e = *element;
    const QString &ns = e.ns;
    Shape s;
    s.connector = true;
    const Ooxml::XmlElement *nv = e.child(ns, u"nvCxnSpPr");
    readNonVisual(scope, nv != nullptr ? nv->child(ns, u"cNvPr") : nullptr, s);
    if (const Ooxml::XmlElement *c = nv != nullptr ? nv->child(ns, u"cNvCxnSpPr") : nullptr) {
        if (const Ooxml::XmlElement *st = c->child(nsA(), u"stCxn")) {
            s.start = {st->attr(u"id").toUInt(), st->attr(u"idx").toInt()};
        }
        if (const Ooxml::XmlElement *en = c->child(nsA(), u"endCxn")) {
            s.end = {en->attr(u"id").toUInt(), en->attr(u"idx").toInt()};
        }
    }
    const Ooxml::XmlElement *spPr = e.child(ns, u"spPr");
    readTransform(spPr != nullptr ? spPr->child(nsA(), u"xfrm") : nullptr, s);
    QString custom;
    readGeometry(spPr, &s.geometry, &s.adjust, &custom);
    if (s.geometry.isEmpty()) {
        s.geometry = QStringLiteral("straightConnector1");
    }
    applyLine(s, effectiveLine(scope.ctx, spPr, e.child(ns, u"style")));
    s.fill = Qt::transparent;
    s.kind = s.line.head.isNone() && s.line.tail.isNone() ? ShapeKind::Line : ShapeKind::Arrow;
    return s;
}

Shape readGrpSp(const ShapeScope &scope, const Ooxml::XmlElementPtr &element, const ResolvedFill *outerFill)
{
    const Ooxml::XmlElement &e = *element;
    const QString &ns = e.ns;
    Shape s;
    s.kind = ShapeKind::Group;
    const Ooxml::XmlElement *nv = e.child(ns, u"nvGrpSpPr");
    readNonVisual(scope, nv != nullptr ? nv->child(ns, u"cNvPr") : nullptr, s);
    const Ooxml::XmlElement *grpSpPr = e.child(ns, u"grpSpPr");
    const Ooxml::XmlElement *xfrm = grpSpPr != nullptr ? grpSpPr->child(nsA(), u"xfrm") : nullptr;
    readTransform(xfrm, s);
    if (xfrm != nullptr) {
        const Ooxml::XmlElement *chOff = xfrm->child(nsA(), u"chOff");
        const Ooxml::XmlElement *chExt = xfrm->child(nsA(), u"chExt");
        if (chOff != nullptr && chExt != nullptr) {
            s.childRect = QRectF(emuToPt(chOff->attr(u"x")), emuToPt(chOff->attr(u"y")), emuToPt(chExt->attr(u"cx")),
                                 emuToPt(chExt->attr(u"cy")));
        }
    }
    ResolvedFill groupFill = resolveGroupFill(fillFrom(scope.ctx, grpSpPr), outerFill);
    const QList<Shape> children = readShapeTree(scope, e, &groupFill);
    s.children.assign(children.cbegin(), children.cend());
    s.fill = Qt::transparent;
    s.strokeWidth = 0;
    return s;
}

} // namespace

Shape readElement(const ShapeScope &scope, const Ooxml::XmlElementPtr &element, const ResolvedFill *groupFill)
{
    const QString &name = element->name;
    Shape shape;
    if (element->ns == nsMc() && name == u"AlternateContent") {
        shape = readAlternateContent(scope, element, groupFill);
    } else if (name == u"sp") {
        shape = readSp(scope, element, groupFill);
    } else if (name == u"pic") {
        shape = readPic(scope, element);
    } else if (name == u"cxnSp") {
        shape = readCxnSp(scope, element);
    } else if (name == u"grpSp") {
        shape = readGrpSp(scope, element, groupFill);
    } else if (name == u"graphicFrame") {
        shape = readGraphicFrame(scope, element);
    } else if (name == u"contentPart") {
        shape = readPreserved(scope, element, QStringLiteral("Ink"));
    } else {
        shape = readPreserved(scope, element, name);
    }
    recordOrigin(scope, element, shape);
    countShape(scope, shape);
    return shape;
}

QList<Shape> readShapeTree(const ShapeScope &scope, const Ooxml::XmlElement &tree, const ResolvedFill *groupFill)
{
    QList<Shape> shapes;
    for (const Ooxml::XmlChild &child : tree.children) {
        if (!child.element) {
            continue;
        }
        const QString &name = child.element->name;
        if (name == u"nvGrpSpPr" || name == u"grpSpPr" || name == u"extLst") {
            continue;
        }
        shapes.append(readElement(scope, child.element, groupFill));
    }
    return shapes;
}

} // namespace QindaOffice::Show::Pptx
