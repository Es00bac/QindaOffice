// SPDX-License-Identifier: GPL-3.0-or-later
// The pptx writer's shape dispatch: verbatim, patched or generated
// (pptx_write_shapes.h).

#include "pptx_write_shapes.h"

#include "ooxml_units.h"

#include <functional>

namespace QindaOffice::Show::Pptx {

namespace {

using Ooxml::XmlChild;
using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;

const QList<QStringView> kAfterGeometry{u"noFill", u"solidFill", u"gradFill", u"blipFill", u"pattFill", u"grpFill",
                                        u"ln", u"effectLst", u"effectDag", u"scene3d", u"sp3d", u"extLst"};
const QList<QStringView> kAfterFill{u"ln", u"effectLst", u"effectDag", u"scene3d", u"sp3d", u"extLst"};
const QList<QStringView> kAfterLine{u"effectLst", u"effectDag", u"scene3d", u"sp3d", u"extLst"};

bool canCarry(const PartWriter &out, const Shape &shape)
{
    return shape.origin && shape.origin->xml && out.session.copier != nullptr;
}

void collectIds(const XmlElement &e, QList<quint32> &ids)
{
    if (e.name == u"cNvPr") {
        ids.append(e.attr(u"id").toUInt());
    }
    for (const XmlElement *c : e.elements()) {
        collectIds(*c, ids);
    }
}

// The element's own cNvPr (the first one in document order).
XmlElement *ownNonVisual(XmlElement &e)
{
    for (XmlChild &c : e.children) {
        if (!c.element) {
            continue;
        }
        if (c.element->name == u"cNvPr") {
            return c.element.get();
        }
        if (c.element->name.startsWith(QLatin1String("nv")) || c.element->name == u"Choice"
            || c.element->name == u"Fallback" || c.element->name == u"sp" || c.element->name == u"pic"
            || c.element->name == u"graphicFrame" || c.element->name == u"grpSp" || c.element->name == u"cxnSp") {
            if (XmlElement *found = ownNonVisual(*c.element)) {
                return found;
            }
        }
    }
    return nullptr;
}

void setId(XmlElement &e, quint32 id)
{
    if (XmlElement *own = ownNonVisual(e); own != nullptr && own->attr(u"id").toUInt() == id) {
        return;
    }
    // The element's own cNvPr only — never a group member's. mc:Alternate-
    // Content spells the shape twice (Choice and Fallback) with one id:
    // renumber the first cNvPr of each branch.
    const std::function<bool(XmlElement &)> renumber = [&](XmlElement &node) {
        if (XmlElement *own = ownNonVisual(node)) {
            own->setAttribute(QString(), QStringLiteral("id"), QString::number(id));
            return true;
        }
        return false;
    };
    if (e.name == u"AlternateContent") {
        for (XmlChild &c : e.children) {
            if (c.element && (c.element->name == u"Choice" || c.element->name == u"Fallback")) {
                renumber(*c.element);
            }
        }
        return;
    }
    renumber(e);
}

XmlElement &ensureChild(XmlElement &parent, const QString &ns, const QString &name, const QList<QStringView> &before)
{
    if (XmlElement *found = parent.child(ns, name)) {
        return *found;
    }
    return parent.insertElementBefore(std::make_shared<XmlElement>(ns, name), before);
}

void replaceChild(XmlElement &parent, const XmlElementPtr &replacement, const QList<QStringView> &names,
                  const QList<QStringView> &before)
{
    for (QStringView name : names) {
        parent.removeChildren(nsA(), name);
    }
    if (replacement) {
        parent.insertElementBefore(replacement, before);
    }
}

bool sameGeometry(const Shape &a, const Shape &b)
{
    return a.rect == b.rect && a.rotation == b.rotation && a.flipH == b.flipH && a.flipV == b.flipV
        && a.childRect == b.childRect;
}

bool sameOutline(const Shape &a, const Shape &b)
{
    return a.stroke == b.stroke && a.strokeWidth == b.strokeWidth && a.line == b.line && a.framed == b.framed;
}

void patchNonVisual(PartWriter &out, XmlElement &e, const Shape &shape, const Shape &old)
{
    XmlElement *cNvPr = ownNonVisual(e);
    if (cNvPr == nullptr) {
        return;
    }
    if (shape.name != old.name) {
        cNvPr->setAttribute(QString(), QStringLiteral("name"), shape.name);
    }
    if (shape.description != old.description) {
        if (shape.description.isEmpty()) {
            cNvPr->removeAttribute(QString(), u"descr");
        } else {
            cNvPr->setAttribute(QString(), QStringLiteral("descr"), shape.description);
        }
    }
    if (shape.hidden != old.hidden) {
        if (shape.hidden) {
            cNvPr->setAttribute(QString(), QStringLiteral("hidden"), QStringLiteral("1"));
        } else {
            cNvPr->removeAttribute(QString(), u"hidden");
        }
    }
    if (shape.hyperlink != old.hyperlink) {
        cNvPr->removeChildren(nsA(), u"hlinkClick");
        if (XmlElementPtr link = out.hyperlink(shape.hyperlink)) {
            cNvPr->insertElementBefore(link, {u"hlinkHover", u"extLst"});
        }
    }
}

void patchShapeProperties(PartWriter &out, XmlElement &spPr, const Shape &shape, const Shape &old)
{
    if (!sameGeometry(shape, old)) {
        spPr.removeChildren(nsA(), u"xfrm");
        spPr.insertElementBefore(transformElement(nsA(), shape, false), {u"custGeom", u"prstGeom", u"noFill",
                                                                         u"solidFill", u"gradFill", u"blipFill",
                                                                         u"pattFill", u"grpFill", u"ln", u"effectLst",
                                                                         u"effectDag", u"scene3d", u"sp3d", u"extLst"});
    }
    if (shape.geometry != old.geometry || shape.adjust != old.adjust || shape.customGeometry != old.customGeometry) {
        spPr.removeChildren(nsA(), u"prstGeom");
        spPr.removeChildren(nsA(), u"custGeom");
        XmlElementPtr geometry = shape.customGeometry.isEmpty() ? nullptr : Ooxml::parseFragment(shape.customGeometry);
        if (!geometry) {
            geometry = std::make_shared<XmlElement>(nsA(), QStringLiteral("prstGeom"));
            QString preset = shape.geometry;
            if (preset.isEmpty()) {
                preset = shape.kind == ShapeKind::Ellipse ? QStringLiteral("ellipse") : QStringLiteral("rect");
            }
            geometry->setAttribute(QString(), QStringLiteral("prst"), preset);
            XmlElement &av = geometry->appendElement(nsA(), QStringLiteral("avLst"));
            for (const auto &[name, value] : shape.adjust) {
                XmlElement &gd = av.appendElement(nsA(), QStringLiteral("gd"));
                gd.setAttribute(QString(), QStringLiteral("name"), name);
                gd.setAttribute(QString(), QStringLiteral("fmla"), QStringLiteral("val %1").arg(value));
            }
        }
        spPr.insertElementBefore(geometry, kAfterGeometry);
    }
    if (shape.fill != old.fill || shape.fillStyle != old.fillStyle) {
        replaceChild(spPr, fillElement(out, shape.fill, shape.fillStyle),
                     {u"noFill", u"solidFill", u"gradFill", u"blipFill", u"pattFill", u"grpFill"}, kAfterFill);
    }
    if (!sameOutline(shape, old)) {
        const bool lineKind = shape.kind == ShapeKind::Line || shape.kind == ShapeKind::Arrow;
        LineStyle line = shape.line;
        if (shape.kind == ShapeKind::Arrow && line.head.isNone() && line.tail.isNone()) {
            line.tail.type = QStringLiteral("triangle");
        }
        const bool visible = (shape.kind != ShapeKind::TextBox && shape.kind != ShapeKind::Image) || shape.framed;
        replaceChild(spPr, lineElement(shape.stroke, lineKind && shape.strokeWidth <= 0 ? 1.5 : shape.strokeWidth, line, visible),
                     {u"ln"}, kAfterLine);
    }
}

void patchPicture(PartWriter &out, XmlElement &e, const Shape &shape, const Shape &old)
{
    XmlElement *blipFill = e.child(e.ns, u"blipFill");
    if (blipFill == nullptr) {
        return;
    }
    if (shape.imagePng != old.imagePng) {
        XmlElement &blip = ensureChild(*blipFill, nsA(), QStringLiteral("blip"), {u"srcRect", u"tile", u"stretch"});
        blip.setAttribute(nsR(), QStringLiteral("embed"),
                          out.relate(relType("image"), out.session.media(shape.imagePng, shape.image.mime)));
        blip.removeChildren(nsA(), u"extLst"); // an SVG twin would contradict the new picture
    }
    if (shape.image != old.image) {
        blipFill->removeChildren(nsA(), u"srcRect");
        if (shape.image.isCropped()) {
            auto src = std::make_shared<XmlElement>(nsA(), QStringLiteral("srcRect"));
            const auto edge = [&src](const char *name, qreal value) {
                if (value != 0) {
                    src->setAttribute(QString(), QLatin1String(name), QString::number(qint64(std::llround(value * 100000))));
                }
            };
            edge("l", shape.image.cropLeft);
            edge("t", shape.image.cropTop);
            edge("r", shape.image.cropRight);
            edge("b", shape.image.cropBottom);
            blipFill->insertElementBefore(src, {u"tile", u"stretch"});
        }
    }
}

void patchConnector(XmlElement &e, const Shape &shape, const Shape &old)
{
    if (shape.start == old.start && shape.end == old.end) {
        return;
    }
    XmlElement *nv = e.child(e.ns, u"nvCxnSpPr");
    XmlElement *c = nv != nullptr ? nv->child(e.ns, u"cNvCxnSpPr") : nullptr;
    if (c == nullptr) {
        return;
    }
    c->removeChildren(nsA(), u"stCxn");
    c->removeChildren(nsA(), u"endCxn");
    for (const auto &[end, name] : {std::pair{&shape.start, "stCxn"}, std::pair{&shape.end, "endCxn"}}) {
        if (end->shapeId != 0) {
            XmlElement &cx = c->insertElementBefore(std::make_shared<XmlElement>(nsA(), QLatin1String(name)), {u"extLst"});
            cx.setAttribute(QString(), QStringLiteral("id"), QString::number(end->shapeId));
            cx.setAttribute(QString(), QStringLiteral("idx"), QString::number(end->site));
        }
    }
}

// Group members: each decides verbatim / patched / generated itself.
void patchGroup(PartWriter &out, XmlElement &e, const Shape &shape)
{
    QList<XmlChild> kept;
    for (const XmlChild &c : e.children) {
        if (!c.element || c.element->name == u"nvGrpSpPr" || c.element->name == u"grpSpPr") {
            kept.append(c);
        }
    }
    XmlChild extension;
    for (const XmlChild &c : e.children) {
        if (c.element && c.element->name == u"extLst") {
            extension = c;
        }
    }
    e.children = kept;
    for (const Shape &child : shape.children) {
        if (XmlElementPtr written = writeShape(out, child, out.claim(child.nvId))) {
            e.appendElement(written);
        }
    }
    if (extension.element) {
        e.children.append(extension);
    }
}

XmlElement *childNamed(XmlElement &parent, QStringView name)
{
    for (XmlChild &c : parent.children) {
        if (c.element && c.element->name == name) {
            return c.element.get();
        }
    }
    return nullptr;
}

// Maps a transform's box from `from` to `to` (both in slide points).
void moveTransform(XmlElement &xfrm, const QRectF &from, const QRectF &to)
{
    XmlElement *off = childNamed(xfrm, u"off");
    XmlElement *ext = childNamed(xfrm, u"ext");
    if (off == nullptr || ext == nullptr) {
        return;
    }
    const qreal sx = from.width() > 0 ? to.width() / from.width() : 1.0;
    const qreal sy = from.height() > 0 ? to.height() / from.height() : 1.0;
    const qreal x = emuToPt(off->attr(u"x"));
    const qreal y = emuToPt(off->attr(u"y"));
    off->setAttribute(QString(), QStringLiteral("x"), emu(to.left() + (x - from.left()) * sx));
    off->setAttribute(QString(), QStringLiteral("y"), emu(to.top() + (y - from.top()) * sy));
    ext->setAttribute(QString(), QStringLiteral("cx"), emu(emuToPt(ext->attr(u"cx")) * sx));
    ext->setAttribute(QString(), QStringLiteral("cy"), emu(emuToPt(ext->attr(u"cy")) * sy));
}

void movePreserved(XmlElement &e, const QRectF &from, const QRectF &to)
{
    if (e.name == u"AlternateContent" || e.name == u"Choice" || e.name == u"Fallback") {
        for (XmlChild &c : e.children) {
            if (c.element) {
                movePreserved(*c.element, from, to);
            }
        }
        return;
    }
    XmlElement *xfrm = childNamed(e, u"xfrm");
    for (const char16_t *properties : {u"spPr", u"grpSpPr"}) {
        if (xfrm == nullptr) {
            if (XmlElement *pr = childNamed(e, properties)) {
                xfrm = childNamed(*pr, u"xfrm");
            }
        }
    }
    if (xfrm != nullptr) {
        moveTransform(*xfrm, from, to);
    }
}

XmlElementPtr patchShape(PartWriter &out, const Shape &shape, const ShapeSource &source, quint32 id)
{
    const Shape &old = source.pristine;
    if (shape.kind != old.kind) {
        return generateShape(out, shape, id);
    }
    XmlElementPtr e = source.xml->clone();
    DetachedChart chart;
    if (shape.kind == ShapeKind::Chart && shape.chart != old.chart && e->name == u"graphicFrame") {
        chart = detachChart(out, *e, source.part);
    }
    // AGENT-GUARD (pptx_write_context.h): carry the source ids first.
    out.carry(*e, source.part);
    setId(*e, id);
    patchNonVisual(out, *e, shape, old);
    const QString &ns = e->ns;
    const QString name = e->name;
    if (name == u"sp" || name == u"pic" || name == u"cxnSp") {
        XmlElement &spPr = ensureChild(*e, ns, QStringLiteral("spPr"), {u"style", u"txBody", u"extLst"});
        patchShapeProperties(out, spPr, shape, old);
        if (name == u"pic") {
            patchPicture(out, *e, shape, old);
        }
        if (name == u"cxnSp") {
            patchConnector(*e, shape, old);
        }
        if (name == u"sp" && (shape.paragraphs != old.paragraphs || shape.textBody != old.textBody)) {
            TextSource text{source.xml->child(ns, u"txBody"), &old.paragraphs, &old.textBody, source.part};
            e->removeChildren(ns, u"txBody");
            if (!shape.paragraphs.isEmpty() || text.txBody != nullptr) {
                e->insertElementBefore(textBodyElement(out, ns, shape.paragraphs, shape.textBody, text), {u"extLst"});
            }
        }
    } else if (name == u"grpSp") {
        if (!sameGeometry(shape, old)) {
            XmlElement &grpSpPr = ensureChild(*e, ns, QStringLiteral("grpSpPr"), {u"sp", u"grpSp", u"graphicFrame", u"cxnSp", u"pic", u"contentPart", u"extLst"});
            grpSpPr.removeChildren(nsA(), u"xfrm");
            grpSpPr.insertElementBefore(transformElement(nsA(), shape, true), kAfterGeometry);
        }
        patchGroup(out, *e, shape);
    } else if (name == u"graphicFrame") {
        if (!sameGeometry(shape, old)) {
            e->removeChildren(ns, u"xfrm");
            e->insertElementBefore(transformElement(ns, shape, false), {u"graphic", u"extLst"});
        }
        if (chart.active() && !attachChart(out, chart, shape.chart, old.chart)) {
            return pictureFallback(out, shape, id);
        }
        if (shape.kind == ShapeKind::Table && shape.table != old.table) {
            XmlElement *graphic = e->child(nsA(), u"graphic");
            XmlElement *data = graphic != nullptr ? graphic->child(nsA(), u"graphicData") : nullptr;
            const XmlElement *sourceGraphic = source.xml->child(nsA(), u"graphic");
            const XmlElement *sourceData = sourceGraphic != nullptr ? sourceGraphic->child(nsA(), u"graphicData") : nullptr;
            if (data != nullptr) {
                data->removeChildren(nsA(), u"tbl");
                data->appendElement(tableElement(out, shape.table, sourceData != nullptr ? sourceData->child(nsA(), u"tbl") : nullptr,
                                                 &old.table, source.part));
            }
        }
    } else if (!sameGeometry(shape, old)) {
        // Preserved content (alternate content, ink, unknown elements):
        // move and scale the transform of every branch.
        movePreserved(*e, old.rect.normalized(), shape.rect.normalized());
    }
    return e;
}

} // namespace

QList<bool> reserveShapeIds(PartWriter &out, const QList<Shape> &shapes)
{
    QList<bool> keeps;
    for (const Shape &shape : shapes) {
        const bool keep = out.isFree(shape.nvId);
        if (keep) {
            out.reserve(shape.nvId);
        }
        keeps.append(keep);
        // Ids nested inside content read from the deck (group members,
        // alternate content) stay theirs, verbatim or patched.
        if (keep && canCarry(out, shape)) {
            QList<quint32> nested;
            collectIds(*shape.origin->xml, nested);
            for (const quint32 id : nested) {
                if (id != shape.nvId && out.isFree(id)) {
                    out.reserve(id);
                }
            }
        }
    }
    return keeps;
}

XmlElementPtr writeShape(PartWriter &out, const Shape &shape, quint32 id)
{
    if (canCarry(out, shape)) {
        const ShapeSource &source = *shape.origin.source;
        if (shape == source.pristine) {
            XmlElementPtr e = source.xml->clone();
            out.carry(*e, source.part);
            setId(*e, id);
            return e;
        }
        return patchShape(out, shape, source, id);
    }
    if ((shape.kind == ShapeKind::Preserved || shape.kind == ShapeKind::Chart) && !shape.preserved.isEmpty()
        && out.session.copier != nullptr && out.session.source->contains(shape.preserved.part)) {
        // A copy of a preserved shape (duplicated slide, paste in the same
        // deck): its XML still resolves against the source package.
        if (XmlElementPtr e = Ooxml::parseFragment(shape.preserved.fragment)) {
            // A pasted chart edited since: compare with what its part caches.
            ChartData cached;
            const DetachedChart chart = shape.kind == ShapeKind::Chart
                ? detachEditedCopy(out, *e, shape.preserved.part, shape.chart, &cached)
                : DetachedChart();
            out.carry(*e, shape.preserved.part);
            setId(*e, id);
            if (chart.active() && !attachChart(out, chart, shape.chart, cached)) {
                return pictureFallback(out, shape, id);
            }
            return e;
        }
    }
    return generateShape(out, shape, id);
}

} // namespace QindaOffice::Show::Pptx
